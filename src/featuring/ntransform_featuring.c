#include <xxhash.h>
#include "../destor.h"
#include "featuring.h"

void ntransform_featuring(unsigned char* buf, int size, struct chunk* c){
	feature fea[NTRANSFORM_FEATURE_NUM];
	sufeature* superfeature = c->fea;
	memset(fea, 0, sizeof(feature)*NTRANSFORM_FEATURE_NUM);

//	printf("buf:%x, size:%d, fea:%x\n", buf, size, fea);
	rabin_ntransform(buf, size, fea, NTRANSFORM_FEATURE_NUM);

	for (int i = 0; i < NTRANSFORM_SF_NUM; ++i) {
		feature temp[NTRANSFORM_FEATURE_NUM / NTRANSFORM_SF_NUM];
		int start = i*NTRANSFORM_FEATURE_NUM/NTRANSFORM_SF_NUM;
		for (int j = 0; j < NTRANSFORM_FEATURE_NUM / NTRANSFORM_SF_NUM; j++) {
			temp[j] = fea[start + j];
		}
		superfeature[i] = XXH64(temp, sizeof(feature) * NTRANSFORM_FEATURE_NUM / NTRANSFORM_SF_NUM, 0);
	}
	c->feaNum = NTRANSFORM_SF_NUM;
}