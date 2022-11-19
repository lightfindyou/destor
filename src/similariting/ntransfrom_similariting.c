#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

static chunkid ntransform_similariting(feature fea){

}

static void ntransform_similariting_init(){
	ntransform_sufeature_tab = g_hash_table_new(g_int64_hash, g_fingerprint_equal);
	srand(time(NULL));
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

/** return base chunk id if similary chunk is found
 *  else return 0
*/
static unsigned char* ntransform_similariting(struct chunk* c){
	int r = rand();
	GSequence* simSeq;
	chunkid baseID = 0;
	for (int i = 0; i < NTRANSFORM_SF_NUM; i++) {
		int index = (r + i) % NTRANSFORM_SF_NUM;
		GQueue *tq = g_hash_table_lookup(ntransform_sufeature_tab, &(c->fea[i]));
		if (tq) {
			simSeq = g_queue_peek_head(tq);
			struct chunk* c = (struct chunk*)g_sequence_get(g_sequence_get_begin_iter(simSeq));
			unsigned char* basefp = c->fp;
			return basefp;
		}
	}

	/*only if the chunk is unique, add the chunk into sufeature table*/
	ntransform_insert_sufeature(c);
	return 0;
}