#include <xxhash.h>
#include "../destor.h"
#include "featuring.h"
#define SF_NUM 4

static void* ntransfrom_featuring(unsigned char* buf, int size, unsigned char* fea){
	sufeature* superfeature = (sufeature)fea;
	for (int i = 0; i < SF_NUM; ++i) superfeature[i] = 0;

	rabin_ntransform(buf, size, superfeature, SF_NUM);
}