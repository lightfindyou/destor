#include <xxhash.h>
#include "featuring.h"
#include "../similariting/similariting.h"
#include "../destor.h"

void highdedup_featuring(unsigned char* buf, int size, struct chunk* c){

	sufeature* superfeature = c->fea;
	memset(superfeature, 0x0, sizeof(sufeature)*HIGHDEDUP_FEATURE_NUM);

//	c->feaNum = gear_max_highdedup_12fea_64B_max(buf, size, superfeature,
//				 HIGHDEDUP_FEATURE_NUM, HIGHDEDUP_FEATURE_MASK);
	c->feaNum = gear_max_highdedup_32fea_16B_max(buf, size, superfeature,
				 HIGHDEDUP_FEATURE_NUM, HIGHDEDUP_FEATURE_MASK, c);
//	c->feaNum = gear_highdedup_max(buf, size, superfeature,
//				 destor.featureNum, destor.featureLenMask);
//	c->feaNum = gear_max_highdedup_32fea_16B_xxhash(buf, size, superfeature,
//				 HIGHDEDUP_FEATURE_NUM, HIGHDEDUP_FEATURE_MASK);
}

void highdedup_featuring_fsc(unsigned char* buf, int size, struct chunk* c){

	sufeature* superfeature = c->fea;
	memset(superfeature, 0x0, sizeof(sufeature)*HIGHDEDUP_FEATURE_NUM);

	c->feaNum = highdedup_32fea_16B_FSC(buf, size, superfeature,
				 HIGHDEDUP_FEATURE_NUM);
}