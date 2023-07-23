#include "../destor.h"
#include "../jcr.h"
#include "../index/index.h"
#include "../backup.h"
#include "../storage/containerstore.h"
#include "../similariting/similariting.h"
#include "../xdelta3/xdelta3.h"
#include "../xdelta3/lz4.h"
#include "../xdelta3/xdelta_thread.h"
#include "recordDelta.h"

static int64_t chunk_num;
char* xdeltaBase;
pthread_t xdelta_tid[XDELTA_THREAD_NUM];

void (*recordDelta)(struct chunk *c1, struct chunk* c2, void* delta, int deltaSize);

void init_xdelta_thread(int recDeltaInfo){
	if(recDeltaInfo){
		printf("record delta info: true.\n");
		recordDelta = recordChunkAndDelta;
	}else{
		printf("record delta info: false.\n");
		recordDelta = recordNULL;
	}
}


#pragma GCC push_options
#pragma GCC optimize ("O0")
void *xdelta_thread(void *arg) {
	char deltaOut[4*destor.chunk_max_size];
	char compressOut[4*destor.chunk_max_size];
//	printf("size of deltaOut: %lx\n", 2*destor.chunk_max_size);
	while (1) {
		if((destor.curStatus & STATUS_XDELTA) == 0){
			continue;
		}
		struct chunk* c = sync_queue_pop(simi_queue);

		if (c == NULL) {
			sync_queue_term(xdelta_queue);
			break;
		}

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END) ||
			CHECK_CHUNK(c, CHUNK_SEGMENT_START) || CHECK_CHUNK(c, CHUNK_SEGMENT_END)){
//			sync_queue_push(xdelta_queue, c);
			continue;
		}

		jcr.cur_porcessed_size += c->size;
		if(!CHECK_CHUNK(c, CHUNK_DUPLICATE)){	//NOT the unique chunks

			TIMER_DECLARE(1);
			TIMER_BEGIN(1);
			int deltaSize;
			int compressSize;
			if(CHECK_CHUNK(c, CHUNK_SIMILAR)){	//chunk may be xdeltaed
				int refSize = 0;
				struct chunk* firstBase;
				if(c->basechunk && g_queue_get_length(c->basechunk)){	//NOT self compress chunk
					int refNum = g_queue_get_length(c->basechunk);
					VERBOSE("Similariting phase: %ldth chunk similar with %d chunks", chunk_num++, refNum);
					firstBase = g_queue_peek_head(c->basechunk);
					for(int i = 0; i < refNum; i++){
						//TODO copy data to buffer 
						struct chunk* basec = g_queue_pop_head(c->basechunk);
						memcpy(&xdeltaBase[refSize], basec->data, basec->size);
						refSize += basec->size;
					}
				}
				deltaSize = xdelta3_compress(c->data, c->size, xdeltaBase, refSize, deltaOut, 1);
				compressSize = LZ4_compress_default(c->data, compressOut,
							 c->size, 4*destor.chunk_max_size);

				if(deltaSize < compressSize){
					if(deltaSize < ((c->size)*(destor.compThreshold))){
						recordDelta(c, firstBase, deltaOut, deltaSize);
						//NOTE: do not change origin data, it will be used by following xdelta
						int32_t ori_size = c->size;
						assert(c->size);
	
						if (pthread_mutex_lock(&jcrMutex) != 0) {
							puts("failed to lock jcrMutex!");
							return;
						}
						jcr.total_xdelta_compressed_chunk++;
						jcr.total_xdelta_size += deltaSize;
						jcr.total_xdelta_saved_size += ori_size - deltaSize;
					}else{
						if (pthread_mutex_lock(&jcrMutex) != 0) {
							puts("failed to lock jcrMutex!");
							return NULL;
						}
					}
					jcr.total_xdelta_chunk++;
					jcr.total_size_after_dedup += deltaSize;		//the size of chunk after xdelta
				}else{
					int32_t ori_size = c->size;
					jcr.total_lz4_compressed_chunk++;
					jcr.total_lz4_saved_size += ori_size - deltaSize;
				}
			}else{				//chunk unable to be xdeltaed
				if (pthread_mutex_lock(&jcrMutex) != 0) {
					puts("failed to lock jcrMutex!");
					return NULL;
				}
				if(!CHECK_CHUNK(c, CHUNK_DUPLICATE)){
					jcr.total_size_after_dedup += c->size;	//the size unique chunks stored in storage
				}
				jcr.total_unique_size += c->size;
			}

			if (pthread_mutex_unlock(&jcrMutex) != 0) {
				puts("failed to unlock jcrMutex!");
				return NULL;
			}
			if (pthread_mutex_lock(&xdeltaTimeMutex) != 0) {
				puts("failed to lock!");
				return;
			}
			TIMER_END(1, jcr.xdelta_time);
			if (pthread_mutex_unlock(&xdeltaTimeMutex)) {
				puts("failed to unlock!");
				return;
			}
		}else{	/*duplicate chunk*/ }
	}

	jcr.status = JCR_STATUS_DONE;
	return NULL;

}
#pragma GCC pop_options

void start_xdelta_thread() {
	for (int i = 0; i < XDELTA_THREAD_NUM; i++){
		pthread_create(&xdelta_tid[i], NULL, xdelta_thread, NULL);
	}
}
