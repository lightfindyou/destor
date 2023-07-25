#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"
#include "../featuring/featuring.h"
#include "../jcr.h"

volatile feature* simiFeature;
int simiFeatureNum = 0;
int loopIdx;
pthread_mutex_t simiThreadOverMutex;
pthread_mutex_t timeMutex[11];
pthread_rwlock_t mainThreadRWLock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_t simi_t[MAX_FEANUM];
int curMaxHitTime = 0;
int skipFeaNum = 0;
int threadIdx[MAX_FEANUM];
volatile short threadNeedExecute[MAX_FEANUM];
volatile int executeOverThreadNum = 0;
struct chunk* simiSearchRet = NULL;
GAsyncQueue* threadCandQueue[MAX_FEANUM];
struct chunk**  threadCandListHead;

inline void resetCandQueue(GAsyncQueue* queue) __attribute__((always_inline));
void resetCandQueue(GAsyncQueue* queue){
	int queueLen = g_async_queue_length(queue);
	for(int i=0; i<queueLen; i++){
		struct chunk* c = g_async_queue_pop(queue);
		c->simiHitTime = 0;
	}
}

inline void resetCandChunks(struct chunk** list, int* length) __attribute__((always_inline));
void resetCandChunks(struct chunk** list, int* lengthp){
	int length = *lengthp;
	for(int i=0; i < length; i++){
		list[i]->simiHitTime = 0;
	}
	*lengthp = 0;
}

inline void addAndTestCounter() __attribute__((always_inline));
void addAndTestCounter(){
	if (G_UNLIKELY(pthread_mutex_lock(&simiThreadOverMutex) != 0)) {
		ERROR("topK common simi MT failed to lock simiThreadOverMutex!");
		return;
	}
	executeOverThreadNum++;

	if (G_UNLIKELY(pthread_mutex_unlock(&simiThreadOverMutex) != 0)) {
		ERROR("topK common simi MT failed to unlock simiThreadOverMutex!");
		return;
	}
}

int allFeatureExisting(struct chunk* c){
	int i = 0;
	for(; i<c->feaNum; i++){
		gpointer skipFeature = g_hash_table_lookup(existing_fea_tab, &(c->fea[i]));
		if(skipFeature == NULL){
			printf("feature %d not contained in table.\n");
			return 0;
		}
	}
//	printf("All %d-%d-%d feature contained in table.\n",
//			 c->feaNum, i, g_hash_table_size(existing_fea_tab));
	return 1;
}

void* thread_topK_match_similariting_MT(void *idp){
	int threadId = *(int*)idp;
	struct chunk** threadCandList = &threadCandListHead[threadId*destor.simiCandLimit];

	while(1){
		if(threadNeedExecute[threadId] == 0){ continue; }

		int candListIdx = 0;
		if(G_UNLIKELY(pthread_rwlock_rdlock(&mainThreadRWLock))){
			printf("top-K thread MT lock rwLock error.\n");
		}

//		feature* tmp = simiFeature;
		if(G_UNLIKELY(!simiFeature)){
			if(G_UNLIKELY(pthread_rwlock_unlock(&mainThreadRWLock))){
				ERROR("top-K thread MT unlock rwLock error.\n");
			}
//			printf("current feature is: %p\n", tmp);
			threadNeedExecute[threadId] = 0;
			return NULL;
		}

//		TIMER_DECLARE(5);
//		TIMER_BEGIN(5);
		//TODO how much time this operation take?
		gpointer skipFeature = g_hash_table_lookup(existing_fea_tab, &(simiFeature[threadId]));
//		TIMER_END(5, jcr.checkSkipFeature);
		if(skipFeature){
			skipFeaNum++;
			goto skipFeaSearch;
		}

//		TIMER_DECLARE(4);
//		TIMER_BEGIN(4);
		chunkList *cl = g_hash_table_lookup(commonSimiSufeatureTab, &(simiFeature[threadId]));
//		TIMER_END(4, jcr.checkHashTable);

//		TIMER_DECLARE(2);
//		TIMER_BEGIN(2);
		if(cl){
			int listLen = cl->length;
			struct chunk** candList = cl->list;
			jcr.candNum += listLen;		//Number of chunks that share same feature with existing chunk
			int iterBeginPos = 0>(listLen-destor.simiCandLimit)?0:(listLen-destor.simiCandLimit);

//			TIMER_DECLARE(6);
//			TIMER_BEGIN(6);
			for (int i = iterBeginPos; i < listLen; i++) {

//				TIMER_DECLARE(7);
//				TIMER_BEGIN(7);
				struct chunk* candChunk = candList[i];
//				int hitTime = __sync_add_and_fetch(&(candChunk->simiHitTime), 1);
				int hitTime = ++(candChunk->simiHitTime);
//				TIMER_END(7, jcr.updateHitTime);

//				TIMER_DECLARE(8);
//				TIMER_BEGIN(8);
				if(hitTime > curMaxHitTime){
					curMaxHitTime = hitTime;
					simiSearchRet = candChunk;
				}
//				TIMER_END(8, jcr.updateSimiChunk);

//				TIMER_DECLARE(9);
//				TIMER_BEGIN(9);
//				threadCandList[candListIdx++] = candChunk;
//				TIMER_END(9, jcr.appendCandChunk);
			}
//			TIMER_END(6, jcr.compareHitTime);
			resetCandChunks(threadCandList, &candListIdx);
		}
//		TIMER_END(2, jcr.chooseMostSim_time);

skipFeaSearch:
		addAndTestCounter();
		threadNeedExecute[threadId] = 0;
		if(G_UNLIKELY(pthread_rwlock_unlock(&mainThreadRWLock))){
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

	if(G_UNLIKELY(pthread_rwlock_wrlock(&mainThreadRWLock))){
		ERROR("top-K MT lock rwLock error.\n");
	}

	for(int baseNum = 0; baseNum < destor.baseChunkNum; baseNum++){
		loopIdx = baseNum;
		executeOverThreadNum = 0;
		simiSearchRet = NULL;
		curMaxHitTime = 0;
		skipFeaNum = 0;

		memset((short *)threadNeedExecute, 1, sizeof(short)*simiFeatureNum);
		if(G_UNLIKELY(pthread_rwlock_unlock(&mainThreadRWLock))){
			ERROR("top-K MT unlock rwLock error.\n");
		}

		TIMER_DECLARE(1);
		TIMER_BEGIN(1);
		while(executeOverThreadNum != simiFeatureNum){ continue; }
		TIMER_END(1, jcr.lookupFea_time);

		if(G_UNLIKELY(pthread_rwlock_wrlock(&mainThreadRWLock))){
			ERROR("top-K MT lock rwLock error.\n");
		}

		if(simiSearchRet == NULL) {
//			printf("skipped feature num and chunk feature number: %d-%d\n",
//					 skipFeaNum, c->feaNum);
			//the unlock outside the loop while act
			break; 
		}
		//insert ret into chunk
		g_queue_push_tail(c->basechunk, simiSearchRet);
		//insert feature to hash table
		insertFeaToTab(existing_fea_tab, simiSearchRet);
//		allFeatureExisting(c);
	}

	if(G_UNLIKELY(pthread_rwlock_unlock(&mainThreadRWLock))){
		ERROR("top-K MT unlock rwLock error.\n");
	}

	TIMER_DECLARE(3);
	TIMER_BEGIN(3);
	g_hash_table_remove_all(existing_fea_tab);
	insert_sufeatureHashList(c, suFeaNum, commonSimiSufeatureTab);
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

	threadCandListHead = calloc(destor.simiCandLimit*MAX_FEANUM, sizeof(struct chunk*));
	
	if(G_UNLIKELY(pthread_rwlock_init(&mainThreadRWLock, 0) ||
			pthread_mutex_init(&simiThreadOverMutex, 0))){
		ERROR("init mutex error\n");
	}

	memset((short *)threadNeedExecute, 0, sizeof(short)*MAX_FEANUM);

	for(int i=0; i<11; i++){
		pthread_mutex_init(&timeMutex[i], 0);
	}

	for(int i = 0; i< feaNum; i++){
		threadIdx[i] = i;
		threadCandQueue[i] = g_async_queue_new();
		assert(threadNeedExecute[i] == 0);
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