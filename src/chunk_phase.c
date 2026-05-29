#include <math.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include "destor.h"
#include "jcr.h"
#include "chunking/chunking.h"
#include "backup.h"
#include "storage/containerstore.h"

static pthread_t chunk_t;
static int64_t chunk_num;
static void* chunk_thread(void *arg);

static int (*chunking)(unsigned char* buf, int size);
static int (*chunking_batch)(unsigned char **buffers, const int *sizes, int task_count, int *chunk_sizes) = NULL;
static int (*chunking_segment_batch)(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes) = NULL;
static int chunking_uses_gpu = 0;
static void (*chunking_gpu_close_fn)() = NULL;

struct chunk_file_state {
	unsigned char *leftbuf;
	int leftlen;
	int leftoff;
	struct chunk *file_start;
	struct chunk *file_end;
	int start_emitted;
	int *pending_chunk_sizes;
	int pending_chunk_capacity;
	int pending_chunk_count;
	int pending_chunk_index;
};

static inline int fixed_chunk_data(unsigned char* buf, int size){
	return destor.chunk_avg_size > size ? size : destor.chunk_avg_size;
}

static int chunk_gpu_batch_task_limit() {
	int bytes_per_state = DEFAULT_BLOCK_SIZE + destor.chunk_max_size;
	int task_limit;

	if (bytes_per_state <= 0) {
		return 1;
	}
	task_limit = destor.chunk_gpu_batch_size / bytes_per_state;
	if (task_limit <= 0) {
		task_limit = 1;
	}
	if (task_limit > 1024) {
		task_limit = 1024;
	}
	return task_limit;
}

static int chunk_gpu_segment_bytes(void) {
	return fastcdc_gpu_segment_bytes() + destor.chunk_max_size;
}

static int chunk_gpu_boundary_stride(void) {
	return fastcdc_gpu_segment_boundary_limit(chunk_gpu_segment_bytes());
}

static int chunk_file_state_has_pending(const struct chunk_file_state *state) {
	return state && state->pending_chunk_index < state->pending_chunk_count;
}

static void chunk_file_state_reset(struct chunk_file_state *state) {
	if (!state) {
		return;
	}
	state->leftbuf = NULL;
	state->leftlen = 0;
	state->leftoff = 0;
	state->file_start = NULL;
	state->file_end = NULL;
	state->start_emitted = 0;
	state->pending_chunk_sizes = NULL;
	state->pending_chunk_capacity = 0;
	state->pending_chunk_count = 0;
	state->pending_chunk_index = 0;
}

static void chunk_file_state_destroy(struct chunk_file_state *state) {
	if (!state) {
		return;
	}
	if (state->leftbuf) {
		free(state->leftbuf);
		state->leftbuf = NULL;
	}
	if (state->pending_chunk_sizes) {
		free(state->pending_chunk_sizes);
		state->pending_chunk_sizes = NULL;
	}
	if (state->file_start) {
		free_chunk(state->file_start);
		state->file_start = NULL;
	}
	if (state->file_end) {
		free_chunk(state->file_end);
		state->file_end = NULL;
	}
	chunk_file_state_reset(state);
}

static int chunk_file_state_fill(struct chunk_file_state *state) {
	struct chunk *c;
	int target_bytes = chunking_segment_batch ? chunk_gpu_segment_bytes() : destor.chunk_max_size;

	if (!state || !state->leftbuf || state->file_end) {
		return 0;
	}
	if (target_bytes < destor.chunk_max_size) {
		target_bytes = destor.chunk_max_size;
	}
	while (state->leftlen < target_bytes && !state->file_end) {
		c = sync_queue_pop(read_queue);
		if (c == NULL) {
			return -1;
		}
		if (CHECK_CHUNK(c, CHUNK_FILE_END)) {
			state->file_end = c;
			break;
		}
		memmove(state->leftbuf, state->leftbuf + state->leftoff, state->leftlen);
		state->leftoff = 0;
		memcpy(state->leftbuf + state->leftlen, c->data, c->size);
		state->leftlen += c->size;
		free_chunk(c);
	}
	return 0;
}

static int chunk_file_state_init(struct chunk_file_state *state) {
	struct chunk *c;

	chunk_file_state_reset(state);
	c = sync_queue_pop(read_queue);
	if (c == NULL) {
		return 0;
	}
	assert(CHECK_CHUNK(c, CHUNK_FILE_START));
	state->leftbuf = malloc(DEFAULT_BLOCK_SIZE + destor.chunk_max_size);
	if (!state->leftbuf) {
		free_chunk(c);
		return -1;
	}
	state->pending_chunk_capacity = chunk_gpu_boundary_stride();
	state->pending_chunk_sizes = malloc(sizeof(int) * (size_t)state->pending_chunk_capacity);
	if (!state->pending_chunk_sizes) {
		free(state->leftbuf);
		state->leftbuf = NULL;
		free_chunk(c);
		return -1;
	}
	state->file_start = c;
	if (chunk_file_state_fill(state) != 0) {
		chunk_file_state_destroy(state);
		return -1;
	}
	return 1;
}

static void chunk_emit_data_chunk(struct chunk_file_state *state,
		unsigned char *zeros) {
	int chunk_size;
	struct chunk *nc;

	chunk_size = state->pending_chunk_sizes[state->pending_chunk_index++];
	nc = new_chunk(chunk_size);
	memcpy(nc->data, state->leftbuf + state->leftoff, chunk_size);
	state->leftlen -= chunk_size;
	state->leftoff += chunk_size;
	if (state->pending_chunk_index >= state->pending_chunk_count) {
		state->pending_chunk_index = 0;
		state->pending_chunk_count = 0;
	}

	if (memcmp(zeros, nc->data, chunk_size) == 0) {
		VERBOSE("Chunk phase: %ldth chunk  of %d zero bytes",
				chunk_num++, chunk_size);
		jcr.zero_chunk_num++;
		jcr.zero_chunk_size += chunk_size;
	} else {
		VERBOSE("Chunk phase: %ldth chunk of %d bytes", chunk_num++, chunk_size);
	}

	sync_queue_push(chunk_queue, nc);
}

static void chunk_file_state_consume(struct chunk_file_state *state,
		unsigned char *zeros) {
	if (!state->start_emitted) {
		sync_queue_push(chunk_queue, state->file_start);
		state->file_start = NULL;
		state->start_emitted = 1;
	}
	if (chunk_file_state_has_pending(state)) {
		chunk_emit_data_chunk(state, zeros);
		if (!chunk_file_state_has_pending(state) && state->leftlen > 0) {
			memmove(state->leftbuf, state->leftbuf + state->leftoff, state->leftlen);
			state->leftoff = 0;
		}
		if (!chunk_file_state_has_pending(state) && !state->file_end) {
			chunk_file_state_fill(state);
		}
	}
}

static int chunk_file_state_is_done(const struct chunk_file_state *state) {
	return state && state->leftlen == 0 && state->file_end && !chunk_file_state_has_pending(state);
}

static void chunk_file_state_emit_end(struct chunk_file_state *state) {
	if (state && state->file_end) {
		sync_queue_push(chunk_queue, state->file_end);
		state->file_end = NULL;
	}
}

static void chunk_compact_active_states(struct chunk_file_state *states,
		int *active_count,
		int remove_index) {
	int i;

	chunk_file_state_destroy(states + remove_index);
	for (i = remove_index; i + 1 < *active_count; i++) {
		states[i] = states[i + 1];
		chunk_file_state_reset(states + i + 1);
	}
	(*active_count)--;
}

static void* chunk_thread_batch_gpu(void *arg) {
	int max_active = chunk_gpu_batch_task_limit();
	struct chunk_file_state *states = calloc((size_t)max_active, sizeof(struct chunk_file_state));
	unsigned char *zeros = malloc(destor.chunk_max_size);
	unsigned char **buffers = NULL;
	int *sizes = NULL;
	int *chunk_sizes = NULL;
	int *boundary_counts = NULL;
	int *state_index = NULL;
	int boundary_stride = chunk_gpu_boundary_stride();
	int active_count = 0;
	int read_done = 0;
	int batch_notice_emitted = 0;
	int i;

	(void)arg;
	buffers = malloc(sizeof(unsigned char *) * (size_t)max_active);
	sizes = malloc(sizeof(int) * (size_t)max_active);
	chunk_sizes = malloc(sizeof(int) * (size_t)max_active * (size_t)boundary_stride);
	boundary_counts = malloc(sizeof(int) * (size_t)max_active);
	state_index = malloc(sizeof(int) * (size_t)max_active);
	if (!states || !zeros || !buffers || !sizes || !chunk_sizes || !boundary_counts || !state_index) {
		free(states);
		free(zeros);
		free(buffers);
		free(sizes);
		free(chunk_sizes);
		free(boundary_counts);
		free(state_index);
		return chunk_thread(NULL);
	}
	bzero(zeros, destor.chunk_max_size);

	while (1) {
		while (!read_done && active_count < max_active) {
			int init_rc = chunk_file_state_init(states + active_count);
			if (init_rc > 0) {
				active_count++;
				continue;
			}
			if (init_rc == 0) {
				read_done = 1;
				break;
			}
			WARNING("Chunk phase: failed to initialize GPU batch file state, fallback to sequential GPU mode");
			read_done = 1;
			break;
		}

		if (active_count == 0) {
			break;
		}

		{
			int task_count = 0;

			for (i = 0; i < active_count; i++) {
				if (!chunk_file_state_has_pending(states + i) && states[i].leftlen > 0) {
					buffers[task_count] = states[i].leftbuf + states[i].leftoff;
					sizes[task_count] = states[i].leftlen;
					state_index[task_count] = i;
					task_count++;
				}
			}

			if (task_count > 0) {
				TIMER_DECLARE(1);
				if (!batch_notice_emitted && task_count > 1) {
					NOTICE("Chunk phase: GPU batch scheduler launched %d concurrent tasks", task_count);
					batch_notice_emitted = 1;
				}
				TIMER_BEGIN(1);
				if (chunking_segment_batch) {
					chunking_segment_batch(buffers,
							sizes,
							task_count,
							boundary_stride,
							boundary_counts,
							chunk_sizes);
				} else if (chunking_batch) {
					chunking_batch(buffers, sizes, task_count, chunk_sizes);
				} else {
					for (i = 0; i < task_count; i++) {
						chunk_sizes[i] = chunking(buffers[i], sizes[i]);
						boundary_counts[i] = 1;
					}
				}
				TIMER_END(1, jcr.chunk_time);
				for (i = 0; i < task_count; i++) {
					struct chunk_file_state *state = states + state_index[i];
					int count = chunking_segment_batch ? boundary_counts[i] : 1;

					if (count <= 0) {
						count = 1;
						chunk_sizes[i * boundary_stride] = chunking(buffers[i], sizes[i]);
					}
					if (count > state->pending_chunk_capacity) {
						count = state->pending_chunk_capacity;
					}
					memcpy(state->pending_chunk_sizes,
							chunk_sizes + (i * boundary_stride),
							sizeof(int) * (size_t)count);
					state->pending_chunk_count = count;
					state->pending_chunk_index = 0;
				}
			}
		}

		while (active_count > 0) {
			if (chunk_file_state_is_done(states)) {
				chunk_file_state_emit_end(states);
				chunk_compact_active_states(states, &active_count, 0);
				continue;
			}
			if (!chunk_file_state_has_pending(states)) {
				break;
			}
			chunk_file_state_consume(states, zeros);
		}
	}

	for (i = 0; i < active_count; i++) {
		chunk_file_state_destroy(states + i);
	}
	free(states);
	free(zeros);
	free(buffers);
	free(sizes);
	free(chunk_sizes);
	free(boundary_counts);
	free(state_index);

#ifndef NODEDUP
	sync_queue_term(chunk_queue);
#else
	jcr.status = JCR_STATUS_DONE;
#endif
	return NULL;
}

/*
 * chunk-level deduplication.
 * Destor currently supports fixed-sized chunking and (normalized) rabin-based chunking.
 */
static void* chunk_thread(void *arg) {
	int leftlen = 0;
	int leftoff = 0;
	unsigned char *leftbuf = malloc(DEFAULT_BLOCK_SIZE + destor.chunk_max_size);

	unsigned char *zeros = malloc(destor.chunk_max_size);
	bzero(zeros, destor.chunk_max_size);
	unsigned char *data = malloc(destor.chunk_max_size);

	struct chunk* c = NULL;

	while (1) {

		/* Try to receive a CHUNK_FILE_START. */
		c = sync_queue_pop(read_queue);

		if (c == NULL) {
#ifndef NODEDUP
			sync_queue_term(chunk_queue);
#endif	//NODEDUP
			break;
		}

		assert(CHECK_CHUNK(c, CHUNK_FILE_START));
#ifndef NODEDUP
			sync_queue_push(chunk_queue, c);
#endif	//NODEDUP

		/* Try to receive normal chunks. */
		c = sync_queue_pop(read_queue);
		if (!CHECK_CHUNK(c, CHUNK_FILE_END)) {
			memcpy(leftbuf, c->data, c->size);
			leftlen += c->size;
			free_chunk(c);
			c = NULL;
		}

		while (1) {
			/* c == NULL indicates more data for this file can be read. */
			while ((leftlen < destor.chunk_max_size) && c == NULL) {
				c = sync_queue_pop(read_queue);
				if (!CHECK_CHUNK(c, CHUNK_FILE_END)) {
					memmove(leftbuf, leftbuf + leftoff, leftlen);
					leftoff = 0;
					memcpy(leftbuf + leftlen, c->data, c->size);
					leftlen += c->size;
					free_chunk(c);
					c = NULL;
				}
			}

			if (leftlen == 0) {
				assert(c);
				break;
			}

			TIMER_DECLARE(1);
			TIMER_BEGIN(1);

			int	chunk_size = chunking(leftbuf + leftoff, leftlen);

			TIMER_END(1, jcr.chunk_time);

#ifndef NODEDUP
//do not pass the chunk down
			struct chunk *nc = new_chunk(chunk_size);
			memcpy(nc->data, leftbuf + leftoff, chunk_size);
			leftlen -= chunk_size;
			leftoff += chunk_size;

			if (memcmp(zeros, nc->data, chunk_size) == 0) {
				VERBOSE("Chunk phase: %ldth chunk  of %d zero bytes",
						chunk_num++, chunk_size);
				jcr.zero_chunk_num++;
				jcr.zero_chunk_size += chunk_size;
			} else
				VERBOSE("Chunk phase: %ldth chunk of %d bytes", chunk_num++,
						chunk_size);

			sync_queue_push(chunk_queue, nc);
#endif	//NODEDUP
		}
		//xzjin add file tail chunck at last
#ifndef NODEDUP
		sync_queue_push(chunk_queue, c);
#endif	//NODEDUP
		leftoff = 0;
		c = NULL;

		if(destor.chunk_algorithm == CHUNK_RABIN ||
				destor.chunk_algorithm == CHUNK_NORMALIZED_RABIN)
			windows_reset();

	}

	free(leftbuf);
	free(zeros);
	free(data);

#ifndef NODEDUP
#else	//NODEDUP
    jcr.status = JCR_STATUS_DONE;
#endif	//NODEDUP

	return NULL;
}


//xzjin get file data from read_queue and hash, then, put into chunk_queue
//xzjin only chunck, no dedup
void start_chunk_phase() {
	chunking_uses_gpu = 0;
	chunking_batch = NULL;
	chunking_segment_batch = NULL;
	destor.chunk_gpu_is_active = 0;
	chunking_gpu_close_fn = NULL;

	if (destor.chunk_algorithm == CHUNK_RABIN){
		int pwr;
		for (pwr = 0; destor.chunk_avg_size; pwr++) {
			destor.chunk_avg_size >>= 1;
		}
		destor.chunk_avg_size = 1 << (pwr - 1);

		assert(destor.chunk_avg_size >= destor.chunk_min_size);
		assert(destor.chunk_avg_size <= destor.chunk_max_size);
		assert(destor.chunk_max_size <= CONTAINER_SIZE - CONTAINER_META_SIZE);

		chunkAlg_init();
		chunking = rabin_chunk_data;
	}else if(destor.chunk_algorithm == CHUNK_NORMALIZED_RABIN){
		int pwr;
		for (pwr = 0; destor.chunk_avg_size; pwr++) {
			destor.chunk_avg_size >>= 1;
		}
		destor.chunk_avg_size = 1 << (pwr - 1);

		assert(destor.chunk_avg_size >= destor.chunk_min_size);
		assert(destor.chunk_avg_size <= destor.chunk_max_size);
		assert(destor.chunk_max_size <= CONTAINER_SIZE - CONTAINER_META_SIZE);

		chunkAlg_init();
		chunking = normalized_rabin_chunk_data;
	}else if(destor.chunk_algorithm == CHUNK_RABIN_JUMP){
		int pwr;
		for (pwr = 0; destor.chunk_avg_size; pwr++) {
			destor.chunk_avg_size >>= 1;
		}
		destor.chunk_avg_size = 1 << (pwr - 1);

		assert(destor.chunk_avg_size >= destor.chunk_min_size);
		assert(destor.chunk_avg_size <= destor.chunk_max_size);
		assert(destor.chunk_max_size <= CONTAINER_SIZE - CONTAINER_META_SIZE);

		rabinJump_init(destor.chunk_avg_size);
		chunking = rabinjump_chunk_data;
	}else if(destor.chunk_algorithm == CHUNK_TTTD){
		int pwr;
		for (pwr = 0; destor.chunk_avg_size; pwr++) {
			destor.chunk_avg_size >>= 1;
		}
		destor.chunk_avg_size = 1 << (pwr - 1);

		assert(destor.chunk_avg_size >= destor.chunk_min_size);
		assert(destor.chunk_avg_size <= destor.chunk_max_size);
		assert(destor.chunk_max_size <= CONTAINER_SIZE - CONTAINER_META_SIZE);

		chunkAlg_init();
		chunking = tttd_chunk_data;
	}else if(destor.chunk_algorithm == CHUNK_FIXED){
		assert(destor.chunk_avg_size <= CONTAINER_SIZE - CONTAINER_META_SIZE);

		destor.chunk_max_size = destor.chunk_avg_size;
		chunking = fixed_chunk_data;
	}else if(destor.chunk_algorithm == CHUNK_FILE){
		/*
		 * approximate file-level deduplication
		 * It splits the stream according to file boundaries.
		 * For a larger file, we need to split it due to container size limit.
		 * Hence, our approximate file-level deduplication are only for files smaller than CONTAINER_SIZE - CONTAINER_META_SIZE.
		 * Similar to fixed-sized chunking of $(( CONTAINER_SIZE - CONTAINER_META_SIZE )) chunk size.
		 * */
		destor.chunk_avg_size = CONTAINER_SIZE - CONTAINER_META_SIZE;
		destor.chunk_max_size = CONTAINER_SIZE - CONTAINER_META_SIZE;
		chunking = fixed_chunk_data;
	}else if(destor.chunk_algorithm == CHUNK_AE){
		assert(destor.chunk_avg_size <= destor.chunk_max_size);
		assert(destor.chunk_max_size <= CONTAINER_SIZE - CONTAINER_META_SIZE);

		chunking = ae_chunk_data;
		ae_init();
	} else if(destor.chunk_algorithm == CHUNK_FASTCDC){
		assert(destor.chunk_avg_size <= destor.chunk_max_size);
		assert(destor.chunk_max_size <= CONTAINER_SIZE - CONTAINER_META_SIZE);

		chunking = fastcdc_chunk_data;
		fastcdc_init();
		if (destor.chunk_gpu_enable) {
			if (fastcdc_gpu_init() == 0) {
				chunking = fastcdc_gpu_chunk_data;
				chunking_batch = fastcdc_gpu_chunk_batch;
				chunking_segment_batch = fastcdc_gpu_chunk_segments_batch;
				chunking_uses_gpu = 1;
				destor.chunk_gpu_is_active = 1;
				chunking_gpu_close_fn = fastcdc_gpu_close;
				NOTICE("Chunk phase: FastCDC GPU mode enabled, device=%d, batch-size=%d", destor.chunk_gpu_device_id, destor.chunk_gpu_batch_size);
			} else {
				WARNING("Chunk phase: FastCDC GPU mode unavailable, fallback to CPU");
			}
		}
	} else if(destor.chunk_algorithm == CHUNK_SC){
		chunking = sc_chunk_data;
		sc_init();
	} else if(destor.chunk_algorithm == CHUNK_GEARJUMP){
		chunking = gearjump_chunk_data;
#if SENTEST
		if(destor.jumpOnes){
			gearjump_init(destor.jumpOnes);
		}else{
//			gearjump_init(log2(destor.chunk_avg_size) - 2);
			gearjump_init(1);
		}
#else
		gearjump_init();
#endif //SENTEST
		if (destor.chunk_gpu_enable) {
			if (jc_gpu_init() == 0) {
				chunking = jc_gpu_chunk_data;
				chunking_batch = jc_gpu_chunk_batch;
				chunking_segment_batch = jc_gpu_chunk_segments_batch;
				chunking_uses_gpu = 1;
				destor.chunk_gpu_is_active = 1;
				chunking_gpu_close_fn = jc_gpu_close;
				NOTICE("Chunk phase: JC GPU mode enabled, device=%d, batch-size=%d", destor.chunk_gpu_device_id, destor.chunk_gpu_batch_size);
			} else {
				WARNING("Chunk phase: JC GPU mode unavailable, fallback to CPU");
			}
		}
	} else if(destor.chunk_algorithm == CHUNK_JCTTTD){
		gearjump_init(destor.jumpOnes);
		chunking = gearjumpTTTD_chunk_data;
	} else if(destor.chunk_algorithm == CHUNK_LEAP){
		leap_init(destor.chunk_avg_size, 0);
		chunking = leap_chunk_data;
	} else if(destor.chunk_algorithm == CHUNK_GEAR){
		gear_init(destor.chunk_avg_size, 0);
		chunking = gear_chunk_data;
	} else if(destor.chunk_algorithm == CHUNK_TTTDGEAR){
		gear_init(destor.chunk_avg_size, 0);
		chunking = TTTD_gear_chunk_data;
	} else if(destor.chunk_algorithm == CHUNK_NORMALIZED_GEARJUMP){
		normalized_gearjump_init(destor.jumpOnes);
		chunking = normalized_gearjump_chunk_data;
	} else{
		NOTICE("Invalid chunking algorithm");
		exit(1);
	}

	if (destor.chunk_gpu_enable && destor.chunk_algorithm != CHUNK_FASTCDC
			&& destor.chunk_algorithm != CHUNK_GEARJUMP) {
		WARNING("Chunk phase: GPU mode currently supports FastCDC/JC only, fallback to CPU");
	}

	chunk_queue = sync_queue_new(100);
	if (chunking_batch) {
		pthread_create(&chunk_t, NULL, chunk_thread_batch_gpu, NULL);
	} else {
		pthread_create(&chunk_t, NULL, chunk_thread, NULL);
	}
    pid_t tid = gettid(); 
	printf("chunking thread          id: %ld\n", chunk_t);
	printf("chunking thread         tid: %d\n", tid);
	tid = syscall(SYS_gettid);
	printf("chunking thread syscall tid: %d\n", tid);
}

void stop_chunk_phase() {
	pthread_join(chunk_t, NULL);
	if (chunking_uses_gpu) {
		if (chunking_gpu_close_fn) {
			chunking_gpu_close_fn();
		}
		chunking_uses_gpu = 0;
		chunking_batch = NULL;
		chunking_segment_batch = NULL;
		destor.chunk_gpu_is_active = 0;
		chunking_gpu_close_fn = NULL;
	}
	NOTICE("chunk phase stops successfully!");
}
