#ifndef __USE_UNIX98
#define __USE_UNIX98
#endif //__USE_UNIX98
#include <xxhash.h>
#include <pthread.h>
#include <sys/queue.h>
#include "../destor.h"
#include "similariting.h"
#include "../featuring/featuring.h"
#include "../jcr.h"

GAsyncQueue* simi_cand_queue;
feature* simiFeature;
int simiFeatureNum = 0;
int loopIdx;
pthread_mutex_t addCounterMutex;
pthread_mutex_t candTableMutex;
//pthread_rwlock_t candTableRWLock = PTHREAD_RWLOCK_INITIALIZER;
pthread_rwlock_t mainThreadRWLock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_t simi_t[MAX_FEANUM];
struct chunk* oneSearchRet = NULL;
int curMaxHitTime = 0;
int threadIdx[MAX_FEANUM];
volatile short threadNeedExecute[MAX_FEANUM];
volatile int executeOverThreadNum = 0;

void resetCandQueue(GAsyncQueue* queue){
	int queueLen = g_async_queue_length(queue);
	for(int i=0; i<queueLen; i++){
		struct chunk* c = g_async_queue_pop(queue);
		c->simiHitTime = 0;
	}
}

inline struct chunk* searchMostSimiChunk_MT(GHashTable* cand_tab,
		 struct chunk* c, int* curMaxHit) __attribute__((always_inline));
struct chunk* searchMostSimiChunk_MT(GHashTable* cand_tab, struct chunk* c, int* curMaxHit){
//	if(pthread_rwlock_rdlock(&candTableRWLock)){
//		ERROR("search most similar chunk readlock fails\n");
//	}
//	int* hitTime = g_hash_table_lookup(cand_tab, c);
//	if(pthread_rwlock_unlock(&candTableRWLock)){
//		ERROR("search most similar chunk unlock fails\n");
//	}
//
//	if(pthread_rwlock_wrlock(&candTableRWLock)){
//		ERROR("search most similar chunk lock fails\n");
//	}
//	hitTime = g_hash_table_lookup(cand_tab, c);
//	if(hitTime){
//		*hitTime = *hitTime+1;
//	}else{
//		hitTime = malloc(sizeof(int));
//		assert(hitTime);
//		*hitTime = 1;
//		g_hash_table_replace(cand_tab, c, hitTime);
//	}
//
//	if(*hitTime > *curMaxHit){
//		*curMaxHit = *hitTime;
//		oneSearchRet = c;
//	}
//	if(pthread_rwlock_unlock(&candTableRWLock)){
//		ERROR("search most similar chunk unlock fails\n");
//	}

	if(pthread_mutex_lock(&candTableMutex)){
		ERROR("search most similar chunk lock fails\n");
	}
	int* hitTime = g_hash_table_lookup(cand_tab, c);
	if(hitTime){
		*hitTime = *hitTime+1;
	}else{
		hitTime = malloc(sizeof(int));
		assert(hitTime);
		*hitTime = 1;
		g_hash_table_replace(cand_tab, c, hitTime);
	}

	if(*hitTime > *curMaxHit){
		*curMaxHit = *hitTime;
		oneSearchRet = c;
	}
	if(pthread_mutex_unlock(&candTableMutex)){
		ERROR("search most similar chunk unlock fails\n");
	}

	return NULL;
}

struct chunk* searchMostSimiChunk_MT_list(GAsyncQueue* queue, struct chunk* c, int* curMaxHit){

	int hitTime = __sync_add_and_fetch(&(c->simiHitTime), 1);

	if(hitTime > *curMaxHit){
		*curMaxHit = hitTime;
		oneSearchRet = c;
	}

	if(hitTime == 1){
		g_async_queue_lock(queue); 
		g_async_queue_push_unlocked(queue, c);
		g_async_queue_unlock(queue);
	}

	return NULL;
}

inline void addAndTestCounter() __attribute__((always_inline));
void addAndTestCounter(){
	if (pthread_mutex_lock(&addCounterMutex) != 0) {
		ERROR("topK common simi MT failed to lock addCounterMutex!");
		return;
	}
	executeOverThreadNum++;

	if (pthread_mutex_unlock(&addCounterMutex) != 0) {
		ERROR("topK common simi MT failed to unlock addCounterMutex!");
		return;
	}
}

void* thread_topK_match_similariting_MT(void *idp){
	int threadId = *(int*)idp;

	while(1){
		if(threadNeedExecute[threadId] == 0){
			continue;
		}
		if(pthread_rwlock_rdlock(&mainThreadRWLock)){
			printf("top-K thread MT lock rwLock error.\n");
		}

		if(!simiFeature){
			if(pthread_rwlock_unlock(&mainThreadRWLock)){
				ERROR("top-K thread MT unlock rwLock error.\n");
			}
			threadNeedExecute[threadId] = 0;
			return NULL;
		}
		assert(threadId < simiFeatureNum);
		if(g_hash_table_lookup(existing_fea_tab, &(simiFeature[threadId]))){
			goto skipFeaSearch;
		}

		TIMER_DECLARE(1);
		TIMER_BEGIN(1);
		GSequence *tq = g_hash_table_lookup(commonSimiSufeatureTab, &(simiFeature[threadId]));
		TIMER_END(1, jcr.lookupFea_time);

		TIMER_DECLARE(2);
		TIMER_BEGIN(2);
		if(tq){
			GSequenceIter *end = g_sequence_get_end_iter(tq);
			GSequenceIter *iter = g_sequence_get_begin_iter(tq);
			for (; iter != end; iter = g_sequence_iter_next(iter)) {
				struct chunk* candChunk = (struct chunk*)g_sequence_get(iter);
//				searchMostSimiChunk_MT(cand_tab, candChunk, &curMaxHitTime);
				searchMostSimiChunk_MT_list(simi_cand_queue, candChunk, &curMaxHitTime);
			}
		}
		TIMER_END(2, jcr.chooseMostSim_time);

skipFeaSearch:
		addAndTestCounter();
		threadNeedExecute[threadId] = 0;
		if(pthread_rwlock_unlock(&mainThreadRWLock)){
			ERROR("top-K thread MT unlock rwLock error.\n");
		}
	}
}

struct chunk* topK_match_similariting_MT(struct chunk* c, int suFeaNum){
	if(c){
		simiFeature = c->fea;
	}else{
		simiFeature = NULL;
		memset((short *)threadNeedExecute, 1, sizeof(short)*MAX_FEANUM);
		return NULL;
	}
	simiFeatureNum = suFeaNum;

	if(pthread_rwlock_wrlock(&mainThreadRWLock)){
		ERROR("top-K MT lock rwLock error.\n");
	}

	for(int baseNum = 0; baseNum < destor.baseChunkNum; baseNum++){
		loopIdx = baseNum;
		executeOverThreadNum = 0;
		oneSearchRet = NULL;
		curMaxHitTime = 0;

		memset((short *)threadNeedExecute, 1, sizeof(short)*simiFeatureNum);
		if(pthread_rwlock_unlock(&mainThreadRWLock)){
			ERROR("top-K MT unlock rwLock error.\n");
		}

		while(executeOverThreadNum != simiFeatureNum){ continue; }
		if(pthread_rwlock_wrlock(&mainThreadRWLock)){
			ERROR("top-K MT lock rwLock error.\n");
		}

		if(oneSearchRet == NULL) {
			//the unlock outside the loop while act
			break; 
		}
		//insert ret into chunk
		g_queue_push_tail(c->basechunk, oneSearchRet);
		//insert feature to hash table
		insertFeaToTab(existing_fea_tab, oneSearchRet);
		//clear cand_tab
//		g_hash_table_remove_all(cand_tab);
		resetCandQueue(simi_cand_queue);
	}

	if(pthread_rwlock_unlock(&mainThreadRWLock)){
		ERROR("top-K MT unlock rwLock error.\n");
	}

	TIMER_DECLARE(3);
	TIMER_BEGIN(3);
//	g_hash_table_remove_all(cand_tab);
	resetCandQueue(simi_cand_queue);

	g_hash_table_remove_all(existing_fea_tab);
	insert_sufeature(c, suFeaNum, commonSimiSufeatureTab);
	TIMER_END(3, jcr.insertFea_time);

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
									 GAsyncQueue*
	simi_cand_queue = g_async_queue_new();
	
	if(pthread_mutex_init(&candTableMutex, 0) ||
			pthread_rwlock_init(&mainThreadRWLock, 0) ||
			pthread_mutex_init(&addCounterMutex, 0)){
//	if(pthread_rwlock_init(&candTableRWLock, 0) ||
//			pthread_rwlock_init(&mainThreadRWLock, 0) ||
//			pthread_mutex_init(&addCounterMutex, 0))
		ERROR("init mutex error\n");
	}

	memset((short *)threadNeedExecute, 0, sizeof(short)*MAX_FEANUM);

	for(int i = 0; i< feaNum; i++){
		threadIdx[i] = i;
		if(pthread_create(&simi_t[i], NULL, thread_topK_match_similariting_MT,
					 (void*)(&threadIdx[i]))){
			printf("create simi thread MT error\n");
		}
	}
}

void common_similariting_stop_MT(){
	//Call with a NULL para to term the threads
	topK_match_similariting_MT(NULL, 0);

	for(int i = 0; i<MAX_FEANUM; i++){
		pthread_join(simi_t[i], NULL);
	}
}