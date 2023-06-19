#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"
#include "../featuring/featuring.h"
#include "../jcr.h"

int executeOverThreadNum = 0;
feature* simiFeature;
int simiFeatureNum = 0;

pthread_mutex_t simi_mutex[MAX_FEANUM];
pthread_cond_t simi_cond_begin;
pthread_cond_t simi_cond_start[MAX_FEANUM];
pthread_cond_t simi_cond_end[MAX_FEANUM];
pthread_mutex_t  mutexReturnResult;
pthread_cond_t condReturnResult;
pthread_rwlock_t candTableRWLock;
static pthread_t simi_t[MAX_FEANUM];
static pthread_t getChunk_simi_t;
struct chunk* oneSearchRet = NULL;
int curMaxHitTime = 0;

struct chunk* searchMostSimiChunk_MT(GHashTable* cand_tab, struct chunk* c, int* curMaxHit,
							 fpp curCandC){

	if(pthread_rwlock_rdlock(&candTableRWLock)){
		printf("search most similar chunk read lock fails\n");
	}
	int* hitTime = g_hash_table_lookup(cand_tab, c);
	if(pthread_rwlock_unlock(&candTableRWLock)){
		printf("search most similar chunk read unlock fails\n");
	}
	if(hitTime){
		__sync_add_and_fetch(hitTime, 1);
	}else{
		hitTime = malloc(sizeof(int));
		assert(hitTime);
		*hitTime = 1;
		if(pthread_rwlock_wrlock(&candTableRWLock)){
			printf("search most similar chunk write lock fails\n");
		}
		g_hash_table_replace(cand_tab, c, hitTime);
		if(pthread_rwlock_unlock(&candTableRWLock)){
			printf("search most similar chunk write unlock fails\n");
		}
	}

	if(*hitTime > *curMaxHit){
		*curMaxHit = hitTime;
		oneSearchRet = c;
	}

}

void addAndTestCounter(int target){
	if(__sync_add_and_fetch(&executeOverThreadNum, 1) == target){
		pthread_cond_broadcast(&condReturnResult);
	}
}

struct chunk* thread_topK_match_similariting_MT(int threadIdx){

	while(1){
		if (pthread_mutex_lock(&simi_mutex[threadIdx]) != 0) {
			puts("common simi MT failed to lock!");
			return NULL;
		}
		pthread_cond_wait(&simi_cond_begin, &simi_mutex[threadIdx]);

		if(simiFeature){ return NULL; }
		/**does not run if threadID is bigger than feature ID and
		 * skip the fea that is contained in exist feature*/
		if(threadIdx>=simiFeatureNum &&
			g_hash_table_lookup(existing_fea_tab, &(simiFeature[threadIdx]))){
				goto nextLoop;
		}

		GSequence *tq = g_hash_table_lookup(commonSimiSufeatureTab, &(simiFeature[threadIdx]));
		if(tq){
//			printf("tq:%lx\n", tq);
//			printf("sequence length: %d\n", g_sequence_get_length(tq));
			GSequenceIter *end = g_sequence_get_end_iter(tq);
			GSequenceIter *iter = g_sequence_get_begin_iter(tq);
			for (; iter != end; iter = g_sequence_iter_next(iter)) {
				struct chunk* candChunk = (struct chunk*)g_sequence_get(iter);
				/**TODO 1 how to stop these threads
				*/
				searchMostSimiChunk_MT(cand_tab, candChunk, &curMaxHitTime, oneSearchRet);
			}
		}

nextLoop:
		if (pthread_mutex_unlock(&simi_mutex[threadIdx]) != 0) {
			puts("common simi MT failed to unlock!");
			return NULL;
		}
	}
}

struct chunk* topK_match_similariting_MT(struct chunk* c, int suFeaNum){
	executeOverThreadNum = 0;
	if(c){
		simiFeature = c->fea;
	}else{
		simiFeature = NULL;
		pthread_cond_broadcast(&simi_cond_begin);
		return NULL;
	}
	simiFeatureNum = suFeaNum;

	for(int baseNum = 0; baseNum < destor.baseChunkNum; baseNum++ ){
		oneSearchRet = NULL;
		curMaxHitTime = 0;
		pthread_cond_broadcast(&simi_cond_begin);

		if (pthread_mutex_lock(&mutexReturnResult) != 0) {
			puts("common simi MT failed to lock!");
			return;
		}
		pthread_cond_wait(&condReturnResult, &mutexReturnResult);
		if (pthread_mutex_unlock(&mutexReturnResult) != 0) {
			puts("common simi MT failed to unlock!");
			return;
		}

		if(oneSearchRet == NULL) { break; }
		//insert ret into chunk
		g_queue_push_tail(c->basechunk, oneSearchRet);
		//insert feature to hash table
		insertFeaToTab(existing_fea_tab, oneSearchRet);
		//clear cand_tab
		g_hash_table_foreach_remove(cand_tab, true, NULL);
	}

	g_hash_table_remove_all(cand_tab);
	g_hash_table_remove_all(existing_fea_tab);
	insert_sufeature(c, suFeaNum, commonSimiSufeatureTab);

	/*Only if the chunk is unique, add the chunk into sufeature table*/
	/*UNNECESSARY, for high dedup ratio, always add*/
	return NULL;
}

void common_similariting_init_MT(int feaNum){
	commonSimiSufeatureTab = g_hash_table_new(g_int64_hash, g_int64_equal);
	cand_tab = g_hash_table_new_full(g_int64_hash,
									 g_int64_equal, NULL, free);
	existing_fea_tab = g_hash_table_new_full(g_int64_hash,
			 						 g_int64_equal, NULL, NULL);
	pthread_cond_init(&simi_cond_start, 0);
	pthread_cond_init(&simi_cond_begin, 0);
	pthread_cond_init(&condReturnResult, 0);
	pthread_rwlock_init(&candTableRWLock, 0);

	for(int i = 0; i<MAX_FEANUM; i++){
		if (pthread_mutex_init(&simi_mutex[i], 0)
			|| pthread_cond_init(&simi_cond_start[i], 0)
			|| pthread_cond_init(&simi_cond_end[i], 0)){
				printf("init cond %d error\n", i);
		}
		pthread_create(&simi_t[i], NULL, thread_topK_match_similariting_MT, &i);
	}

//	pthread_create(&getChunk_simi_t, NULL, getChunk_topK_match_similariting_MT, NULL);
}

void common_similariting_stop_MT(){
	//Call with a NULL para to term the threads
	topK_match_similariting_MT(NULL, 0);
//	pthread_join(getChunk_simi_t, NULL);

	for(int i = 0; i<MAX_FEANUM; i++){
		pthread_join(simi_t[i], NULL);
	}
}