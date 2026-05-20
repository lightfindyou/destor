extern "C" {

static __device__ __forceinline__ int gpu_effective_window(int warp_window) {
	if (warp_window <= 0) {
		return 32;
	}
	return warp_window < 32 ? warp_window : 32;
}

struct warp_prefix_transform {
	int count;
	unsigned long long add;
};

static __device__ __forceinline__ struct warp_prefix_transform warp_prefix_scan(
		unsigned long long value,
		int active) {
	struct warp_prefix_transform prefix;
	int lane = (int)(threadIdx.x & 31);

	prefix.count = active ? 1 : 0;
	prefix.add = active ? value : 0ULL;

	for (int offset = 1; offset < 32; offset <<= 1) {
		int prev_count = __shfl_up_sync(0xffffffffu, prefix.count, offset);
		unsigned long long prev_add = __shfl_up_sync(0xffffffffu, prefix.add, offset);
		if (lane >= offset) {
			int local_count = prefix.count;
			prefix.count = prev_count + local_count;
			prefix.add = (prev_add << local_count) + prefix.add;
		}
	}

	return prefix;
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
		int n,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		int expect_size,
		unsigned long long mask_s,
		unsigned long long mask_l,
		int warp_window,
		struct fastcdc_gpu_kernel_result *result) {
	const int lane = (int)(threadIdx.x & 31);
	unsigned long long fingerprint = 0;
	unsigned long long updates = 0;
	unsigned long long redundant = 0;
	unsigned long long warp_groups = 0;
	int i = 0;
	int mid = expect_size;
	int end = n;
	int chunk_size = n;
	int cutoff_hit = 0;
	int cutoff_lane = -1;
	int tail_idle_lanes = 0;
	int effective_warp_window = gpu_effective_window(warp_window);

	if (blockIdx.x != 0 || threadIdx.x >= 32) {
		return;
	}

	if (lane == 0) {
		result->chunk_size = n;
		result->cutoff_hit = 0;
		result->cutoff_lane = -1;
		result->tail_idle_lanes = 0;
		result->fingerprint_updates = 0;
		result->redundant_checks = 0;
		result->warp_groups = 0;
		result->jump_hits = 0;
		result->jump_bytes_skipped = 0;
	}

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
			int width = phase_end - i;
			int active;
			unsigned long long gear_value;
			struct warp_prefix_transform prefix;
			unsigned long long lane_fingerprint;
			unsigned int match_mask;

			if (width > effective_warp_window) {
				width = effective_warp_window;
			}
			active = lane < width;
			gear_value = active ? gear_matrix[input[i + lane]] : 0ULL;
			prefix = warp_prefix_scan(gear_value, active);
			lane_fingerprint = active ? ((fingerprint << prefix.count) + prefix.add) : 0ULL;
			match_mask = __ballot_sync(0xffffffffu,
					active && !(lane_fingerprint & phase_mask));
			warp_groups++;

			if (match_mask != 0U) {
				int first_lane = __ffs((int)match_mask) - 1;

				updates += (unsigned long long)(first_lane + 1);
				chunk_size = i + first_lane;
				cutoff_hit = 1;
				cutoff_lane = chunk_size % effective_warp_window;
				tail_idle_lanes = width - first_lane - 1;
				if (tail_idle_lanes < 0) {
					tail_idle_lanes = 0;
				}
				redundant += (unsigned long long)tail_idle_lanes;
			} else {
				updates += (unsigned long long)width;
				fingerprint = __shfl_sync(0xffffffffu, lane_fingerprint, width - 1);
				i += width;
			}
		}
	}

	if (!cutoff_hit) {
		chunk_size = i;
	}

	if (lane == 0) {
		result->chunk_size = chunk_size;
		result->cutoff_hit = cutoff_hit;
		result->cutoff_lane = cutoff_lane;
		result->tail_idle_lanes = tail_idle_lanes;
		result->fingerprint_updates = updates;
		result->redundant_checks = redundant;
		result->warp_groups = warp_groups;
	}

	__syncwarp();
}

__global__ void jc_chunk_kernel(const unsigned char *input,
		int n,
		const unsigned long long *gear_matrix,
		int min_size,
		int max_size,
		unsigned long long mask,
		unsigned long long jump_mask,
		int jump_len,
		int warp_window,
		struct fastcdc_gpu_kernel_result *result) {
	const int lane = (int)(threadIdx.x & 31);
	unsigned long long fingerprint = 0;
	unsigned long long updates = 0;
	unsigned long long redundant = 0;
	unsigned long long warp_groups = 0;
	unsigned long long jump_hits = 0;
	unsigned long long jump_bytes_skipped = 0;
	int i = min_size;
	int chunk_size = n;
	int cutoff_hit = 0;
	int cutoff_lane = -1;
	int tail_idle_lanes = 0;
	int effective_warp_window = gpu_effective_window(warp_window);
	int end = n;

	if (blockIdx.x != 0 || threadIdx.x >= 32) {
		return;
	}

	if (lane == 0) {
		result->chunk_size = n;
		result->cutoff_hit = 0;
		result->cutoff_lane = -1;
		result->tail_idle_lanes = 0;
		result->fingerprint_updates = 0;
		result->redundant_checks = 0;
		result->warp_groups = 0;
		result->jump_hits = 0;
		result->jump_bytes_skipped = 0;
	}

	if (n <= min_size) {
		return;
	}
	if (n > max_size) {
		end = max_size;
	}
	chunk_size = end;

	while (i < end && !cutoff_hit) {
		int width = end - i;
		int active;
		unsigned long long gear_value;
		struct warp_prefix_transform prefix;
		unsigned long long lane_fingerprint;
		unsigned int jump_hit_mask;

		if (width > effective_warp_window) {
			width = effective_warp_window;
		}
		active = lane < width;
		gear_value = active ? gear_matrix[input[i + lane]] : 0ULL;
		prefix = warp_prefix_scan(gear_value, active);
		lane_fingerprint = active ? ((fingerprint << prefix.count) + prefix.add) : 0ULL;
		jump_hit_mask = __ballot_sync(0xffffffffu,
				active && !(lane_fingerprint & jump_mask));
		warp_groups++;

		if (jump_hit_mask != 0U) {
			int first_lane = __ffs((int)jump_hit_mask) - 1;
			int examined_index = i + first_lane;
			unsigned long long first_fingerprint = __shfl_sync(0xffffffffu,
					lane_fingerprint,
					first_lane);

			updates += (unsigned long long)(first_lane + 1);
			tail_idle_lanes = width - first_lane - 1;
			if (tail_idle_lanes < 0) {
				tail_idle_lanes = 0;
			}
			redundant += (unsigned long long)tail_idle_lanes;

			if (!(first_fingerprint & mask)) {
				chunk_size = examined_index + 1;
				cutoff_hit = 1;
				cutoff_lane = examined_index % effective_warp_window;
			} else {
				fingerprint = 0;
				jump_hits++;
				jump_bytes_skipped += (unsigned long long)jump_len;
				i = examined_index + 1 + jump_len;
			}
		} else {
			updates += (unsigned long long)width;
			fingerprint = __shfl_sync(0xffffffffu, lane_fingerprint, width - 1);
			i += width;
		}
	}

	if (!cutoff_hit) {
		chunk_size = i < end ? i : end;
	}

	if (lane == 0) {
		result->chunk_size = chunk_size;
		result->cutoff_hit = cutoff_hit;
		result->cutoff_lane = cutoff_lane;
		result->tail_idle_lanes = tail_idle_lanes;
		result->fingerprint_updates = updates;
		result->redundant_checks = redundant;
		result->warp_groups = warp_groups;
		result->jump_hits = jump_hits;
		result->jump_bytes_skipped = jump_bytes_skipped;
	}

	__syncwarp();
}

}