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
#include "featuring/featuring.h"
#include "storage/containerstore.h"

static pthread_t feature_t;
static int64_t chunk_num;

static void (*featuring)(unsigned char* buf, int size, unsigned char* fea);

void *feature_thread(void *arg) {
	while (1) {
		struct chunk* c = sync_queue_pop(dedup_queue);

		if (c == NULL) {
			sync_queue_term(feature_queue);
			break;
		}

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END)) {
			sync_queue_push(feature_queue, c);
			continue;
		}

		TIMER_DECLARE(1);
		TIMER_BEGIN(1);
		/*calculate features*/
		featuring(c->data, c->size, &c->fea);
		TIMER_END(1, jcr.hash_time);

		//TODO xzjin out of boundary fix
		VERBOSE("Feature phase: %ldth chunk featured by %s", chunk_num++, c->fea);

		sync_queue_push(feature_queue, c);
	}
	return NULL;

}

void start_feature_phase() {

	if (destor.feature_algorithm == FEAUTRE_NTRANSFORM){
		featuring = ntransform_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_DEEPSKETCH){
		featuring = deepsketch_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_FINENESS){
		featuring = fineness_featuring;
	}

	feature_queue = sync_queue_new(1000);
	pthread_create(&feature_t, NULL, feature_thread, NULL);
}

void stop_feature_phase() {
	pthread_join(feature_t, NULL);
	NOTICE("feature phase stops successfully: %d chunks", chunk_num);
}
