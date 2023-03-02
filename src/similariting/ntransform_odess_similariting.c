#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

void ntransform_similariting_init(){
	ntransform_sufeature_tab = g_hash_table_new(g_int64_hash, g_chunk_feature_equal);
}

void odess_similariting_init(){
	odess_sufeature_tab = g_hash_table_new(g_int64_hash, g_chunk_feature_equal);
}

/*Insert super features into the hash table*/
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

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
struct chunk* most_match_similariting(struct chunk* c, int suFeaNum, GHashTable* sufea_tab){

	struct chunk* ret = NULL;
	GHashTable* cand_tab = g_hash_table_new_full(g_int64_hash,
			 g_int64_equal, NULL, free);
	int r = rand();
	int curMaxHitTime = 0;

	for (int i = 0; i < suFeaNum; i++) {
		GSequence *tq = g_hash_table_lookup(sufea_tab, &(c->fea[i]));
		if(tq){
//			printf("tq:%lx\n", tq);
//			printf("sequence length: %d\n", g_sequence_get_length(tq));
			GSequenceIter *end = g_sequence_get_end_iter(tq);
			GSequenceIter *iter = g_sequence_get_begin_iter(tq);
			for (; iter != end; iter = g_sequence_iter_next(iter)) {
				struct chunk* candChunk = (struct chunk*)g_sequence_get(iter);
				ret = searchMostSimiChunk(cand_tab, candChunk, &curMaxHitTime, ret);
			}
		}
	}

	g_hash_table_destroy(cand_tab);

	if(ret){ return ret; }

	/*Only if the chunk is unique, add the chunk into sufeature table*/
	insert_sufeature(c, suFeaNum, sufea_tab);
	return NULL;
}

struct chunk* ntransform_similariting(struct chunk* c){
	return most_match_similariting(c, NTRANSFORM_SF_NUM, ntransform_sufeature_tab);
}

struct chunk* odess_similariting(struct chunk* c){
	return most_match_similariting(c, ODESS_SF_NUM, odess_sufeature_tab);
}

struct chunk* odess_similariting_flatFea(struct chunk* c){
	return most_match_similariting(c, ODESS_FEATURE_NUM, odess_sufeature_tab);
}