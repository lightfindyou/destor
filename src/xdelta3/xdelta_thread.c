#include "../destor.h"
#include "../jcr.h"
#include "../index/index.h"
#include "../backup.h"
#include "../storage/containerstore.h"
#include "../similariting/similariting.h"
#include "../xdelta3/xdelta3.h"
#include "../xdelta3/xdelta_thread.h"
#include "recordDelta.h"

static int64_t chunk_num;
char* xdeltaBase;

void (*recordDelta)(struct chunk *c1, struct chunk* c2, void* delta, int deltaSize);

void init_xdelta_thread(int recDeltaInfo){
	if(recDeltaInfo){
		printf("record delta info: true.");
		recordDelta = recordChunkAndDelta;
	}else{
		printf("record delta info: false.");
		recordDelta = recordNULL;
	}
}


#pragma GCC push_options
#pragma GCC optimize ("O0")
void *xdelta_thread(void *arg) {
	char deltaOut[4*destor.chunk_max_size];
//	printf("size of deltaOut: %lx\n", 2*destor.chunk_max_size);
	while (1) {
		if(!(destor.curStatus & status_xdelta)){ continue; }
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
		if(!CHECK_CHUNK(c, CHUNK_DUPLICATE)){	//the unique chunks

			TIMER_DECLARE(1);
			TIMER_BEGIN(1);
			int deltaSize;
			if(c->basechunk){	//chunk may be xdeltaed
//				printf("xdelta c:%lx, c->flags:%x c->data:%lx, c->size:%ld, basec:%lx, basec->flag:%x, basec->data:%lx, basec->size:%ld\n", 
//							c, c->flag, c->data, c->size, basec, basec->flag, basec->data, basec->size);
				int refSize = 0;
				int refNum = g_queue_get_length(c->basechunk);
				VERBOSE("Similariting phase: %ldth chunk similar with %d chunks", chunk_num++, refNum);
				struct chunk* firstBase = g_queue_peek_head(c->basechunk);
				for(int i = 0; i < refNum; i++){
					//TODO copy data to buffer 
					struct chunk* basec = g_queue_pop_head(c->basechunk);
					memcpy(&xdeltaBase[refSize], basec->data, basec->size);
					refSize += basec->size;
				}
				deltaSize = xdelta3_compress(c->data, c->size, xdeltaBase, refSize, deltaOut, 1);
				if(deltaSize < ((c->size)*(destor.compThreshold))){
					recordDelta(c, firstBase, deltaOut, deltaSize);
					//NOTE: do not change origin data, it will be used by following xdelta
//					memcpy(c->data, deltaOut, deltaSize);
					int32_t ori_size = c->size;
//					c->size = deltaSize;
//					printf("c->size: %d, delta size: %d\n", c->size, deltaSize);
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
			//TODO this time is uncorrect
			TIMER_END(1, jcr.xdelta_time);
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
