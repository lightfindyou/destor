#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

static void ntransform_similariting_init(){
	ntransform_sufeature_tab = g_hash_table_new(g_int64_hash, g_fingerprint_equal);
}

/*Insert super features into the hash table*/
void ntransform_insert_sufeature(struct chunk* c){
	GSequence* targetSeq;
	for (int i = 0; i < NTRANSFORM_SF_NUM; i++) {
		
		GQueue *tq = g_hash_table_lookup(ntransform_sufeature_tab, &(c->fea[i]));
		if (tq) {
			targetSeq = g_queue_peek_head(tq);
		}else{
			targetSeq = g_sequence_new(NULL);
			g_hash_table_replace(ntransform_sufeature_tab, &(c->fea[i]), targetSeq);
		}
		g_sequence_prepend(targetSeq, c);
	}
}

fpp searchMostSimiChunk(GHashTable* cand_tab, fpp fp, int* curMaxHit, fpp curCandFp){

	int* hitTime = NULL;
	GQueue *tq = g_hash_table_lookup(cand_tab, fp);
	if(tq){
		hitTime = (int*)g_queue_peek_head(tq);
		*hitTime = *hitTime + 1;
		g_hash_table_replace (cand_tab, fp, hitTime);

	}else{
		hitTime = malloc(sizeof(int));
		assert(hitTime);
		*hitTime = 1;
		g_hash_table_replace (cand_tab, fp, hitTime);
	}
	if(*hitTime > *curMaxHit) return fp;

	return curCandFp;
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
static fpp ntransform_similariting(struct chunk* c){

	fpp ret = NULL;
	GHashTable* cand_tab = g_hash_table_new_full(g_int64_hash,
			 g_fingerprint_equal, NULL, free);
	int r = rand();
	int curMaxHitTime = 0;

	for (int i = 0; i < NTRANSFORM_SF_NUM; i++) {
		GQueue *tq = g_hash_table_lookup(ntransform_sufeature_tab, &(c->fea[i]));
		GSequenceIter *end = g_sequence_get_end_iter(tq);
		GSequenceIter *iter = g_sequence_get_begin_iter(tq);
		for (; iter != end; iter = g_sequence_iter_next(iter)) {
			struct chunk* c = (struct chunk*)g_sequence_get(iter);
			fpp fp = c->fp;
			ret = searchMostSimiChunk(cand_tab, fp, &curMaxHitTime, ret);
		}
	}

	g_hash_table_destroy(cand_tab);

	if(ret){ return ret; }

	/*Only if the chunk is unique, add the chunk into sufeature table*/
	ntransform_insert_sufeature(c);
	return NULL;
}