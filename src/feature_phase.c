#include "destor.h"
#include "jcr.h"
#include "index/index.h"
#include "backup.h"
#include "featuring/featuring.h"
#include "storage/containerstore.h"

static pthread_t feature_t;
static int64_t chunk_num;

SyncQueue* feature_queue;
SyncQueue* feature_temp_queue;


void (*recordFeature)(struct chunk *c);
void (*stopRecordFeature)();
static int (*featuring)(unsigned char* buf, int size, struct chunk* c);

void *feature_thread(void *arg) {
	printf("feature thread         tid: %d\n", gettid());
	while (1) {
		if((destor.curStatus & STATUS_FEATURE) == 0){
//			printf("feature: %d\n", destor.curStatus & STATUS_FEATURE);
			continue;
		}
		struct chunk* c = sync_queue_pop(dedup_queue);

		if (c == NULL) {
			sync_queue_term(feature_queue);
//			jcr.status = JCR_STATUS_DONE;
			//Get the last chunks out for deepsketch
			if(featuring == deepsketch_featuring){
				int featuringRet = deepsketch_featuring_stop();
				for(int i = 0; i < featuringRet; i++){
					struct chunk *c = sync_queue_pop(feature_temp_queue);
					sync_queue_push(feature_queue, c);
					recordFeature(c);
				}
			}
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
		int featuringRet = featuring(c->data, c->size, c);
		jcr.totalFeaNum += c->feaNum;
		TIMER_END(1, jcr.fea_time);

		//TODO xzjin out of boundary fix
		VERBOSE("Feature phase: %ldth chunk featured by %s", chunk_num++, c->fea);

		//TODO maybe use a stack here to temporarily store
		if(featuring == deepsketch_featuring){
			for(int i = 0; i < featuringRet; i++){
				struct chunk *c = sync_queue_pop(feature_temp_queue);
				sync_queue_push(feature_queue, c);
				recordFeature(c);
			}
		}else{
			sync_queue_push(feature_queue, c);
			recordFeature(c);
		}
	}

//	printf("feature over!\n");
	return NULL;
}

void start_feature_phase() {
	int featureNum = 0;
	int featureLength = destor.featureLen;

	if (destor.feature_algorithm == FEAUTRE_NTRANSFORM){
		rabinhash_rabin_init();
		featuring = ntransform_featuring;
	}else if(destor.feature_algorithm == FEAUTRE_DEEPSKETCH){
		deepsketch_featuring_init(destor.modelPath);
		featuring = deepsketch_featuring;
		featureNum = 1;
		featureLength = 128;
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
		featuring = highdedup_featuring;	//TODO need to write in 
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

	feature_queue = sync_queue_new(FEAQUESIZE);
	feature_temp_queue = sync_queue_new(BATCH_SIZE * 3);

	if(destor.store_feature){
		recordFeatureToFile_init(featureNum, featureLength);
		recordFeature = recordFeatureToFile;
		stopRecordFeature = stopRecordFeatureToFile;
	}else{
		recordFeature = NULL;
		stopRecordFeature = NULL;
	}

	pthread_create(&feature_t, NULL, feature_thread, NULL);
}

void stop_feature_phase() {
    SETSTATUS(STATUS_FEATURE);
	pthread_join(feature_t, NULL);
	stopRecordFeature();
	NOTICE("feature phase stops successfully: %d chunks", chunk_num);
}
