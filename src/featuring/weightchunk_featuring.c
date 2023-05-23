#include <xxhash.h>
#include "../destor.h"
#include "featuring.h"

void weightchunk_featuring(unsigned char* buf, int size, struct chunk* c){
	sufeature* superfeature = c->fea;
	memset(superfeature, 0, sizeof(feature)*MAX_FEA_SIZE);

	c->feaNum = weightchunkGearing(buf, size, superfeature);
}