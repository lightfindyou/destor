#include "../destor.h"
#include "../jcr.h"
#include "../index/index.h"
#include "../backup.h"
#include "../storage/containerstore.h"
#include "../similariting/similariting.h"
#include "../xdelta3/xdelta3.h"
#include "../xdelta3/xdelta_thread.h"

static int64_t chunk_num;

void *xdelta_thread(void *arg) {
	char deltaOut[2*destor.chunk_max_size];
	while (1) {
		struct chunk* c = sync_queue_pop(simi_queue);

		if (c == NULL) {
			sync_queue_term(xdelta_queue);
			break;
		}

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END)) {
//			sync_queue_push(xdelta_queue, c);
			continue;
		}

		if(c->basechunk){
//			TIMER_DECLARE(1);
//			TIMER_BEGIN(1);
			struct chunk* basec = c->basechunk;
//			struct chunk* basec = g_hash_table_lookup_threadsafe(fp_tab, c->basechunk, fp_tab_mutex);
//			if (! basec) {
//				printf("find chunk wrong.\n");
//			}
			VERBOSE("Similariting phase: %ldth chunk similar with %d", chunk_num++, basec->basechunk);
//			printf("xdelta c:%lx, c->data:%lx, c->size:%ld,   basec:%lx, basec->data:%lx, basec->size:%ld\n", 
//					c, c->data, c->size, basec, basec->data, basec->size);
			int deltaSize = xdelta3_compress(c->data, c->size, basec->data, basec->size, deltaOut, 1);
			if(deltaSize < c->size){
				jcr.total_xdelta_compressed_chunk++;
				memcpy(c->data, deltaOut, deltaSize);
				int32_t ori_size = c->size;
				c->size = deltaSize;
				jcr.total_xdelta_size += c->size;
				jcr.total_xdelta_saved_size += ori_size - c->size;
			}else{
				jcr.total_unique_size += c->size;
			}
			jcr.total_xdelta_chunk++;

//			//TODO if calculate size, here should use mutex
//			TIMER_END(1, jcr.xdelta_time);
		}else{
			jcr.total_unique_size += c->size;
		}
		//TODO calcualte output size/
		jcr.total_dedup_size += c->size;

		//TODO store chunk
//		sync_queue_push(xdelta_queue, c);
	}

	jcr.status = JCR_STATUS_DONE;
	return NULL;

}

void start_xdelta_thread() {
	for (int i = 0; i < XDELTA_THREAD_NUM; i++){
		pthread_create(&xdelta_tid[i], NULL, xdelta_thread, NULL);
	}
}
