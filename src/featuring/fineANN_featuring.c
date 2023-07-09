#include <xxhash.h>
#include "../destor.h"
#include "featuring.h"

int fineANN_featuring(unsigned char* buf, int size, struct chunk* c){

	fineANN_t* fea = c->fea;
	memset(fea, 0, FINEANN_FEATURE_LEN);

	gear_fineANN(buf, size, fea, ODESS_FEATURE_NUM);

	return 1;
}