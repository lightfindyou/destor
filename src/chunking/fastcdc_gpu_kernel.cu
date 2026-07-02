extern "C" {

struct fastcdc_gpu_segment_result {
	int boundary_count;
	int consumed_bytes;
};

static __device__ __forceinline__ void gpu_init_result(struct fastcdc_gpu_segment_result *task_result) {
	task_result->boundary_count = 0;
	task_result->consumed_bytes = 0;
}

static __device__ __forceinline__ void gpu_finalize_result(struct fastcdc_gpu_segment_result *task_result) {
	(void)task_result;
}

static __device__ __forceinline__ void gpu_compose_affine(unsigned long long left_a,
		unsigned long long left_b,
		unsigned long long right_a,
		unsigned long long right_b,
		unsigned long long *out_a,
		unsigned long long *out_b) {
	*out_a = right_a * left_a;
	*out_b = right_a * left_b + right_b;
}

static __device__ void gpu_prefix_scan_tile(unsigned long long elem_a,
		unsigned long long elem_b,
		int active_count,
		unsigned long long base_fingerprint,
		unsigned long long *scan_a_cur,
		unsigned long long *scan_b_cur,
		unsigned long long *scan_a_next,
		unsigned long long *scan_b_next,
		unsigned long long *local_fingerprint,
		unsigned long long *tile_end_fingerprint) {
	int tid = (int)threadIdx.x;
	unsigned long long local_a = 1ULL;
	unsigned long long local_b = 0ULL;

	if (tid < active_count) {
		local_a = elem_a;
		local_b = elem_b;
	}
	scan_a_cur[tid] = local_a;
	scan_b_cur[tid] = local_b;
	__syncthreads();

	for (int offset = 1; offset < active_count; offset <<= 1) {
		if (tid < active_count && tid >= offset) {
			gpu_compose_affine(scan_a_cur[tid - offset],
					scan_b_cur[tid - offset],
					local_a,
					local_b,
					&local_a,
					&local_b);
		}
		__syncthreads();
		if (tid < active_count) {
			scan_a_next[tid] = local_a;
			scan_b_next[tid] = local_b;
		}
		__syncthreads();
		if (tid < active_count) {
			local_a = scan_a_next[tid];
			local_b = scan_b_next[tid];
			scan_a_cur[tid] = local_a;
			scan_b_cur[tid] = local_b;
		}
		__syncthreads();
	}

	if (tid < active_count) {
		*local_fingerprint = local_a * base_fingerprint + local_b;
		if (tid == active_count - 1) {
			*tile_end_fingerprint = *local_fingerprint;
		}
	}
	__syncthreads();
}

static __device__ __forceinline__ void gpu_mark_segment_result(struct fastcdc_gpu_segment_result *task_result,
		int target_n,
		int n,
		int boundary_stride) {
	(void)boundary_stride;

	if (task_result->boundary_count < 0) {
		return;
	}
	if (task_result->consumed_bytes >= target_n || task_result->consumed_bytes >= n) {
		return;
	}
	if (task_result->boundary_count >= boundary_stride) {
		return;
	}
	task_result->boundary_count = -1;
}

__global__ void fastcdc_chunk_kernel(const unsigned char *input,
		const long long *input_offsets,
		const int *input_lengths,
		const int *target_lengths,
		int task_count,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		int expect_size,
		unsigned long long mask_s,
		unsigned long long mask_l,
		int warp_window,
		int boundary_stride,
		int *boundaries,
		struct fastcdc_gpu_segment_result *result) {
	extern __shared__ unsigned long long shared_u64[];
	unsigned long long *scan_a_cur = shared_u64;
	unsigned long long *scan_b_cur = scan_a_cur + blockDim.x;
	unsigned long long *scan_a_next = scan_b_cur + blockDim.x;
	unsigned long long *scan_b_next = scan_a_next + blockDim.x;
	const int task_id = (int)blockIdx.x;
	const int tid = (int)threadIdx.x;
	const unsigned char *task_input;
	struct fastcdc_gpu_segment_result *task_result;
	int *task_boundaries;
	int n;
	int target_n;
	__shared__ int first_match;
	__shared__ unsigned long long tile_end_fingerprint;

	(void)warp_window;

	if (task_id >= task_count) {
		return;
	}

	task_input = input + input_offsets[task_id];
	n = input_lengths[task_id];
	target_n = target_lengths[task_id];
	task_result = result + task_id;
	task_boundaries = boundaries + (task_id * boundary_stride);

	if (tid == 0) {
		gpu_init_result(task_result);
	}
	__syncthreads();

	if (n <= 0 || boundary_stride <= 0 || target_n <= 0) {
		if (tid == 0) {
			gpu_finalize_result(task_result);
		}
		return;
	}
	if (target_n > n) {
		target_n = n;
	}

	while (task_result->consumed_bytes < n && task_result->boundary_count < boundary_stride) {
		const unsigned char *chunk_input = task_input + task_result->consumed_bytes;
		int chunk_remaining = n - task_result->consumed_bytes;
		unsigned long long fingerprint = 0ULL;
		int mid = expect_size;
		int end = chunk_remaining;
		int chunk_size = chunk_remaining;
		int cutoff_hit = 0;

		if (chunk_remaining > min_size) {
			if (end > max_size) {
				end = max_size;
			} else if (end < mid) {
				mid = end;
			}
			chunk_size = end;

			for (int phase = 0; phase < 2 && !cutoff_hit; phase++) {
				int phase_end = phase == 0 ? mid : end;
				unsigned long long phase_mask = phase == 0 ? mask_s : mask_l;
				int position = phase == 0 ? 0 : mid;

				while (position < phase_end && !cutoff_hit) {
					int active_count = phase_end - position;

					if (active_count > blockDim.x) {
						active_count = blockDim.x;
					}

					gpu_prefix_scan_tile(tid < active_count ? 2ULL : 1ULL,
							tid < active_count ? gear_matrix[chunk_input[position + tid]] : 0ULL,
							active_count,
							fingerprint,
							scan_a_cur,
							scan_b_cur,
							scan_a_next,
							scan_b_next,
							&fingerprint,
							&tile_end_fingerprint);

					if (tid == 0) {
						first_match = 0x7fffffff;
					}
					__syncthreads();
					if (tid < active_count && !(fingerprint & phase_mask)) {
						atomicMin(&first_match, tid);
					}
					__syncthreads();

					if (first_match != 0x7fffffff) {
						chunk_size = position + first_match;
						cutoff_hit = 1;
					} else {
						position += active_count;
						fingerprint = tile_end_fingerprint;
					}
					__syncthreads();
				}
			}
		}

		if (tid == 0) {
			if (chunk_size <= 0 || chunk_size > chunk_remaining) {
				task_result->boundary_count = -1;
			} else {
				task_result->consumed_bytes += chunk_size;
				task_boundaries[task_result->boundary_count] = task_result->consumed_bytes;
				task_result->boundary_count++;
			}
		}
		__syncthreads();

		if (task_result->boundary_count < 0 || task_result->consumed_bytes >= target_n) {
			break;
		}
	}

	if (tid == 0) {
		gpu_mark_segment_result(task_result, target_n, n, boundary_stride);
		gpu_finalize_result(task_result);
	}
}

__global__ void jc_chunk_kernel(const unsigned char *input,
		const long long *input_offsets,
		const int *input_lengths,
		const int *target_lengths,
		int task_count,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		unsigned long long mask,
		unsigned long long jump_mask,
		int jump_len,
		int warp_window,
		int boundary_stride,
		int *boundaries,
		struct fastcdc_gpu_segment_result *result) {
	extern __shared__ unsigned long long shared_u64[];
	unsigned long long *scan_a_cur = shared_u64;
	unsigned long long *scan_b_cur = scan_a_cur + blockDim.x;
	unsigned long long *scan_a_next = scan_b_cur + blockDim.x;
	unsigned long long *scan_b_next = scan_a_next + blockDim.x;
	const int task_id = (int)blockIdx.x;
	const int tid = (int)threadIdx.x;
	const unsigned char *task_input;
	struct fastcdc_gpu_segment_result *task_result;
	int *task_boundaries;
	int n;
	int target_n;
	__shared__ int first_match;
	__shared__ unsigned long long tile_end_fingerprint;
	__shared__ unsigned long long first_match_fingerprint;

	(void)warp_window;

	if (task_id >= task_count) {
		return;
	}

	task_input = input + input_offsets[task_id];
	n = input_lengths[task_id];
	target_n = target_lengths[task_id];
	task_result = result + task_id;
	task_boundaries = boundaries + (task_id * boundary_stride);

	if (tid == 0) {
		gpu_init_result(task_result);
	}
	__syncthreads();

	if (n <= 0 || boundary_stride <= 0 || target_n <= 0) {
		if (tid == 0) {
			gpu_finalize_result(task_result);
		}
		return;
	}
	if (target_n > n) {
		target_n = n;
	}

	while (task_result->consumed_bytes < n && task_result->boundary_count < boundary_stride) {
		const unsigned char *chunk_input = task_input + task_result->consumed_bytes;
		int chunk_remaining = n - task_result->consumed_bytes;
		unsigned long long fingerprint = 0ULL;
		int position = min_size;
		int end = chunk_remaining;
		int chunk_size = chunk_remaining;
		int cutoff_hit = 0;

		if (chunk_remaining > min_size) {
			if (chunk_remaining > max_size) {
				end = max_size;
			}
			chunk_size = end;

			while (position < end && !cutoff_hit) {
				int active_count = end - position;

				if (active_count > blockDim.x) {
					active_count = blockDim.x;
				}

				gpu_prefix_scan_tile(tid < active_count ? 2ULL : 1ULL,
						tid < active_count ? gear_matrix[chunk_input[position + tid]] : 0ULL,
						active_count,
						fingerprint,
						scan_a_cur,
						scan_b_cur,
						scan_a_next,
						scan_b_next,
						&fingerprint,
						&tile_end_fingerprint);

				if (tid == 0) {
					first_match = 0x7fffffff;
				}
				__syncthreads();
				if (tid < active_count && !(fingerprint & jump_mask)) {
					atomicMin(&first_match, tid);
				}
				__syncthreads();

				if (first_match != 0x7fffffff) {
					if (tid == first_match) {
						first_match_fingerprint = fingerprint;
					}
					__syncthreads();
					if (!(first_match_fingerprint & mask)) {
						chunk_size = position + first_match + 1;
						cutoff_hit = 1;
					} else {
						position += first_match + jump_len + 1;
						fingerprint = 0ULL;
					}
				} else {
					position += active_count;
					fingerprint = tile_end_fingerprint;
				}
				__syncthreads();
			}
		}

		if (tid == 0) {
			if (chunk_size <= 0 || chunk_size > chunk_remaining) {
				task_result->boundary_count = -1;
			} else {
				task_result->consumed_bytes += chunk_size;
				task_boundaries[task_result->boundary_count] = task_result->consumed_bytes;
				task_result->boundary_count++;
			}
		}
		__syncthreads();

		if (task_result->boundary_count < 0 || task_result->consumed_bytes >= target_n) {
			break;
		}
	}

	if (tid == 0) {
		gpu_mark_segment_result(task_result, target_n, n, boundary_stride);
		gpu_finalize_result(task_result);
	}
}

__global__ void gear_chunk_kernel(const unsigned char *input,
		const long long *input_offsets,
		const int *input_lengths,
		const int *target_lengths,
		int task_count,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		unsigned long long mask,
		int warp_window,
		int boundary_stride,
		int *boundaries,
		struct fastcdc_gpu_segment_result *result) {
	extern __shared__ unsigned long long shared_u64[];
	unsigned long long *scan_a_cur = shared_u64;
	unsigned long long *scan_b_cur = scan_a_cur + blockDim.x;
	unsigned long long *scan_a_next = scan_b_cur + blockDim.x;
	unsigned long long *scan_b_next = scan_a_next + blockDim.x;
	const int task_id = (int)blockIdx.x;
	const int tid = (int)threadIdx.x;
	const unsigned char *task_input;
	struct fastcdc_gpu_segment_result *task_result;
	int *task_boundaries;
	int n;
	int target_n;
	__shared__ int first_match;
	__shared__ unsigned long long tile_end_fingerprint;

	(void)warp_window;

	if (task_id >= task_count) {
		return;
	}

	task_input = input + input_offsets[task_id];
	n = input_lengths[task_id];
	target_n = target_lengths[task_id];
	task_result = result + task_id;
	task_boundaries = boundaries + (task_id * boundary_stride);

	if (tid == 0) {
		gpu_init_result(task_result);
	}
	__syncthreads();

	if (n <= 0 || boundary_stride <= 0 || target_n <= 0) {
		if (tid == 0) {
			gpu_finalize_result(task_result);
		}
		return;
	}
	if (target_n > n) {
		target_n = n;
	}

	while (task_result->consumed_bytes < n && task_result->boundary_count < boundary_stride) {
		const unsigned char *chunk_input = task_input + task_result->consumed_bytes;
		int chunk_remaining = n - task_result->consumed_bytes;
		unsigned long long fingerprint = 0ULL;
		int position = min_size;
		int end = chunk_remaining;
		int chunk_size = chunk_remaining;
		int cutoff_hit = 0;

		if (chunk_remaining > min_size) {
			if (chunk_remaining > max_size) {
				end = max_size;
			}
			chunk_size = end;

			while (position < end && !cutoff_hit) {
				int active_count = end - position;

				if (active_count > blockDim.x) {
					active_count = blockDim.x;
				}

				gpu_prefix_scan_tile(tid < active_count ? 2ULL : 1ULL,
						tid < active_count ? gear_matrix[chunk_input[position + tid]] : 0ULL,
						active_count,
						fingerprint,
						scan_a_cur,
						scan_b_cur,
						scan_a_next,
						scan_b_next,
						&fingerprint,
						&tile_end_fingerprint);

				if (tid == 0) {
					first_match = 0x7fffffff;
				}
				__syncthreads();
				if (tid < active_count && !(fingerprint & mask)) {
					atomicMin(&first_match, tid);
				}
				__syncthreads();

				if (first_match != 0x7fffffff) {
					chunk_size = position + first_match + 1;
					cutoff_hit = 1;
				} else {
					position += active_count;
					fingerprint = tile_end_fingerprint;
				}
				__syncthreads();
			}
		}

		if (tid == 0) {
			if (chunk_size <= 0 || chunk_size > chunk_remaining) {
				task_result->boundary_count = -1;
			} else {
				task_result->consumed_bytes += chunk_size;
				task_boundaries[task_result->boundary_count] = task_result->consumed_bytes;
				task_result->boundary_count++;
			}
		}
		__syncthreads();

		if (task_result->boundary_count < 0 || task_result->consumed_bytes >= target_n) {
			break;
		}
	}

	if (tid == 0) {
		gpu_mark_segment_result(task_result, target_n, n, boundary_stride);
		gpu_finalize_result(task_result);
	}
}

struct fastcdc_gpu_naive_kernel_result {
	int chunk_size;
};

__global__ void fastcdc_naive_chunk_kernel(const unsigned char *input,
		int n,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		int expect_size,
		unsigned long long mask_s,
		unsigned long long mask_l,
		struct fastcdc_gpu_naive_kernel_result *result) {
	unsigned long long fingerprint = 0;
	int i = 0;
	int mid = expect_size;

	if (blockIdx.x != 0 || threadIdx.x != 0) {
		return;
	}

	result->chunk_size = n;

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
		if (!(fingerprint & mask_s)) {
			result->chunk_size = i;
			return;
		}
		i++;
	}

	while (i < n) {
		fingerprint = (fingerprint << 1) + gear_matrix[input[i]];
		if (!(fingerprint & mask_l)) {
			result->chunk_size = i;
			return;
		}
		i++;
	}

	result->chunk_size = i;
}

__global__ void gear_naive_chunk_kernel(const unsigned char *input,
		int n,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		unsigned long long mask,
		struct fastcdc_gpu_naive_kernel_result *result) {
	unsigned long long fingerprint = 0;
	int i = min_size;

	if (blockIdx.x != 0 || threadIdx.x != 0) {
		return;
	}

	result->chunk_size = n;

	if (n <= min_size) {
		return;
	}
	if (n > max_size) {
		n = max_size;
	}

	while (i < n) {
		fingerprint = (fingerprint << 1) + gear_matrix[input[i]];
		i++;
		if (!(fingerprint & mask)) {
			result->chunk_size = i;
			return;
		}
	}

	result->chunk_size = i;
}

__global__ void jc_naive_chunk_kernel(const unsigned char *input,
		int n,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		unsigned long long mask,
		unsigned long long jump_mask,
		int jump_len,
		struct fastcdc_gpu_naive_kernel_result *result) {
	unsigned long long fingerprint = 0;
	int i = min_size;

	if (blockIdx.x != 0 || threadIdx.x != 0) {
		return;
	}

	result->chunk_size = n;

	if (n <= min_size) {
		return;
	}
	if (n > max_size) {
		n = max_size;
	}

	while (i < n) {
		fingerprint = (fingerprint << 1) + gear_matrix[input[i]];
		i++;
		if (!(fingerprint & jump_mask)) {
			if (!(fingerprint & mask)) {
				result->chunk_size = i;
				return;
			}
			fingerprint = 0;
			i += jump_len;
		}
	}

	result->chunk_size = i < n ? i : n;
}

}
