#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

GHashTable* weightchunk_sufeature_tab;

void weightchunk_similariting_init(){
	weightchunk_sufeature_tab = g_hash_table_new(g_int64_hash, g_int64_equal);
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
struct chunk* weightchunk_similariting(struct chunk* c){
	struct chunk* ret = most_match_similariting(c, c->feaNum, weightchunk_sufeature_tab);
	return ret;
}