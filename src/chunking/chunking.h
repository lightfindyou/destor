/*
 * Public chunking interfaces grouped by algorithm family.
 */

#ifndef CHUNK_H_
#define CHUNK_H_

#include "../destor.h"

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

void chunk_experiment_reset_stats();
void chunk_experiment_snapshot(struct chunk_experiment_stats *stats);
void chunk_experiment_note_fingerprint_update();
void chunk_experiment_note_jump(int jump_bytes);
void chunk_experiment_note_redundancy(int redundant_checks, int warp_group_count);
void chunk_experiment_note_chunk_complete(int chunk_size, int cutoff_hit);

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
