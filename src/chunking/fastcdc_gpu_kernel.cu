extern "C" {

static __device__ __forceinline__ int gpu_effective_window(int warp_window) {
	if (warp_window <= 0) {
		return 32;
	}
	return warp_window < 32 ? warp_window : 32;
}

struct fastcdc_gpu_segment_result {
	int boundary_count;
	int consumed_bytes;
	unsigned long long fingerprint_updates;
	unsigned long long redundant_checks;
	unsigned long long warp_groups;
	unsigned long long jump_hits;
	unsigned long long jump_bytes_skipped;
	unsigned long long chunk_count;
	unsigned long long cutoff_hits;
	unsigned long long cutoff_lane_sum;
	unsigned long long tail_idle_lane_sum;
	unsigned long long total_chunk_bytes;
	unsigned long long total_checks_per_chunk;
	unsigned long long min_checks_per_chunk;
	unsigned long long max_checks_per_chunk;
};

static __device__ __forceinline__ void gpu_init_result(struct fastcdc_gpu_segment_result *task_result) {
	task_result->boundary_count = 0;
	task_result->consumed_bytes = 0;
	task_result->fingerprint_updates = 0;
	task_result->redundant_checks = 0;
	task_result->warp_groups = 0;
	task_result->jump_hits = 0;
	task_result->jump_bytes_skipped = 0;
	task_result->chunk_count = 0;
	task_result->cutoff_hits = 0;
	task_result->cutoff_lane_sum = 0;
	task_result->tail_idle_lane_sum = 0;
	task_result->total_chunk_bytes = 0;
	task_result->total_checks_per_chunk = 0;
	task_result->min_checks_per_chunk = ~0ULL;
	task_result->max_checks_per_chunk = 0;
}

static __device__ __forceinline__ void gpu_finalize_result(struct fastcdc_gpu_segment_result *task_result) {
	if (task_result->chunk_count == 0) {
		task_result->min_checks_per_chunk = 0;
	}
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

__global__ void fastcdc_chunk_kernel(const unsigned char *input,
		const int *input_offsets,
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
	int effective_warp_window = gpu_effective_window(warp_window);
	__shared__ int first_match;
	__shared__ unsigned long long tile_end_fingerprint;

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
		unsigned long long updates = 0ULL;
		unsigned long long redundant = 0ULL;
		unsigned long long warp_groups = 0ULL;
		unsigned long long fingerprint = 0ULL;
		int mid = expect_size;
		int end = chunk_remaining;
		int chunk_size = chunk_remaining;
		int cutoff_hit = 0;
		int cutoff_lane = -1;
		int tail_idle_lanes = 0;

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

					updates += (unsigned long long)active_count;
					if (first_match != 0x7fffffff) {
						chunk_size = position + first_match;
						cutoff_hit = 1;
						cutoff_lane = effective_warp_window > 0
								? ((position + first_match) % effective_warp_window)
								: 0;
						tail_idle_lanes = active_count - first_match - 1;
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
				warp_groups = (updates + (unsigned long long)effective_warp_window - 1ULL)
						/ (unsigned long long)effective_warp_window;
				task_result->fingerprint_updates += updates;
				task_result->redundant_checks += redundant;
				task_result->warp_groups += warp_groups;
				task_result->chunk_count++;
				task_result->total_chunk_bytes += (unsigned long long)chunk_size;
				task_result->total_checks_per_chunk += updates;
				if (cutoff_hit) {
					task_result->cutoff_hits++;
					task_result->cutoff_lane_sum += (unsigned long long)cutoff_lane;
					task_result->tail_idle_lane_sum += (unsigned long long)tail_idle_lanes;
				}
				if (updates < task_result->min_checks_per_chunk) {
					task_result->min_checks_per_chunk = updates;
				}
				if (updates > task_result->max_checks_per_chunk) {
					task_result->max_checks_per_chunk = updates;
				}
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
		if (task_result->consumed_bytes < target_n) {
			task_result->boundary_count = -1;
		}
		gpu_finalize_result(task_result);
	}
}

__global__ void jc_chunk_kernel(const unsigned char *input,
		const int *input_offsets,
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
	int effective_warp_window = gpu_effective_window(warp_window);
	__shared__ int first_match;
	__shared__ unsigned long long tile_end_fingerprint;
	__shared__ unsigned long long first_match_fingerprint;

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
		unsigned long long updates = 0ULL;
		unsigned long long redundant = 0ULL;
		unsigned long long warp_groups = 0ULL;
		unsigned long long jump_hits = 0ULL;
		unsigned long long jump_bytes_skipped = 0ULL;
		unsigned long long fingerprint = 0ULL;
		int position = min_size;
		int end = chunk_remaining;
		int chunk_size = chunk_remaining;
		int cutoff_hit = 0;
		int cutoff_lane = -1;
		int tail_idle_lanes = 0;

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

				updates += (unsigned long long)active_count;
				if (first_match != 0x7fffffff) {
					if (tid == first_match) {
						first_match_fingerprint = fingerprint;
					}
					__syncthreads();
					if (!(first_match_fingerprint & mask)) {
						chunk_size = position + first_match + 1;
						cutoff_hit = 1;
						cutoff_lane = effective_warp_window > 0
								? ((position + first_match) % effective_warp_window)
								: 0;
						tail_idle_lanes = active_count - first_match - 1;
					} else {
						jump_hits++;
						jump_bytes_skipped += (unsigned long long)jump_len;
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
				warp_groups = (updates + (unsigned long long)effective_warp_window - 1ULL)
						/ (unsigned long long)effective_warp_window;
				task_result->fingerprint_updates += updates;
				task_result->redundant_checks += redundant;
				task_result->warp_groups += warp_groups;
				task_result->jump_hits += jump_hits;
				task_result->jump_bytes_skipped += jump_bytes_skipped;
				task_result->chunk_count++;
				task_result->total_chunk_bytes += (unsigned long long)chunk_size;
				task_result->total_checks_per_chunk += updates;
				if (cutoff_hit) {
					task_result->cutoff_hits++;
					task_result->cutoff_lane_sum += (unsigned long long)cutoff_lane;
					task_result->tail_idle_lane_sum += (unsigned long long)tail_idle_lanes;
				}
				if (updates < task_result->min_checks_per_chunk) {
					task_result->min_checks_per_chunk = updates;
				}
				if (updates > task_result->max_checks_per_chunk) {
					task_result->max_checks_per_chunk = updates;
				}
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
		if (task_result->consumed_bytes < target_n) {
			task_result->boundary_count = -1;
		}
		gpu_finalize_result(task_result);
	}
}

}