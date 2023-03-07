#include <iostream>
#include <vector>
#include <set>
#include <bitset>
#include <map>
#include <cmath>
#include <algorithm>
#include "deepsketch_ANN.h"
#include "xxhash.h"

using namespace std;

ANN* ann;

void deepsketch_ANN_init(){

	string indexPath = "ngtindex";
	NGT::Property property;
	property.dimension = DEEPSKETCH_HASH_SIZE / 8;
	property.objectType = NGT::ObjectSpace::ObjectType::Uint8;
	property.distanceType = NGT::Index::Property::DistanceType::DistanceTypeHamming;
	NGT::Index::create(indexPath, property);
	NGT::Index index(indexPath);
	int threshold = 32;
	int threadNum = 16;
	int cacheSize = 128;

	ann = new ANN(20, cacheSize, threadNum, threshold, &property, &index);
}

struct chunk* deepsketch_ANN_similariting(struct chunk* c){

	DEEPSKETCHHASH fea;
	memcpy(fea.data(), c->fea, DEEPSKETCH_HASH_SIZE/8);

	struct chunk* ret = (struct chunk*)ann->request(fea);

	if (!ret) {
		ann->insert(fea, (DEEPSKETCHCHUNK)c);
	}

	return ret;
}