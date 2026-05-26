extern "C" {

static __device__ __forceinline__ int gpu_effective_window(int warp_window) {
	if (warp_window <= 0) {
		return 32;
	}
	return warp_window < 32 ? warp_window : 32;
}

struct fastcdc_gpu_kernel_result {
	int chunk_size;
	int cutoff_hit;
	int cutoff_lane;
	int tail_idle_lanes;
	unsigned long long fingerprint_updates;
	unsigned long long redundant_checks;
	unsigned long long warp_groups;
	unsigned long long jump_hits;
	unsigned long long jump_bytes_skipped;
};

__global__ void fastcdc_chunk_kernel(const unsigned char *input,
		const int *input_offsets,
		const int *input_lengths,
		int task_count,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		int expect_size,
		unsigned long long mask_s,
		unsigned long long mask_l,
		int warp_window,
		struct fastcdc_gpu_kernel_result *result) {
	const int task_id = (int)(blockIdx.x * blockDim.x + threadIdx.x);
	const unsigned char *task_input;
	struct fastcdc_gpu_kernel_result *task_result;
	int n = 0;
	unsigned long long fingerprint = 0;
	unsigned long long updates = 0;
	unsigned long long redundant = 0;
	unsigned long long warp_groups = 0;
	int i = 0;
	int mid = expect_size;
	int end;
	int chunk_size;
	int cutoff_hit = 0;
	int cutoff_lane = -1;
	int tail_idle_lanes = 0;
	int effective_warp_window = gpu_effective_window(warp_window);

	if (task_id >= task_count) {
		return;
	}

	task_input = input + input_offsets[task_id];
	n = input_lengths[task_id];
	end = n;
	chunk_size = n;
	task_result = result + task_id;
	task_result->chunk_size = n;
	task_result->cutoff_hit = 0;
	task_result->cutoff_lane = -1;
	task_result->tail_idle_lanes = 0;
	task_result->fingerprint_updates = 0;
	task_result->redundant_checks = 0;
	task_result->warp_groups = 0;
	task_result->jump_hits = 0;
	task_result->jump_bytes_skipped = 0;

	if (n <= min_size) {
		return;
	}
	if (end > max_size) {
		end = max_size;
	} else if (end < mid) {
		mid = end;
	}
	chunk_size = end;

	for (int phase = 0; phase < 2 && !cutoff_hit; phase++) {
		int phase_end = phase == 0 ? mid : end;
		unsigned long long phase_mask = phase == 0 ? mask_s : mask_l;

		while (i < phase_end && !cutoff_hit) {
			fingerprint = (fingerprint << 1) + gear_matrix[task_input[i]];
			updates++;
			if (!(fingerprint & phase_mask)) {
				chunk_size = i;
				cutoff_hit = 1;
				cutoff_lane = effective_warp_window > 0 ? (i % effective_warp_window) : 0;
			} else {
				i++;
			}
		}
	}

	if (!cutoff_hit) {
		chunk_size = i;
	}
	warp_groups = (updates + (unsigned long long)effective_warp_window - 1ULL)
			/ (unsigned long long)effective_warp_window;
	task_result->chunk_size = chunk_size;
	task_result->cutoff_hit = cutoff_hit;
	task_result->cutoff_lane = cutoff_lane;
	task_result->tail_idle_lanes = tail_idle_lanes;
	task_result->fingerprint_updates = updates;
	task_result->redundant_checks = redundant;
	task_result->warp_groups = warp_groups;
}

__global__ void jc_chunk_kernel(const unsigned char *input,
		const int *input_offsets,
		const int *input_lengths,
		int task_count,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		unsigned long long mask,
		unsigned long long jump_mask,
		int jump_len,
		int warp_window,
		struct fastcdc_gpu_kernel_result *result) {
	const int task_id = (int)(blockIdx.x * blockDim.x + threadIdx.x);
	const unsigned char *task_input;
	struct fastcdc_gpu_kernel_result *task_result;
	int n = 0;
	unsigned long long fingerprint = 0;
	unsigned long long updates = 0;
	unsigned long long redundant = 0;
	unsigned long long warp_groups = 0;
	unsigned long long jump_hits = 0;
	unsigned long long jump_bytes_skipped = 0;
	int i = min_size;
	int chunk_size;
	int cutoff_hit = 0;
	int cutoff_lane = -1;
	int tail_idle_lanes = 0;
	int effective_warp_window = gpu_effective_window(warp_window);
	int end;

	if (task_id >= task_count) {
		return;
	}

	task_input = input + input_offsets[task_id];
	n = input_lengths[task_id];
	chunk_size = n;
	end = n;
	task_result = result + task_id;
	task_result->chunk_size = n;
	task_result->cutoff_hit = 0;
	task_result->cutoff_lane = -1;
	task_result->tail_idle_lanes = 0;
	task_result->fingerprint_updates = 0;
	task_result->redundant_checks = 0;
	task_result->warp_groups = 0;
	task_result->jump_hits = 0;
	task_result->jump_bytes_skipped = 0;

	if (n <= min_size) {
		return;
	}
	if (n > max_size) {
		end = max_size;
	}
	chunk_size = end;

	while (i < end && !cutoff_hit) {
		fingerprint = (fingerprint << 1) + gear_matrix[task_input[i]];
		updates++;
		if (!(fingerprint & jump_mask)) {
			if (!(fingerprint & mask)) {
				chunk_size = i + 1;
				cutoff_hit = 1;
				cutoff_lane = effective_warp_window > 0 ? (i % effective_warp_window) : 0;
			} else {
				fingerprint = 0;
				jump_hits++;
				jump_bytes_skipped += (unsigned long long)jump_len;
				i += jump_len + 1;
			}
		} else {
			i++;
		}
	}

	if (!cutoff_hit) {
		chunk_size = i < end ? i : end;
	}

	warp_groups = (updates + (unsigned long long)effective_warp_window - 1ULL)
			/ (unsigned long long)effective_warp_window;
	task_result->chunk_size = chunk_size;
	task_result->cutoff_hit = cutoff_hit;
	task_result->cutoff_lane = cutoff_lane;
	task_result->tail_idle_lanes = tail_idle_lanes;
	task_result->fingerprint_updates = updates;
	task_result->redundant_checks = redundant;
	task_result->warp_groups = warp_groups;
	task_result->jump_hits = jump_hits;
	task_result->jump_bytes_skipped = jump_bytes_skipped;
}

}