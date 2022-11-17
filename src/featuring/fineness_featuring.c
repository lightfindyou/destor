#include <xxhash.h>
#include "featuring.h"
#include "../destor.h"
#define FEATURE_NUM 12
#define SF_NUM 4

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

static void* finesse_featuring(unsigned char* buf, int size, unsigned char* fea){
	feature curFea[FEATURE_NUM];
	sufeature* superfeature = (sufeature)fea;

	int subchs = size/FEATURE_NUM;

	for (int i = 0; i < FEATURE_NUM; ++i) curFea[i] = 0;
	for (int i = 0; i < SF_NUM; ++i) superfeature[i] = 0;

	for (int i = 0; i < FEATURE_NUM; ++i) {
		int64_t fp = 0;
		int len = subchs;
		if(i== (FEATURE_NUM - 1)){
			len = size - (subchs * i);
		}

		curFea[i] = rabin_maxfp(buf[subchs*i], len);
	}

	for (int i = 0; i < FEATURE_NUM / SF_NUM; ++i) {
		//sort in descending
		qsort(curFea, FEATURE_NUM, sizeof(feature), compar);
	}

	for (int i = 0; i < SF_NUM; ++i) {
		feature temp[FEATURE_NUM / SF_NUM];
		for (int j = 0; j < FEATURE_NUM / SF_NUM; ++j) {
			temp[j] = curFea[j * SF_NUM + i];
		}
		superfeature[i] = XXH64(temp, sizeof(feature) * FEATURE_NUM / SF_NUM, 0);
	}

	return -1;
}