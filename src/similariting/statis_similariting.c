#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

GHashTable* statis_sufeature_tab;

void statis_similariting_init(){
	statis_sufeature_tab = g_hash_table_new(g_int64_hash, g_int64_equal);
	common_similariting_init();
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
void statis_similariting(struct chunk* c){
	most_match_similariting(c, STATIS_FEATURE_NUM, statis_sufeature_tab);
	return;
}