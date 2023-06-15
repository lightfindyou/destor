#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"
#include "../jcr.h"

GHashTable* cand_tab;
GHashTable* existing_fea_tab;

void common_similariting_init(){
	cand_tab = g_hash_table_new_full(g_int64_hash,
									 g_int64_equal, NULL, free);
	existing_fea_tab = g_hash_table_new_full(g_int64_hash,
			 						 g_int64_equal, NULL, NULL);
}

/** Insert super features into the hash table
 * hash table[fea] --> List ---> chunk addr
*/
void insert_sufeature(struct chunk* c, int suFeaNum, GHashTable* sufea_tab){

//	printf("insert candidate chunk: %lx\n", c);
	for (int i = 0; i < suFeaNum; i++) {
		GSequence *tq = g_hash_table_lookup(sufea_tab, &(c->fea[i]));
		if (!tq) {
			tq = g_sequence_new(NULL);
			g_hash_table_replace(sufea_tab, &(c->fea[i]), tq);
		}
		g_sequence_prepend(tq, c);
	}
}

struct chunk* searchMostSimiChunk(GHashTable* cand_tab, struct chunk* c, int* curMaxHit,
							 fpp curCandC, struct chunk* excludedChunk){

	if(c == excludedChunk) {return curCandC;}
	int* hitTime = g_hash_table_lookup(cand_tab, c);
	if(hitTime){
		*hitTime = *hitTime + 1;
	}else{
		hitTime = malloc(sizeof(int));
		assert(hitTime);
		*hitTime = 1;
		g_hash_table_replace(cand_tab, c, hitTime);
	}

	if(*hitTime > *curMaxHit){
		*curMaxHit = hitTime;
		return c;
	}

	return curCandC;
}

int featureExists(feature fea, feature* feaList, int feaNum){

	for(int i = 0; i < feaNum; i++){
		if(fea == feaList[i]){
			return 1;
		}
	}

	return 0;
}

void insertFeaToTab(GHashTable* tab, struct chunk* c){
	//TODO insert feature to hash table
	for(int j = 0; j < c->feaNum; j++){
		g_hash_table_replace(tab, &(c->fea[j]), 1);
	}
}

gboolean true( gpointer key, gpointer value, gpointer user_data){
	return 1;
}

struct chunk* first_match_similariting(struct chunk* c, int suFeaNum, GHashTable* sufea_tab){

	struct chunk* ret = NULL;
	int curMaxHitTime = 0;

	for (int i = 0; i < suFeaNum; i++) {
		GSequence *tq = g_hash_table_lookup(sufea_tab, &(c->fea[i]));
		if(tq){
			ret = (struct chunk*)g_sequence_get(g_sequence_get_begin_iter(tq));
			goto retPoint;
		}
	}

retPoint:
	insert_sufeature(c, suFeaNum, sufea_tab);
	if(ret){
		//insert ret into chunk
		g_queue_push_tail(c->basechunk, ret);
	}

	/*Only if the chunk is unique, add the chunk into sufeature table*/
	/*UNNECESSARY, for high dedup ratio, always add*/
	return ret;
}

/** return base chunk if similary chunk is found
 *  else return 0
 * base chunk is the one shares most features
*/
struct chunk* most_match_similariting(struct chunk* c, int suFeaNum, GHashTable* sufea_tab){

	struct chunk* ret = NULL;
	int curMaxHitTime = 0;

	for (int i = 0; i < suFeaNum; i++) {
		/**skip the fea that is contained in exist feature*/
		TIMER_DECLARE(1);
		TIMER_BEGIN(1);
		GSequence *tq = g_hash_table_lookup(sufea_tab, &(c->fea[i]));
		TIMER_END(1, jcr.lookupFea_time);
		if(tq){
	//		printf("tq:%lx\n", tq);
	//		printf("sequence length: %d\n", g_sequence_get_length(tq));
			GSequenceIter *end = g_sequence_get_end_iter(tq);
			GSequenceIter *iter = g_sequence_get_begin_iter(tq);
			TIMER_DECLARE(2);
			TIMER_BEGIN(2);
			for (; iter != end; iter = g_sequence_iter_next(iter)) {
				struct chunk* candChunk = (struct chunk*)g_sequence_get(iter);
				ret = searchMostSimiChunk(cand_tab, candChunk, &curMaxHitTime, ret, NULL);
			}
			TIMER_END(2, jcr.chooseMostSim_time);
		}
	}

	g_hash_table_remove_all(cand_tab);
	TIMER_DECLARE(3);
	TIMER_BEGIN(3);
	insert_sufeature(c, suFeaNum, sufea_tab);
	TIMER_END(3, jcr.insertFea_time);

//	printf("Most match similaring chunk is: %p\n", ret);
	if(ret){
		//insert ret into chunk
		g_queue_push_tail(c->basechunk, ret);
		return ret;
	}

	/*Only if the chunk is unique, add the chunk into sufeature table*/
	/*UNNECESSARY, for high dedup ratio, always add*/
	return NULL;
}

/** return base chunk if similary chunk is found
 *  else return 0
 * base chunk is the one shares most features
*/
struct chunk* topK_match_similariting(struct chunk* c, int suFeaNum, GHashTable* sufea_tab){

	struct chunk* ret = NULL;
	int curMaxHitTime = 0;

	for(int baseNum = 0; baseNum < destor.baseChunkNum; baseNum++ ){
		ret = NULL;
		for (int i = 0; i < suFeaNum; i++) {
			/**skip the fea that is contained in exist feature*/
			if (g_hash_table_lookup(existing_fea_tab, &(c->fea[i]))) { continue; }
			jcr.simiFeaNum++;
			TIMER_DECLARE(1);
			TIMER_BEGIN(1);
			GSequence *tq = g_hash_table_lookup(sufea_tab, &(c->fea[i]));
			TIMER_END(1, jcr.lookupFea_time);
			if(tq){
				int seqLen = g_sequence_get_length(tq);
				jcr.candNum += seqLen;
				int iterBeginPos = 0>(seqLen-50)?0:(seqLen-50);
	//			printf("tq:%lx\n", tq);
	//			printf("sequence length: %d\n", g_sequence_get_length(tq));
				GSequenceIter *end = g_sequence_get_end_iter(tq);
//				GSequenceIter *iter = g_sequence_get_begin_iter(tq);
				GSequenceIter *iter = g_sequence_get_iter_at_pos(tq, iterBeginPos);
				TIMER_DECLARE(2);
				TIMER_BEGIN(2);
				for (; iter != end; iter = g_sequence_iter_next(iter)) {
					struct chunk* candChunk = (struct chunk*)g_sequence_get(iter);
					ret = searchMostSimiChunk(cand_tab, candChunk, &curMaxHitTime, ret, NULL);
				}
				TIMER_END(2, jcr.chooseMostSim_time);
			}
		}
		if(ret == NULL) { break; }
		//insert ret into chunk
		g_queue_push_tail(c->basechunk, ret);
		//insert feature to hash table
		insertFeaToTab(existing_fea_tab, ret);
		//clear cand_tab
		g_hash_table_foreach_remove(cand_tab, true, NULL);
	}

	g_hash_table_remove_all(cand_tab);
	g_hash_table_remove_all(existing_fea_tab);
	TIMER_DECLARE(3);
	TIMER_BEGIN(3);
	insert_sufeature(c, suFeaNum, sufea_tab);
	TIMER_END(3, jcr.insertFea_time);

//	printf("Most match similaring chunk is: %p\n", ret);
	if(ret){ return ret; }

	/*Only if the chunk is unique, add the chunk into sufeature table*/
	/*UNNECESSARY, for high dedup ratio, always add*/
	return NULL;
}