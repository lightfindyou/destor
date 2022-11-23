#include <xxhash.h>
#include "../../src/destor.h"
#include "../../src/similariting/similariting.h"

gboolean g_feature_equal(feature* fp1, feature* fp2) {
	return !memcmp(fp1, fp2, sizeof(feature));
}

void fineness_similariting_init(){
	fineness_sufeature_tab = g_hash_table_new(g_int64_hash, g_feature_equal);
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
//			printf("similar with: %lx\n", basefp);
			return basefp;
		}
	}

//	printf("Not found similar, insert\n");
	/*only if the chunk is unique, add the chunk into sufeature table*/
	fineness_insert_sufeature(c);
	return 0;
}

#define CHUNK_NUM (5)
void main(){
	struct chunk c[CHUNK_NUM];
	memset(c, 0, sizeof(struct chunk)*CHUNK_NUM);
	for(int i=0; i<CHUNK_NUM; i++){
		c[i].fp[0] = i+10;
		for(int j=0; j<4; j++){
			c[i].fea[j] = i*CHUNK_NUM + j;
		}
	}
	c[1].fea[1] = c[0].fea[0];
	c[2].fea[2] = c[1].fea[3];
	c[3].fea[3] = c[2].fea[3];
	c[4].fea[2] = c[3].fea[2];

	for(int i=0; i<CHUNK_NUM; i++){
		printf("c[%d]: %lx, c[%d]->fp: %lx, c[%d]->fea: %d %d %d %d\n", 
			i, &c[i], i, &c[i].fp, i, c[i].fea[0], c[i].fea[1], c[i].fea[2], c[i].fea[3]);
	}
	fineness_similariting_init();
	for(int i = 0; i<CHUNK_NUM; i++){
		printf("c[%d] similar with: %lx\n",
				i, fineness_similariting(&c[i]));
	}

}