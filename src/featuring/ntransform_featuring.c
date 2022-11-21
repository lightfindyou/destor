#include <xxhash.h>
#include "../destor.h"
#include "featuring.h"

void ntransform_featuring(unsigned char* buf, int size, struct chunk* c){
	feature fea[NTRANSFORM_FEATURE_NUM];
	sufeature* superfeature = c->fea;
	for (int i = 0; i < NTRANSFORM_SF_NUM; ++i) fea[i] = 0;

	rabin_ntransform(buf, size, fea, NTRANSFORM_SF_NUM);

	for (int i = 0; i < FINESSE_SF_NUM; ++i) {
		feature temp[FINESSE_FEATURE_NUM / FINESSE_SF_NUM];
		int start = i*FINESSE_FEATURE_NUM/FINESSE_SF_NUM;
		for (int j = 0; j < FINESSE_FEATURE_NUM / FINESSE_SF_NUM; j++) {
			temp[j] = fea[start + j];
		}
		superfeature[i] = XXH64(temp, sizeof(feature) * FINESSE_FEATURE_NUM / FINESSE_SF_NUM, 0);
	}
}