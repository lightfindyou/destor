#include <xxhash.h>
#include "featuring.h"
#include "../similariting/similariting.h"
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

void finesse_featuring(unsigned char* buf, int size, struct chunk* c){
	feature curFea[FINESSE_FEATURE_NUM];
	sufeature* superfeature = c->fea;
	memset(curFea, 0, sizeof(feature)*FINESSE_FEATURE_NUM);

	int subchs = size/FINESSE_FEATURE_NUM;

	for (int i = 0; i < FINESSE_FEATURE_NUM; ++i) {
		int64_t fp = 0;
		int len = subchs;
		if(i== (FINESSE_FEATURE_NUM - 1)){
			len = size - (subchs * i);
		}

		rabin_finesse(&buf[subchs*i], len, &curFea[i]);
	}

	for (int i = 0; i < FINESSE_FEATURE_NUM / FINESSE_SF_NUM; ++i) {
		//sort in descending
		qsort(curFea, FINESSE_FEATURE_NUM, sizeof(feature), compar);
	}

	for (int i = 0; i < FINESSE_SF_NUM; ++i) {
		feature temp[FINESSE_FEATURE_NUM / FINESSE_SF_NUM];
		for (int j = 0; j < FINESSE_FEATURE_NUM / FINESSE_SF_NUM; ++j) {
			temp[j] = curFea[j * FINESSE_SF_NUM + i];
		}
		superfeature[i] = XXH64(temp, sizeof(feature) * FINESSE_FEATURE_NUM / FINESSE_SF_NUM, 0);
	}

//	printf("new chunk: %lx, chunk->data: %lx, fp addr: %lx, chunk->size: %d, feature: %6lu %6lu %6lu %6lu\n",
//			 c, c->data, c->fp, c->size,
//			 c->fea[0] & 0x3f, c->fea[1] & 0x3f, c->fea[2] & 0x3f, c->fea[3] & 0x3f);
}