#include <xxhash.h>
#include "../destor.h"
#include "featuring.h"

void odess_featuring(unsigned char* buf, int size, struct chunk* c){
	feature fea[ODESS_FEATURE_NUM];
	sufeature* superfeature = c->fea;
	for (int i = 0; i < ODESS_SF_NUM; ++i) fea[i] = 0;

	rabin_ntransform(buf, size, fea, ODESS_FEATURE_NUM);

	for (int i = 0; i < ODESS_SF_NUM; ++i) {
		feature temp[ODESS_FEATURE_NUM / ODESS_SF_NUM];
		int start = i*ODESS_FEATURE_NUM/ODESS_SF_NUM;
		for (int j = 0; j < ODESS_FEATURE_NUM / ODESS_SF_NUM; j++) {
			temp[j] = fea[start + j];
		}
		superfeature[i] = XXH64(temp, sizeof(feature) * FINESSE_FEATURE_NUM / FINESSE_SF_NUM, 0);
	}
}