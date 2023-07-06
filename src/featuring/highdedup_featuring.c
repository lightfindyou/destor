#include <xxhash.h>
#include "featuring.h"
#include "../similariting/similariting.h"
#include "../destor.h"

void highdedup_featuring(unsigned char* buf, int size, struct chunk* c){

	sufeature* superfeature = c->fea;
	memset(superfeature, 0x0, sizeof(sufeature)*HIGHDEDUP_FEATURE_NUM);

//	c->feaNum = gear_max_highdedup_12fea_64B_max(buf, size, superfeature,
//				 HIGHDEDUP_FEATURE_NUM, HIGHDEDUP_FEATURE_MASK);
/**The feature phase has been merged with chunking phase*/
//	c->feaNum = gear_max_highdedup_32fea_16B_max(buf, size, superfeature,
//				 HIGHDEDUP_FEATURE_NUM, HIGHDEDUP_FEATURE_MASK, c);
//	c->feaNum = gear_highdedup_max(buf, size, superfeature,
//				 destor.featureNum, destor.featureLenMask);
//	c->feaNum = gear_max_highdedup_32fea_16B_xxhash(buf, size, superfeature,
//				 HIGHDEDUP_FEATURE_NUM, HIGHDEDUP_FEATURE_MASK);
	if(destor.compressSelf){

		int upLimit = c->feaNum - 1;
		for(int i = 0; i < upLimit; i++){
			sufeature cmp = superfeature[i+1];
			for(int j = 0; j <= i; j++){
				if(cmp == superfeature[j]){
					UNSET_CHUNK(c, CHUNK_UNIQUE);
					SET_CHUNK(c, CHUNK_SIMILAR);
					break;
				}
			}
		}

	}
}

void highdedup_featuring_fsc(unsigned char* buf, int size, struct chunk* c){

	sufeature* superfeature = c->fea;
	memset(superfeature, 0x0, sizeof(sufeature)*HIGHDEDUP_FEATURE_NUM);

	c->feaNum = highdedup_32fea_16B_FSC(buf, size, superfeature,
				 HIGHDEDUP_FEATURE_NUM);
}