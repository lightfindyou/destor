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

void deepsketch_ANN_init(){

	string indexPath = "ngtindex";
	NGT::Property property;
	property.dimension = DEEPSKETCH_HASH_SIZE / 8;
	property.objectType = NGT::ObjectSpace::ObjectType::Uint8;
	property.distanceType = NGT::Index::Property::DistanceType::DistanceTypeHamming;
	NGT::Index::create(indexPath, property);
	NGT::Index index(indexPath);

	ann = ANN(20, 128, 16, threshold, &property, &index);
}

struct chunk* deepsketch_ANN_similariting(struct chunk* c){

	struct chunk* ret = ann.request(c->fea);

	if (!ret) {
		ann.insert(c->fea, c);
	}

	return ret;
}