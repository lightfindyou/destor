#include "destor.h"
#include "jcr.h"
#include "index/index.h"
#include "backup.h"
#include "featuring/featuring.h"
#include "storage/containerstore.h"

static pthread_t feature_t;
static int64_t chunk_num;

static void (*featuring)(unsigned char* buf, int size, struct chunk* c);

void *feature_thread(void *arg) {
	while (1) {
		struct chunk* c = sync_queue_pop(dedup_queue);

		if (c == NULL) {
			sync_queue_term(feature_queue);
//			jcr.status = JCR_STATUS_DONE;
			break;
		}

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END) || 
			CHECK_CHUNK(c, CHUNK_SEGMENT_START) || CHECK_CHUNK(c, CHUNK_SEGMENT_END)) {
			sync_queue_push(feature_queue, c);
			continue;
		}

		TIMER_DECLARE(1);
		TIMER_BEGIN(1);
		/*calculate features*/
		featuring(c->data, c->size, c);
		TIMER_END(1, jcr.fea_time);

		//TODO xzjin out of boundary fix
		VERBOSE("Feature phase: %ldth chunk featured by %s", chunk_num++, c->fea);

		sync_queue_push(feature_queue, c);
	}
	return NULL;

}

void start_feature_phase() {

	if (destor.feature_algorithm == FEAUTRE_NTRANSFORM){
		rabinhash_rabin_init();
		featuring = ntransform_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_DEEPSKETCH){
		deepsketch_featuring_init(destor.modelPath);
		featuring = deepsketch_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_FINENESS){
		rabinhash_rabin_init();
		featuring = finesse_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_FINESS_FLATFEA){
		rabinhash_rabin_init();
		featuring = finesse_featuring_flatFea;
	}else if(destor.feature_algorithm == FEAUTRE_ODESS){
		gearhash_gear_init(ODESS_FEATURE_NUM);
		featuring = odess_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_HIGHDEDUP){
		gearhash_gear_init(destor.featureNum);
		featuring = highdedup_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_HIGHDEDUP_FSC){
		featuring = highdedup_featuring_fsc;
	}else if(destor.feature_algorithm == FEAUTRE_ODESS_FLATFEA){
		gearhash_gear_init(ODESS_FEATURE_NUM);
		featuring = odess_featuring_flatFea;
	}else if(destor.feature_algorithm == FEAUTRE_BRUTEFORCE){
		featuring = bruteforce_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_FINE_ANN){
		initGearMatrixFea();
		featuring = fineANN_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_STATIS){
		initGearMatrixFea();
		featuring = statis_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_WEIGHTCHUNK){
		weightchunkGearInit();
		featuring = weightchunk_featuring;
	}

	feature_queue = sync_queue_new(1000);

	pthread_create(&feature_t, NULL, feature_thread, NULL);
}

void stop_feature_phase() {
	pthread_join(feature_t, NULL);
	NOTICE("feature phase stops successfully: %d chunks", chunk_num);
}
