#include "destor.h"
#include "jcr.h"
#include "index/index.h"
#include "backup.h"
#include "storage/containerstore.h"
#include "similariting/similariting.h"
#include "xdelta3/xdelta3.h"

static pthread_t simi_t;
static int64_t chunk_num;

static void (*similariting)(struct chunk* c);

void *simi_thread(void *arg) {
	printf("simi thread         tid: %d\n", gettid());
	char deltaOut[2*destor.chunk_avg_size];
	while (1) {
		if((destor.curStatus & STATUS_SIMI) == 0){
//			printf("simi: %d\n", destor.curStatus & STATUS_SIMI);
			continue;
		}
		struct chunk* c = sync_queue_pop(feature_queue);

		if (c == NULL) {
			sync_queue_term(simi_queue);
			break;
		}


		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END) ||
			CHECK_CHUNK(c, CHUNK_SEGMENT_START) || CHECK_CHUNK(c, CHUNK_SEGMENT_END)) {
			sync_queue_push(simi_queue, c);
			continue;
		}

		//here cannot use CHECK_CHUNK(c, CHUNK_UNIQUE), because CHUNK_UNIQUE is 0x0
		if(!CHECK_CHUNK(c, CHUNK_DUPLICATE)){
			TIMER_DECLARE(1);
			TIMER_BEGIN(1);
			/*find similar chunks*/
			similariting(c);
			TIMER_END(1, jcr.seaFea_time);
			jcr.featuredChunks++;

			if(c->basechunk){
				struct chunk* basec = c->basechunk;
				jcr.similarChunks++;
//				printf("simi_phase Chunk similar with %p\n", basec);
//				printf("which similar with %p\n", basec->basechunk);
				UNSET_CHUNK(c, CHUNK_UNIQUE);
				SET_CHUNK(c, CHUNK_SIMILAR);
			}
		}

		VERBOSE("Similariting phase: %ldth chunk similar with %p", chunk_num++, c->basechunk);

		sync_queue_push(simi_queue, c);
	}

	printf("simi over!\n");
	return NULL;
}

void start_simi_phase() {

	if (destor.similarity_algorithm == SIMILARITY_NTRANSFORM){
		ntransform_similariting_init();
		similariting = ntransform_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_DEEPSKETCH){
		deepsketch_similariting_init();
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
	}else if(destor.similarity_algorithm == SIMILARITY_FINENESS_FLATFEA){
		fineness_similariting_init();
		similariting = fineness_similariting_flatFea;
	}else if(destor.similarity_algorithm == SIMILARITY_BRUTEFORCE){
		bruteforce_similariting_init();
		similariting = bruteforce_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_FINE_ANN){
		fineANN_similariting_init();
		similariting = fineANN_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_STATIS){
		statis_similariting_init();
		similariting = statis_similariting;
	}else if(destor.similarity_algorithm == SIMILARITY_WEIGHTCHUNK){
		weightchunk_similariting_init();
		similariting = weightchunk_similariting;
	}


	simi_queue = sync_queue_new(SIMIQUESIZE);
	pthread_create(&simi_t, NULL, simi_thread, NULL);
}

void stop_simi_phase() {
    SETSTATUS(STATUS_SIMI);
	pthread_join(simi_t, NULL);
	NOTICE("similarity phase stops successfully: %d chunks", chunk_num);
}
