#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

void ntransform_similariting_init(){
	ntransform_sufeature_tab = g_hash_table_new(g_int64_hash, g_chunk_feature_equal);
}

void odess_similariting_init(){
	odess_sufeature_tab = g_hash_table_new(g_int64_hash, g_chunk_feature_equal);
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