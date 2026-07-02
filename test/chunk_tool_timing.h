#ifndef CHUNK_TOOL_TIMING_H
#define CHUNK_TOOL_TIMING_H

/*
 * Unified timing for chunkingTool / experiment scripts.
 *
 * Disk read (read_input_file) is excluded from both metrics.
 *
 *   elapsed_ms (total / e2e):  compute-phase wall clock including GPU H2D/D2H.
 *   actual_elapsed_ms (kernel): pure chunking compute only.
 *
 *   CPU: elapsed == actual (no PCIe transfers).
 *   GPU Ours (batch): elapsed = merged batch compute wall; actual = Σ CUDA kernel events.
 *   GPU Naive: elapsed = per-run compute wall (H2D+kernel+D2H); actual = kernel launch wall.
 *
 * Throughput(total)  = bytes / elapsed_ms
 * Throughput(actual) = bytes / actual_elapsed_ms  (primary paper metric for GPU)
 */

#include <pthread.h>
#include <time.h>

typedef struct chunk_tool_wall_clock {
	struct timespec start;
	struct timespec end;
	int running;
} chunk_tool_wall_clock;

typedef struct chunk_tool_parallel_wall {
	pthread_mutex_t lock;
	int active;
	struct timespec start;
	struct timespec end;
} chunk_tool_parallel_wall;

typedef enum chunk_tool_compute_kind {
	CHUNK_TOOL_COMPUTE_CPU = 0,
	CHUNK_TOOL_COMPUTE_GPU_BATCH,
	CHUNK_TOOL_COMPUTE_GPU_NAIVE,
} chunk_tool_compute_kind;

double chunk_tool_timespec_diff_ms(const struct timespec *start, const struct timespec *end);

void chunk_tool_wall_clock_start(chunk_tool_wall_clock *wc);
void chunk_tool_wall_clock_stop(chunk_tool_wall_clock *wc);
double chunk_tool_wall_clock_ms(const chunk_tool_wall_clock *wc);

void chunk_tool_parallel_wall_init(chunk_tool_parallel_wall *pw);
void chunk_tool_parallel_wall_destroy(chunk_tool_parallel_wall *pw);
void chunk_tool_parallel_wall_merge(chunk_tool_parallel_wall *pw,
		const struct timespec *start,
		const struct timespec *end);
double chunk_tool_parallel_wall_ms(const chunk_tool_parallel_wall *pw);

chunk_tool_compute_kind chunk_tool_compute_kind_for(int uses_gpu, int gpu_naive);
void chunk_tool_compute_reset(chunk_tool_compute_kind kind);
double chunk_tool_compute_kernel_ms(chunk_tool_compute_kind kind);

/* e2e_ms: compute-phase wall (incl. GPU transfers). kernel_ms: optional CPU wall fallback. */
void chunk_tool_timing_assign(double *elapsed_ms,
		double *actual_elapsed_ms,
		double e2e_ms,
		chunk_tool_compute_kind kind,
		double kernel_ms);

#endif
