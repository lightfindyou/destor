#include <iostream>
#include <vector>
#include <set>
#include <bitset>
#include <map>
#include <cmath>
#include <algorithm>
#include "deepsketch_model.h"

extern "C" {
#include "../featuring.h"
#include "../../destor.h"
}

#define INF 987654321
using namespace std;

void deepsketch_modelInit(char * modelPath){
	network = NetworkHash(256, modelPath);
}

void deepsketch_getHash(struct chunk* c, int chunkLen, char* hash){
	network.push(c->data, i, c)
	vector<pair<DEEPSKETCH_HASH, DEEPSKETCHCHUNK>> netHash = network.request();
	memcpy(hash, netHash.first, DEEPSKETCH_HASH_SIZE/8);
}

