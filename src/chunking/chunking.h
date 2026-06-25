/*
 * Public chunking interfaces grouped by algorithm family.
 */

#ifndef CHUNK_H_
#define CHUNK_H_

#include "../destor.h"
#include <string.h>

#ifndef CHUNK_EXPERIMENT_STATS
#define CHUNK_EXPERIMENT_STATS 0
#endif

struct chunk_experiment_stats {
	uint64_t fingerprint_updates;
	uint64_t chunk_count;
	uint64_t cutoff_hits;
	uint64_t jump_hits;
	uint64_t jump_bytes_skipped;
	uint64_t redundant_checks;
	uint64_t simulated_warp_groups;
	uint64_t cutoff_lane_sum;
	uint64_t tail_idle_lane_sum;
	uint64_t total_chunk_bytes;
	uint64_t total_checks_per_chunk;
	uint64_t min_checks_per_chunk;
	uint64_t max_checks_per_chunk;
};

#if CHUNK_EXPERIMENT_STATS
void chunk_experiment_reset_stats(void);
void chunk_experiment_snapshot(struct chunk_experiment_stats *stats);
void chunk_experiment_note_fingerprint_update(void);
void chunk_experiment_note_jump(int jump_bytes);
void chunk_experiment_note_redundancy(int redundant_checks, int warp_group_count);
void chunk_experiment_note_chunk_complete(int chunk_size, int cutoff_hit);
#else
#define chunk_experiment_reset_stats() ((void)0)
#define chunk_experiment_snapshot(stats) \
	do { \
		if (stats) { \
			memset((stats), 0, sizeof(*(stats))); \
		} \
	} while (0)
#define chunk_experiment_note_fingerprint_update() ((void)0)
#define chunk_experiment_note_jump(jump_bytes) ((void)(jump_bytes))
#define chunk_experiment_note_redundancy(redundant_checks, warp_group_count) \
	((void)(redundant_checks), (void)(warp_group_count))
#define chunk_experiment_note_chunk_complete(chunk_size, cutoff_hit) \
	((void)(chunk_size), (void)(cutoff_hit))
#endif

/* Rabin family */
void windows_reset();
void chunkAlg_init();
int rabin_chunk_data(unsigned char *p, int n);
int normalized_rabin_chunk_data(unsigned char *p, int n);
int tttd_chunk_data(unsigned char *p, int n);
void rabinJump_init(int chunkSize);
int rabinjump_chunk_data(unsigned char *p, int n);

/* AE and SC */
void ae_init();
int ae_chunk_data(unsigned char *p, int n);
void sc_init();
int sc_chunk_data(unsigned char *p, int n);

/* FastCDC */
void fastcdc_init();
int fastcdc_chunk_data(unsigned char *p, int n);

/* FastCDC GPU fallback wrapper */
int fastcdc_gpu_init();
int fastcdc_gpu_is_ready();
void fastcdc_gpu_close();
int fastcdc_gpu_chunk_data(unsigned char *p, int n);
int fastcdc_gpu_chunk_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes);
int fastcdc_gpu_chunk_segments_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes);
int fastcdc_gpu_segment_bytes(void);
int fastcdc_gpu_segment_boundary_limit(int segment_bytes);
void fastcdc_gpu_set_threads_per_block(int threads);
void fastcdc_gpu_set_pipeline_tasks(int tasks);
void fastcdc_gpu_set_naive_mode(int enabled);
int fastcdc_gpu_naive_probe_max_workers(int file_count);
void fastcdc_gpu_reset_batch_timing(void);
double fastcdc_gpu_get_batch_compute_ms(void);

/* JC GPU fallback wrapper */
int jc_gpu_init();
void jc_gpu_close();
int jc_gpu_chunk_data(unsigned char *p, int n);
int jc_gpu_chunk_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes);
int jc_gpu_chunk_segments_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes);

/* Gear GPU */
int gear_gpu_init();
void gear_gpu_close();
int gear_gpu_chunk_data(unsigned char *p, int n);
int gear_gpu_chunk_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes);
int gear_gpu_chunk_segments_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes);
int gear_gpu_naive_chunk_data(unsigned char *p, int n);
int jc_gpu_naive_chunk_data(unsigned char *p, int n);
void fastcdc_gpu_set_naive_algorithm(int algorithm);

/* Gear family */
void gear_init();
int gear_chunk_data(unsigned char *p, int n);
int TTTD_gear_chunk_data(unsigned char *p, int n);

#if SENTEST
void gearjump_init(int i);
#else
void gearjump_init();
#endif	//SENTEST
int gearjump_chunk_data(unsigned char *p, int n);
int gearjumpTTTD_chunk_data(unsigned char *p, int n);

void normalized_gearjump_init(int mto);
int normalized_gearjump_chunk_data(unsigned char *p, int n);

/* Leap */
void leap_init(int chunkSize, int parIdx);
int leap_chunk_data(unsigned char *p, int n);

#endif
