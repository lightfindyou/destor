#include <xxhash.h>
#include "../destor.h"
#include "featuring.h"

int odess_featuring(unsigned char* buf, int size, struct chunk* c){
	feature fea[ODESS_FEATURE_NUM];
	sufeature* superfeature = c->fea;
	memset(fea, 0, sizeof(feature)*ODESS_FEATURE_NUM);

	gear_odess(buf, size, fea, ODESS_FEATURE_NUM);

	for (int i = 0; i < ODESS_SF_NUM; ++i) {
		feature temp[ODESS_FEATURE_NUM / ODESS_SF_NUM];
		int start = i*ODESS_FEATURE_NUM/ODESS_SF_NUM;
		for (int j = 0; j < ODESS_FEATURE_NUM / ODESS_SF_NUM; j++) {
			temp[j] = fea[start + j];
		}
		superfeature[i] = XXH64(temp, sizeof(feature) * ODESS_FEATURE_NUM / ODESS_SF_NUM, 0);
	}
	c->feaNum = ODESS_SF_NUM;

	return 1;
}

int odess_featuring_flatFea(unsigned char* buf, int size, struct chunk* c){
	feature* fea = c->fea;
	memset(fea, 0, sizeof(feature)*ODESS_FEATURE_NUM);

	gear_odess(buf, size, fea, ODESS_FEATURE_NUM);
	c->feaNum = ODESS_FEATURE_NUM;

	return 1;
}