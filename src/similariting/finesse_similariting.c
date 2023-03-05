#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

//Together used by finess superfeature and flatfeature
pthread_mutex_t fineness_sufeature_tab_mutex;

void fineness_similariting_init(){
	fineness_sufeature_tab = g_hash_table_new(g_int64_hash, g_chunk_feature_equal);
	pthread_mutex_init(& fineness_sufeature_tab_mutex, 0);
}

/*Insert super features into the hash table*/
void fineness_insert_sufeature(struct chunk* c){
	for (int i = 0; i < FINESSE_SF_NUM; i++) {
		
		GSequence *tq = g_hash_table_lookup(fineness_sufeature_tab, &(c->fea[i]));
		if(!tq) {
			tq = g_sequence_new(NULL);
			g_hash_table_replace(fineness_sufeature_tab, &(c->fea[i]), tq);
		}
		//insert c into the queue of the hash table
		g_sequence_prepend(tq, c);
	}
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
struct chunk* fineness_similariting(struct chunk* c){

	unsigned char* ret = NULL;
	for (int i = 0; i < FINESSE_SF_NUM; i++) {
		GSequence *tq = (GSequence*)g_hash_table_lookup(fineness_sufeature_tab, &(c->fea[i]));
		if (tq) {
			struct chunk* basec = (struct chunk*)g_sequence_get(g_sequence_get_begin_iter(tq));
//			printf("find simi: input chunk:%lx, basec:%lx, basec->data:%lx, basec->fp:%lx, basec->size:%ld\n", 
//					c, basec, basec->data, basec->fp, basec->size);
			ret = basec;
			goto retPoint;
		}
	}

	/*only if the chunk is unique, add the chunk into sufeature table*/
	fineness_insert_sufeature(c);

retPoint:
	return ret;
}

struct chunk* fineness_similariting_flatFea(struct chunk* c){

	struct chunk* ret = most_match_similariting(c, FINESSE_FEATURE_NUM, fineness_sufeature_tab);
	return ret;
}