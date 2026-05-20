extern "C" {

struct fastcdc_gpu_kernel_result {
	int chunk_size;
	int cutoff_hit;
	unsigned long long fingerprint_updates;
};

__global__ void fastcdc_chunk_kernel(const unsigned char *input,
		int n,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		int expect_size,
		unsigned long long mask_s,
		unsigned long long mask_l,
		struct fastcdc_gpu_kernel_result *result) {
	unsigned long long fingerprint = 0;
	unsigned long long updates = 0;
	int i = 0;
	int mid = expect_size;

	if (blockIdx.x != 0 || threadIdx.x != 0) {
		return;
	}

	result->chunk_size = n;
	result->cutoff_hit = 0;
	result->fingerprint_updates = 0;

	if (n <= min_size) {
		return;
	}
	if (n > max_size) {
		n = max_size;
	} else if (n < mid) {
		mid = n;
	}

	while (i < mid) {
		fingerprint = (fingerprint << 1) + gear_matrix[input[i]];
		updates++;
		if (!(fingerprint & mask_s)) {
			result->chunk_size = i;
			result->cutoff_hit = 1;
			result->fingerprint_updates = updates;
			return;
		}
		i++;
	}

	while (i < n) {
		fingerprint = (fingerprint << 1) + gear_matrix[input[i]];
		updates++;
		if (!(fingerprint & mask_l)) {
			result->chunk_size = i;
			result->cutoff_hit = 1;
			result->fingerprint_updates = updates;
			return;
		}
		i++;
	}

	result->chunk_size = i;
	result->fingerprint_updates = updates;
}

}