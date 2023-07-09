#include <xxhash.h>
#include "../destor.h"
#include "featuring.h"

int statis_featuring(unsigned char* buf, int size, struct chunk* c){
	sufeature* superfeature = c->fea;
	memset(superfeature, 0, sizeof(feature)*MAX_FEA_SIZE);

	gear_statis(buf, size, superfeature, STATIS_FEATURE_NUM);
	c->feaNum = STATIS_FEATURE_NUM;

	return 1;
}