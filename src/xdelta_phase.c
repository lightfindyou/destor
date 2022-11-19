#include "destor.h"
#include "jcr.h"
#include "index/index.h"
#include "backup.h"
#include "storage/containerstore.h"
#include "similariting/similariting.h"
#include "xdelta3/xdelta3.h"

static pthread_t store_t;
static int64_t chunk_num;

void *store_thread(void *arg) {
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
			TIMER_DECLARE(1);
			TIMER_BEGIN(1);
			//TODO get chunk by id
			struct chunk* basec;

			GQueue *tq = g_hash_table_lookup(fp_tab, c->basefp);
			if (tq) {
				basec = g_queue_peek_head(tq);
			}else{
				DEBUG("find chunk wrong.\n");
			}
			//TODO xdelta
			xdelta3_compress(c->data, c->size, basec->data, basec->size, deltaOut, 1);
			//TODO calcualte output size

			TIMER_END(1, jcr.hash_time);
		}
		VERBOSE("Similariting phase: %ldth chunk similar with %ld", chunk_num++, c->basecid);

		//TODO calcualte output size

		//TODO store chunk
		sync_queue_push(xdelta_queue, c);
	}
	return NULL;

}

void start_xdelta_phase() {

	fp_tab = g_hash_table_new(g_int64_hash, g_fingerprint_equal);
	feature_queue = sync_queue_new(1000);
	pthread_create(&store_t, NULL, store_thread, NULL);
}

void stop_xdelta_phase() {
	pthread_join(store_t, NULL);
	NOTICE("xdelta phase stops successfully: %d chunks", chunk_num);
}
