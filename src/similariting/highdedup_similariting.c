#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

void highdedup_similariting_init(){
	highdedup_sufeature_tab = g_hash_table_new(g_int64_hash, g_chunk_feature_equal);
}

/*Insert super features into the hash table*/
void highdedup_insert_sufeature(struct chunk* c){

	for (int i = 0; i < c->feaNum; i++) {
		GSequence *tq = g_hash_table_lookup(highdedup_sufeature_tab, &(c->fea[i]));
		if (!tq) {
			tq = g_sequence_new(NULL);
			g_hash_table_replace(highdedup_sufeature_tab, &(c->fea[i]), tq);
		}
		g_sequence_prepend(tq, c);
	}
}

struct chunk* highdedupSearchMostSimiChunk(GHashTable* cand_tab, struct chunk* c, int* curMaxHit, fpp curCandC){

	int* hitTime = g_hash_table_lookup(cand_tab, c);
	if(hitTime){
		*hitTime = *hitTime + 1;
	}else{
		hitTime = malloc(sizeof(int));
		assert(hitTime);
		*hitTime = 1;
		g_hash_table_replace(cand_tab, c, hitTime);
	}
	if(*hitTime > *curMaxHit) return c;

	return curCandC;
}

/** return base chunk if similary chunk is found
 *  else return 0
*/
struct chunk* highdedup_similariting(struct chunk* c){

	struct chunk* ret = NULL;
	GHashTable* cand_tab = g_hash_table_new_full(g_int64_hash,
			 g_int64_equal, NULL, free);
	int r = rand();
	int curMaxHitTime = 0;

	for (int i = 0; i < c->feaNum; i++) {
		GSequence *tq = (GSequence*)g_hash_table_lookup(highdedup_sufeature_tab, &(c->fea[i]));
		if(tq){
			GSequenceIter *end = g_sequence_get_end_iter(tq);
			GSequenceIter *iter = g_sequence_get_begin_iter(tq);
			for (; iter != end; iter = g_sequence_iter_next(iter)) {
				struct chunk* candChunk = (struct chunk*)g_sequence_get(iter);
				ret = highdedupSearchMostSimiChunk(cand_tab, candChunk, &curMaxHitTime, ret);
			}
		}
	}

	g_hash_table_destroy(cand_tab);

	if(ret){ return ret; }

	/*Only if the chunk is unique, add the chunk into sufeature table*/
	highdedup_insert_sufeature(c);
	return NULL;
}