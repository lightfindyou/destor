#include <xxhash.h>
#include "../destor.h"

static void* fineness_featuring(unsigned char* buf, int size, unsigned char* fea){
	for (int i = 0; i < FEATURE_NUM; ++i) feature[i] = 0;
	for (int i = 0; i < SF_NUM; ++i) superfeature[i] = 0;

	for (int i = 0; i < FEATURE_NUM; ++i) {
		int64_t fp = 0;

		for (int j = subchunkIndex[i]; j < subchunkIndex[i] + W; ++j) {
			fp *= A;
			fp += (unsigned char)buf[j];
			fp %= MOD;
		}

		for (int j = subchunkIndex[i]; j < subchunkIndex[i + 1] - W + 1; ++j) {
			if (fp > feature[i]) feature[i] = fp;

			fp -= (buf[j] * Apower) % MOD;
			while (fp < 0) fp += MOD;
			if (j != subchunkIndex[i + 1] - W) {
				fp *= A;
				fp += buf[j + W];
				fp %= MOD;
			}
		}
	}

	for (int i = 0; i < FEATURE_NUM / SF_NUM; ++i) {
		std::sort(feature + i * SF_NUM, feature + (i + 1) * SF_NUM);
	}

	for (int i = 0; i < SF_NUM; ++i) {
		uint64_t temp[FEATURE_NUM / SF_NUM];
		for (int j = 0; j < FEATURE_NUM / SF_NUM; ++j) {
			temp[j] = feature[j * SF_NUM + i];
		}
		superfeature[i] = XXH64(temp, sizeof(uint64_t) * FEATURE_NUM / SF_NUM, 0);
	}

	uint32_t r = full_uint32_t(gen2) % SF_NUM;
	for (int i = 0; i < SF_NUM; ++i) {
		int index = (r + i) % SF_NUM;
		if (sfTable[index].count(superfeature[index])) {
			return sfTable[index][superfeature[index]].back();
		}
	}
	return -1;
}