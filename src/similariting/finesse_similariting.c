#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

void fineness_similariting_init(){
	fineness_sufeature_tab = g_hash_table_new(g_int64_hash, g_fingerprint_equal);
}

/*Insert super features into the hash table*/
void fineness_insert_sufeature(struct chunk* c){
	for (int i = 0; i < FINESSE_SF_NUM; i++) {
		
		GSequence *tq = g_hash_table_lookup(fineness_sufeature_tab, &(c->fea[i]));
		if(!tq) {
			tq = g_sequence_new(NULL);
			g_hash_table_replace(fineness_sufeature_tab, &(c->fea[i]), tq);
		}
		g_sequence_prepend(tq, c);
	}
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
unsigned char* fineness_similariting(struct chunk* c){
	chunkid baseID = 0;
	for (int i = 0; i < FINESSE_SF_NUM; i++) {
		GSequence *tq = (GSequence*)g_hash_table_lookup(fineness_sufeature_tab, &(c->fea[i]));
		if (tq) {
			struct chunk* c = (struct chunk*)g_sequence_get(g_sequence_get_begin_iter(tq));
			unsigned char* basefp = c->fp;
			return basefp;
		}
	}

	/*only if the chunk is unique, add the chunk into sufeature table*/
	fineness_insert_sufeature(c);
	return 0;
}