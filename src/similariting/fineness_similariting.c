#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

static void fineness_similariting_init(){
	fineness_sufeature_tab = g_hash_table_new(g_int64_hash, g_fingerprint_equal);
	srand(time(NULL));
}

/*Insert super features into the hash table*/
void fineness_insert_sufeature(struct chunk* c){
	GSequence* targetSeq;
	for (int i = 0; i < FINESSE_SF_NUM; i++) {
		
		GQueue *tq = g_hash_table_lookup(fineness_sufeature_tab, &(c->fea[i]));
		if (tq) {
			targetSeq = g_queue_peek_head(tq);
		}else{
			targetSeq = g_sequence_new(NULL);
			g_hash_table_replace(fineness_sufeature_tab, &(c->fea[i]), targetSeq);
		}
		g_sequence_prepend(targetSeq, c);
	}
}

/** return base chunk id if similary chunk is found
 *  else return 0
*/
static unsigned char* fineness_similariting(struct chunk* c){
	int r = rand();
	GSequence* simSeq;
	chunkid baseID = 0;
	for (int i = 0; i < FINESSE_SF_NUM; i++) {
		int index = (r + i) % FINESSE_SF_NUM;
		GQueue *tq = g_hash_table_lookup(fineness_sufeature_tab, &(c->fea[i]));
		if (tq) {
			simSeq = g_queue_peek_head(tq);
			struct chunk* c = (struct chunk*)g_sequence_get(g_sequence_get_begin_iter(simSeq));
			unsigned char* basefp = c->fp;
			return basefp;
		}
	}

	/*only if the chunk is unique, add the chunk into sufeature table*/
	fineness_insert_sufeature(c);
	return 0;
}