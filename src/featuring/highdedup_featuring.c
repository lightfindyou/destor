#include <xxhash.h>
#include "featuring.h"
#include "../similariting/similariting.h"
#include "../destor.h"

void highdedup_featuring(unsigned char* buf, int size, struct chunk* c){

	sufeature* superfeature = c->fea;
	for (int i = 0; i < HIGHDEDUP_FEATURE_NUM; ++i) superfeature[i] = 0;

	c->feaNum = gear_max_highdedup(buf, size, superfeature, HIGHDEDUP_FEATURE_NUM);
}