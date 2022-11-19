#include "destor.h"
#include "jcr.h"
#include "index/index.h"
#include "backup.h"
#include "storage/containerstore.h"
#include "similariting/similariting.h"
#include "xdelta3/xdelta3.h"
#include "xdelta3/xdelta_thread.h"

static int64_t chunk_num;

void *xdelta_thread(void *arg) {
	char deltaOut[2*destor.chunk_avg_size];
	while (1) {
		struct chunk* c = sync_queue_pop(simi_queue);

		if (c == NULL) {
			sync_queue_term(xdelta_queue);
			break;
		}

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END)) {
			sync_queue_push(xdelta_queue, c);
			continue;
		}

		if(c->basefp){
//			TIMER_DECLARE(1);
//			TIMER_BEGIN(1);
			//TODO get chunk by id
			struct chunk* basec;

			//TOTO fix, here may the base chunk may have not been add into fp_tab
			GQueue *tq = g_hash_table_lookup(fp_tab, c->basefp);
			if (tq) {
				basec = g_queue_peek_head(tq);
			}else{
				DEBUG("find chunk wrong.\n");
			}
			VERBOSE("Similariting phase: %ldth chunk similar with %d", chunk_num++, basec->cid);
			int deltaSize = xdelta3_compress(c->data, c->size, basec->data, basec->size, deltaOut, 1);
			if(deltaSize< c->size){
				memcpy(c->data, deltaOut, deltaSize);
				c->size = deltaSize;
				jcr.total_xdelta_size += c->size;
			}else{
				jcr.total_unique_size += c->size;
			}

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
	return NULL;

}

void start_xdelta_thread() {
	for (int i = 0; i < XDELTA_THREAD_NUM; i++){
		pthread_create(&xdelta_tid[i], NULL, xdelta_thread, NULL);
	}
}
