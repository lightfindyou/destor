#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

GHashTable* weightchunk_sufeature_tab;

void weightchunk_similariting_init(){
	weightchunk_sufeature_tab = g_hash_table_new(g_int64_hash, g_int64_equal);
	common_similariting_init();
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
void weightchunk_similariting(struct chunk* c){
	most_match_similariting(c, c->feaNum, weightchunk_sufeature_tab);
	return;
}