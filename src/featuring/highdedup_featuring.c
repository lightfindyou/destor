#include <xxhash.h>
#include "featuring.h"
#include "similariting.h"
#include "../destor.h"

int compar(const void * a, const void * b){
	feature* f1 = a;
	feature* f2 = b;

	if(f1<f2){
		return 1;
	}else if(f1 > f2){
		return -1;
	}

	return 0;
}

static void highdedup_featuring(unsigned char* buf, int size, struct chunk* c){

	sufeature* superfeature = c->fea;
	for (int i = 0; i < FINESSE_SF_NUM; ++i) superfeature[i] = 0;

	c->feaNum = gear_max_highdedup(buf, size, superfeature, HIGHDEDUP_FEATURE_NUM);
}