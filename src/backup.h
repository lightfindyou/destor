/*
 * backup.h
 *
 *  Created on: Dec 4, 2013
 *      Author: fumin
 */

#ifndef BACKUP_H_
#define BACKUP_H_

#include "destor.h"
#include "utils/sync_queue.h"

/*
 * CHUNK_FILE_START NORMAL_CHUNK... CHUNK_FILE_END
 */
void start_read_phase();
void stop_read_phase();

/*
 * Input: Raw data blocks
 * Output: Chunks
 */
void start_chunk_phase();
void stop_chunk_phase();
/* Input: Chunks
 * Output: Hashed Chunks.
 */
void start_hash_phase();
void stop_hash_phase();

void start_read_trace_phase();
void stop_read_trace_phase();

/*
 * Duplicate chunks are marked CHUNK_DUPLICATE
 */
void start_dedup_phase();
void stop_dedup_phase();
/*
 * Fragmented chunks are marked CHUNK_SPARSE, CHUNK_OUT_OF_ORDER or CHUNK_NOT_IN_CACHE.
 */
void start_rewrite_phase();
void stop_rewrite_phase();
/*
 * Determine which chunks are required to be written according to their flags.
 * All unique/rewritten chunks aggregate into containers.
 *
 * output: containers
 */
void start_filter_phase();
void stop_filter_phase();
/*
 * Write containers.
 */
void start_append_phase();
void stop_append_phase();

#define READQUESIZE 10
/* Output of read phase. */
extern SyncQueue* read_queue;
#define CHUNKQUESIZE 1000
/* Output of chunk phase. */
extern SyncQueue* chunk_queue;
#define HASHQUESIZE 1000
/* Output of hash phase. */
extern SyncQueue* hash_queue;
/* Output of trace phase. */
extern SyncQueue* trace_queue;
#define DEDUPQUESIZE 1000
/* Output of dedup phase */
extern SyncQueue* dedup_queue;
#define FEAQUESIZE 1000
/* Output of feature phase */
extern SyncQueue* feature_queue;
#define SIMIQUESIZE 1000
/* Output of simi phase */
extern SyncQueue* simi_queue;
#define XDElTAQUESIZE 1000
/* Output of xdelta phase */
extern SyncQueue* xdelta_queue;
/* Output of rewrite phase. */
extern SyncQueue* rewrite_queue;

extern GHashTable* fp_tab;
extern pthread_mutex_t fp_tab_mutex;

#endif /* BACKUP_H_ */
