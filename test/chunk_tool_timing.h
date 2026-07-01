#ifndef CHUNK_TOOL_TIMING_H
#define CHUNK_TOOL_TIMING_H

/*
 * Unified timing for chunkingTool / experiment scripts.
 *
 * All paths exclude read_input_file / disk I/O. GPU paths exclude H2D/D2H.
 * elapsed_ms and actual_elapsed_ms are always the same value.
 *
 *   CPU (serial or parallel): merged compute wall clock from first chunk start
 *                             to last chunk end (parallel overlaps once).
 *   GPU Ours (batch):         accumulated CUDA kernel time (cuEventElapsedTime).
 *   GPU Naive:                merged kernel launch+sync wall across workers.
 *
 * Throughput = total_bytes / elapsed_ms for all experiment configs.
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
double chunk_tool_compute_read_ms(chunk_tool_compute_kind kind);

/* Write identical elapsed_ms and actual_elapsed_ms for the given backend. */
void chunk_tool_timing_assign(double *elapsed_ms,
		double *actual_elapsed_ms,
		double fallback_ms,
		chunk_tool_compute_kind kind,
		double cpu_wall_ms);

#endif
