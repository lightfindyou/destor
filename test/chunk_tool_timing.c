#include "chunk_tool_timing.h"

#include "../src/chunking/chunking.h"

#include <stddef.h>

double chunk_tool_timespec_diff_ms(const struct timespec *start, const struct timespec *end) {
	if (!start || !end) {
		return 0.0;
	}
	return (double)(end->tv_sec - start->tv_sec) * 1000.0
			+ (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
}

void chunk_tool_wall_clock_start(chunk_tool_wall_clock *wc) {
	if (!wc) {
		return;
	}
	clock_gettime(CLOCK_MONOTONIC, &wc->start);
	wc->running = 1;
}

void chunk_tool_wall_clock_stop(chunk_tool_wall_clock *wc) {
	if (!wc || !wc->running) {
		return;
	}
	clock_gettime(CLOCK_MONOTONIC, &wc->end);
	wc->running = 0;
}

double chunk_tool_wall_clock_ms(const chunk_tool_wall_clock *wc) {
	if (!wc || !wc->running) {
		if (wc && !wc->running) {
			return chunk_tool_timespec_diff_ms(&wc->start, &wc->end);
		}
		return 0.0;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return chunk_tool_timespec_diff_ms(&wc->start, &now);
}

void chunk_tool_parallel_wall_init(chunk_tool_parallel_wall *pw) {
	if (!pw) {
		return;
	}
	pthread_mutex_init(&pw->lock, NULL);
	pw->active = 0;
}

void chunk_tool_parallel_wall_destroy(chunk_tool_parallel_wall *pw) {
	if (!pw) {
		return;
	}
	pthread_mutex_destroy(&pw->lock);
}

void chunk_tool_parallel_wall_merge(chunk_tool_parallel_wall *pw,
		const struct timespec *start,
		const struct timespec *end) {
	if (!pw || !start || !end) {
		return;
	}
	pthread_mutex_lock(&pw->lock);
	if (!pw->active) {
		pw->start = *start;
		pw->end = *end;
		pw->active = 1;
	} else {
		if (start->tv_sec < pw->start.tv_sec
				|| (start->tv_sec == pw->start.tv_sec && start->tv_nsec < pw->start.tv_nsec)) {
			pw->start = *start;
		}
		if (end->tv_sec > pw->end.tv_sec
				|| (end->tv_sec == pw->end.tv_sec && end->tv_nsec > pw->end.tv_nsec)) {
			pw->end = *end;
		}
	}
	pthread_mutex_unlock(&pw->lock);
}

double chunk_tool_parallel_wall_ms(const chunk_tool_parallel_wall *pw) {
	if (!pw || !pw->active) {
		return 0.0;
	}
	return chunk_tool_timespec_diff_ms(&pw->start, &pw->end);
}

chunk_tool_compute_kind chunk_tool_compute_kind_for(int uses_gpu, int gpu_naive) {
	if (!uses_gpu) {
		return CHUNK_TOOL_COMPUTE_CPU;
	}
	if (gpu_naive) {
		return CHUNK_TOOL_COMPUTE_GPU_NAIVE;
	}
	return CHUNK_TOOL_COMPUTE_GPU_BATCH;
}

void chunk_tool_compute_reset(chunk_tool_compute_kind kind) {
	switch (kind) {
	case CHUNK_TOOL_COMPUTE_GPU_BATCH:
		fastcdc_gpu_reset_batch_timing();
		break;
	case CHUNK_TOOL_COMPUTE_GPU_NAIVE:
		fastcdc_gpu_naive_reset_kernel_wall();
		break;
	default:
		break;
	}
}

double chunk_tool_compute_kernel_ms(chunk_tool_compute_kind kind) {
	switch (kind) {
	case CHUNK_TOOL_COMPUTE_GPU_BATCH:
		return fastcdc_gpu_get_batch_compute_ms();
	case CHUNK_TOOL_COMPUTE_GPU_NAIVE:
		return fastcdc_gpu_naive_get_kernel_wall_ms();
	default:
		return 0.0;
	}
}

void chunk_tool_timing_assign(double *elapsed_ms,
		double *actual_elapsed_ms,
		double e2e_ms,
		chunk_tool_compute_kind kind,
		double kernel_ms) {
	double elapsed;
	double actual;

	if (!elapsed_ms || !actual_elapsed_ms) {
		return;
	}

	switch (kind) {
	case CHUNK_TOOL_COMPUTE_GPU_BATCH:
		actual = chunk_tool_compute_kernel_ms(kind);
		if (actual <= 0.0 && kernel_ms > 0.0) {
			actual = kernel_ms;
		}
		elapsed = e2e_ms;
		if (elapsed <= 0.0) {
			elapsed = fastcdc_gpu_get_batch_e2e_ms();
		}
		if (elapsed <= 0.0) {
			elapsed = actual;
		}
		if (actual <= 0.0) {
			actual = elapsed;
		}
		break;
	case CHUNK_TOOL_COMPUTE_GPU_NAIVE:
		actual = chunk_tool_compute_kernel_ms(kind);
		if (actual <= 0.0 && kernel_ms > 0.0) {
			actual = kernel_ms;
		}
		elapsed = e2e_ms;
		if (elapsed <= 0.0) {
			elapsed = actual;
		}
		if (actual <= 0.0) {
			actual = elapsed;
		}
		break;
	default:
		if (kernel_ms > 0.0) {
			elapsed = kernel_ms;
		} else {
			elapsed = e2e_ms;
		}
		actual = elapsed;
		break;
	}

	*elapsed_ms = elapsed;
	*actual_elapsed_ms = actual;
}
