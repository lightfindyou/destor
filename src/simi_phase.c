#include "destor.h"
#include "jcr.h"
#include "index/index.h"
#include "backup.h"
#include "storage/containerstore.h"
#include "similariting/similariting.h"
#include "xdelta3/xdelta3.h"

static pthread_t simi_t;
static int64_t chunk_num;

static struct chunk* (*similariting)(struct chunk* c);

void *simi_thread(void *arg) {
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
		//here cannot use CHECK_CHUNK(c, CHUNK_UNIQUE), because CHUNK_UNIQUE is 0x0
		if(!CHECK_CHUNK(c, CHUNK_DUPLICATE)){
			/*find similar chunks*/
			c->basechunk = similariting(c);
			jcr.featuredChunks++;

			if(c->basechunk){
				jcr.similarChunks++;
				UNSET_CHUNK(c, CHUNK_UNIQUE);
				SET_CHUNK(c, CHUNK_SIMILAR);
			}
		}
		TIMER_END(1, jcr.simi_time);

		VERBOSE("Similariting phase: %ldth chunk similar with %ld", chunk_num++, c->basechunk);

		//TODO store chunk
		sync_queue_push(simi_queue, c);
	}
	return NULL;

}

void start_simi_phase() {

	if (destor.similarity_algorithm == SIMILARITY_NTRANSFORM){
		ntransform_similariting_init();
		similariting = ntransform_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_DEEPSKETCH){
		similariting = deepsketch_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_FINENESS){
		fineness_similariting_init();
		similariting = fineness_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_HIGHDEDUP){
		highdedup_similariting_init();
		similariting = highdedup_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_ODESS){
		odess_similariting_init();
		similariting = odess_similariting;
	}

	simi_queue = sync_queue_new(1000);
	pthread_create(&simi_t, NULL, simi_thread, NULL);
}

void stop_simi_phase() {
	pthread_join(simi_t, NULL);
	NOTICE("similarity phase stops successfully: %d chunks", chunk_num);
}
