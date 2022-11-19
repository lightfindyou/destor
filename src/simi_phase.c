/*
 * In the phase,
 * we aggregate chunks into segments,
 * and deduplicate each segment with its similar segments.
 * Duplicate chunks are identified and marked.
 * For fingerprint indexes exploiting physical locality (e.g., DDFS, Sampled Index),
 * segments are only for batch process.
 * */
#include "destor.h"
#include "jcr.h"
#include "index/index.h"
#include "backup.h"
#include "storage/containerstore.h"
#include "similariting/similariting.h"
#include "xdelta3/xdelta3.h"

static pthread_t store_t;
static int64_t chunk_num;

static chunkid (*similariting)(feature fea);

void *store_thread(void *arg) {
	char deltaOut[2*destor.chunk_avg_size];
	while (1) {
		struct chunk* c = sync_queue_pop(feature_queue);

		if (c == NULL) {
			sync_queue_term(simi_queue);
			break;
		}

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END)) {
			sync_queue_push(simi_queue, c);
			continue;
		}

		TIMER_DECLARE(1);
		TIMER_BEGIN(1);
		if(CHECK_CHUNK(c, CHUNK_UNIQUE)){
			/*calculate features*/
			c->basefp = similariting(c->fea);

			if(c->basefp){
				UNSET_CHUNK(c, CHUNK_UNIQUE);
				SET_CHUNK(c, CHUNK_SIMILAR);
			}
		}
		TIMER_END(1, jcr.simi_time);

		VERBOSE("Similariting phase: %ldth chunk similar with %ld", chunk_num++, c->basefp);

		//TODO store chunk
		sync_queue_push(simi_queue, c);
	}
	return NULL;

}

void start_simi_phase() {

	if (destor.similarity_algorithm == SIMILARITY_NTRANSFORM){
		similariting = ntransform_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_DEEPSKETCH){
		similariting = deepsketch_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_FINENESS){
		fineness_similariting_init();
		similariting = fineness_similariting;
	}

	feature_queue = sync_queue_new(1000);
	pthread_create(&store_t, NULL, store_thread, NULL);
}

void stop_simi_phase() {
	pthread_join(store_t, NULL);
	NOTICE("similarity phase stops successfully: %d chunks", chunk_num);
}
