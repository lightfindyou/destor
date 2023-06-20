#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"
#include "../featuring/featuring.h"
#include "../jcr.h"

int executeOverThreadNum = 0;
feature* simiFeature;
int simiFeatureNum = 0;
int loopIdx;
pthread_mutex_t simi_mutex[MAX_FEANUM];
pthread_mutex_t threadCounterMutex;
//pthread_mutex_t mutexThreadReturnResult;
pthread_cond_t simi_cond_begin;
pthread_cond_t simi_cond_ReturnResult;
pthread_mutex_t  mutexReturnResult;
pthread_rwlock_t candTableRWLock;
pthread_rwlock_t mainThreadRWLock;
static pthread_t simi_t[MAX_FEANUM];
struct chunk* oneSearchRet = NULL;
int curMaxHitTime = 0;
int threadIdx[MAX_FEANUM];

struct chunk* searchMostSimiChunk_MT(GHashTable* cand_tab, struct chunk* c, int* curMaxHit){

	if(pthread_rwlock_rdlock(&candTableRWLock)){
		printf("search most similar chunk read lock fails\n");
	}
	int* hitTime = g_hash_table_lookup(cand_tab, c);
	if(pthread_rwlock_unlock(&candTableRWLock)){
		printf("search most similar chunk read unlock fails\n");
	}

	if(pthread_rwlock_wrlock(&candTableRWLock)){
		printf("search most similar chunk write lock fails\n");
	}
	if(hitTime){
//		__sync_add_and_fetch(hitTime, 1);
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
	if(pthread_rwlock_unlock(&candTableRWLock)){
		printf("search most similar chunk write unlock fails\n");
	}

	return NULL;
}

void addAndTestCounter(){
	if (pthread_mutex_lock(&threadCounterMutex) != 0) {
		puts("topK common simi MT failed to lock threadCounterMutex!");
		return;
	}

	if(__sync_add_and_fetch(&executeOverThreadNum, 1) == simiFeatureNum){
//	if(__sync_add_and_fetch(&executeOverThreadNum, 1) == MAX_FEANUM)
		if (pthread_mutex_unlock(&threadCounterMutex) != 0) {
			puts("topK common simi MT failed to unlock threadCounterMutex!");
			return;
		}
		//NOTE here lock main lock to guarantee the main thread is waiting for signal
		if (pthread_mutex_lock(&mutexReturnResult) != 0) {
			puts("topK common simi MT failed to lock!");
			return;
		}
		pthread_cond_broadcast(&simi_cond_ReturnResult);
		if (pthread_mutex_unlock(&mutexReturnResult) != 0) {
			puts("topK common simi MT failed to unlock!");
			return;
		}
		printf("return statified, broadcasting, feature num: %d!\n", simiFeatureNum);
	}else{
		if (pthread_mutex_unlock(&threadCounterMutex) != 0) {
			puts("topK common simi MT failed to unlock threadCounterMutex!");
			return;
		}
		printf("return NOT statified\n");
	}
}

void* thread_topK_match_similariting_MT(void *idp){
	int threadId = *(int*)idp;
	printf("simi thread index %d\n", threadId);

	if (pthread_mutex_lock(&simi_mutex[threadId]) != 0) {
		puts("common simi MT failed to lock!");
		return NULL;
	}
	while(1){
		if(pthread_rwlock_unlock(&mainThreadRWLock)){
			printf("top-K thread MT unlock rwLock error.\n");
		}
		/** If put the thread signal lock here, sometimes signal will
		 *  send before this thread gain the lock, thus
		 *  missing the signal
		 */
		pthread_cond_wait(&simi_cond_begin, &simi_mutex[threadId]);
		if(pthread_rwlock_rdlock(&mainThreadRWLock)){
			printf("top-K thread MT unlock rwLock error.\n");
		}

		printf("loop %d, thread %2d of feature %2d start\n", loopIdx, threadId, simiFeatureNum);
		if(!simiFeature){
			printf("simi thread %d over!", threadId);	
			if (pthread_mutex_unlock(&simi_mutex[threadId]) != 0) {
				puts("common simi MT failed to unlock!");
				return NULL;
			}
			return NULL;
		}
		/**does not run if threadID is bigger than feature ID and
		 * skip the fea that is contained in exist feature*/
		if((threadId) >= simiFeatureNum){
			continue;
//			goto skipFeaSearch;
		}

		if(g_hash_table_lookup(existing_fea_tab, &(simiFeature[threadId]))){
			goto skipFeaSearch;
		}
		GSequence *tq = g_hash_table_lookup(commonSimiSufeatureTab, &(simiFeature[threadId]));
		if(tq){
//			printf("tq:%lx\n", tq);
//			printf("sequence length: %d\n", g_sequence_get_length(tq));
			GSequenceIter *end = g_sequence_get_end_iter(tq);
			GSequenceIter *iter = g_sequence_get_begin_iter(tq);
			for (; iter != end; iter = g_sequence_iter_next(iter)) {
				struct chunk* candChunk = (struct chunk*)g_sequence_get(iter);
				searchMostSimiChunk_MT(cand_tab, candChunk, &curMaxHitTime);
			}
		}

skipFeaSearch:
		printf("loop %d, thread %2d of feature %2d execute over\n", loopIdx, threadId, simiFeatureNum);
		addAndTestCounter();
	}
}

inline void simiThreadStartBroadcasting(int suFeaNum) __attribute__((always_inline));
inline void simiThreadStartBroadcasting(int suFeaNum){
	for(int i = 0; i < suFeaNum; i++){
		printf("trying lock simi_mutex %d\n", i);
		if (pthread_mutex_lock(&simi_mutex[i]) != 0) {
			printf("topK common simi MT failed to lock %d mutex!", i);
			return;
		}
		printf("lock simi_mutex %d over\n", i);
	}
	pthread_cond_broadcast(&simi_cond_begin);
	for(int i = 0; i < suFeaNum; i++){
		if (pthread_mutex_unlock(&simi_mutex[i]) != 0) {
			printf("topK common simi MT failed to unlock %d mutex!", i);
			return;
		}
	}
}

struct chunk* topK_match_similariting_MT(struct chunk* c, int suFeaNum){
	if(c){
		simiFeature = c->fea;
	}else{
		simiFeature = NULL;
		pthread_cond_broadcast(&simi_cond_begin);
		return NULL;
	}
	simiFeatureNum = suFeaNum;
	printf("feature number: %d\n", suFeaNum);

	if (pthread_mutex_lock(&mutexReturnResult) != 0) {
		puts("topK common simi MT failed to lock!");
		return NULL;
	}

	for(int baseNum = 0; baseNum < destor.baseChunkNum; baseNum++ ){
		loopIdx = baseNum;
		executeOverThreadNum = 0;
		oneSearchRet = NULL;
		curMaxHitTime = 0;

		simiThreadStartBroadcasting(suFeaNum);
//		simiThreadStartBroadcasting(MAX_FEANUM);
		if(pthread_rwlock_unlock(&mainThreadRWLock)){
			printf("top-K MT unlock rwLock error.\n");
		}
		pthread_cond_wait(&simi_cond_ReturnResult, &mutexReturnResult);
		if(pthread_rwlock_wrlock(&mainThreadRWLock)){
			printf("top-K MT lock rwLock error.\n");
		}

		if(oneSearchRet == NULL) { break; }
		//insert ret into chunk
		g_queue_push_tail(c->basechunk, oneSearchRet);
		//insert feature to hash table
		insertFeaToTab(existing_fea_tab, oneSearchRet);
		//clear cand_tab
		g_hash_table_foreach_remove(cand_tab, true, NULL);
		printf("base chunk %d search over\n", baseNum);
	}

	g_hash_table_remove_all(cand_tab);
	g_hash_table_remove_all(existing_fea_tab);
	insert_sufeature(c, suFeaNum, commonSimiSufeatureTab);

	if (pthread_mutex_unlock(&mutexReturnResult) != 0) {
		puts("topK common simi MT failed to unlock!");
		return NULL;
	}

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
	pthread_cond_init(&simi_cond_begin, 0);
	pthread_cond_init(&simi_cond_ReturnResult, 0);
	pthread_rwlock_init(&candTableRWLock, 0);
	pthread_rwlock_init(&mainThreadRWLock, 0);
	pthread_mutex_init(&threadCounterMutex, 0);
//	pthread_mutex_init(&mutexThreadReturnResult, 0);

	for(int i = 0; i< feaNum; i++){
		if (pthread_mutex_init(&simi_mutex[i], 0)){
				printf("init cond %d error\n", i);
		}
		threadIdx[i] = i;
		if(pthread_create(&simi_t[i], NULL, thread_topK_match_similariting_MT,
					 (void*)(&threadIdx[i]))){
			printf("create simi thread MT error\n");
		}else{
			printf("create simi thread %lu\n", simi_t[i]);
		}
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