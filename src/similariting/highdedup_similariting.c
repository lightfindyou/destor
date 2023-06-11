#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

GHashTable* highdedup_sufeature_tab;

void highdedup_similariting_init(){
	highdedup_sufeature_tab = g_hash_table_new(g_int64_hash, g_int64_equal);
	common_similariting_init();
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
void highdedup_similariting(struct chunk* c){
	topK_match_similariting(c, c->feaNum, highdedup_sufeature_tab);
	return;
}