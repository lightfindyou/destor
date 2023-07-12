#include "deepsketch_similariting.h"

#include <iostream>

#include "../../featuring/deepsketch/deepsketch_featuring_c.h"
#include "deepsketch_similariting_c.h"
#include "./hnswlib/hnswlib.h"

// The max size of dataset GB
#define MAXDATASETSIZE 100ULL
#define MINICHUNKSIZE 4096ULL
#define MAXELEMENTS MAXDATASETSIZE*1024*1024*1024/MINICHUNKSIZE

hnswlib::HierarchicalNSW<float>* alg_hnsw;

extern "C" void deepsketch_similariting_hnsw_init() {
    int M = 288;                 // Tightly connected with internal dimensionality of the data
                                // strongly affects the memory consumption
    int ef_construction = 200;  // Controls index search speed/build speed tradeoff
    // Initing index
//    hnswlib::L2Space space(dim);
//    hnswlib::HammingSpace space(dim);
//    hnswlib::AsymHammingSpace space(dim);
    hnswlib::AsymWeightHammingSpace space(DEEPSKETCH_HASH_SIZE/8/sizeof(unsigned long long));

    alg_hnsw = new hnswlib::HierarchicalNSW<float>(&space, MAXELEMENTS, M, ef_construction);
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
 */
extern "C" void deepsketch_similariting_hnsw(struct chunk* c) {
    struct chunk* ret = NULL;
    feature* fea = c->fea;

	std::priority_queue<std::pair<float, hnswlib::labeltype>> result =
						 alg_hnsw->searchKnn(fea, 1);
	hnswlib::labeltype dcomp_ann_ref;
	if(!result.empty()){
		ret = (struct chunk*)result.top().second;
        g_queue_push_tail(c->basechunk, ret);
	}
	alg_hnsw->addPoint(fea, (unsigned long long)c);

    return;
}