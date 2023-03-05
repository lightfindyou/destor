#include <torch/script.h>

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "../xxhash.h"
#include "./NGT/Index.h"
#include "../../deepsketch.h"

ANN ann;

class ANN {
   private:
    int ANN_SEARCH_CNT, LINEAR_SIZE, NUM_THREAD, THRESHOLD;
    std::vector<DEEPSKETCHHASH> linear;
    std::unordered_map<DEEPSKETCHHASH, std::vector<DEEPSKETCHCHUNK>> hashtable;
    NGT::Property* property;
    NGT::Index* index;

   public:
    ANN(int ANN_SEARCH_CNT, int LINEAR_SIZE, int NUM_THREAD, int THRESHOLD,
        NGT::Property* property, NGT::Index* index) {
        this->ANN_SEARCH_CNT =
            ANN_SEARCH_CNT;  // The number of candidates extract from ANN class
        this->LINEAR_SIZE = LINEAR_SIZE;  // Size of linear buffer
        this->NUM_THREAD = NUM_THREAD;
        this->THRESHOLD = THRESHOLD;
        this->property = property;
        this->index = index;
    }
    int request(DEEPSKETCHHASH h);
    void insert(DEEPSKETCHHASH h, int c);
};

// search the nearest point in ANN
DEEPSKETCHCHUNK ANN::request(DEEPSKETCHHASH h) {
    int dist = 999;
    DEEPSKETCHCHUNK ret = NULL;

    // scan list
    for (int i = linear.size() - 1; i >= 0; --i) {
        int nowdist = (linear[i] ^ h).count();  // hammin distance
        if (dist > nowdist) {
            dist = nowdist;
            ret = hashtable[linear[i]].back();
        }
    }

    std::vector<uint8_t> query;
    // change the searched hash into uint array
    for (int i = 0; i < property->dimension; ++i) {
        query.push_back(
            (uint8_t)((h << (DEEPSKETCH_HASH_SIZE - 8 * i - 8)) >> (DEEPSKETCH_HASH_SIZE - 8))
                .to_ulong());
    }

    NGT::SearchQuery sc(query);
    NGT::ObjectDistances objects;
    sc.setResults(&objects);
    sc.setSize(this->ANN_SEARCH_CNT);
    sc.setEpsilon(0.2);

    index->search(sc);  // here search the result
    // process the search result
    for (int i = 0; i < objects.size(); ++i) {  
        int nowdist = objects[i].distance;

        if (dist > nowdist) {  // find better result
            DEEPSKETCHHASH now;

            NGT::ObjectSpace& objectSpace = index->getObjectSpace();
            uint8_t* object =
                static_cast<uint8_t*>(objectSpace.getObject(objects[i].id));
            for (int j = 0; j < objectSpace.getDimension(); ++j) {
                memcpy(now, object, DEEPSKETCH_HASH_SIZE/8);
//                for (int k = 0; k < 8; ++k) {
//                    if (object[j] & (1 << k)) {
//                        // copy the hash code into DEEPSKETCHHASH now
//                        now.flip(8 * j + k);
//                    }
//                }
            }
            dist = nowdist;
            ret = hashtable[now].back();
        } else if (dist == nowdist) { /** find same result,
                                        choose the one with bigger
                                        index, but why use bigger index? */
            DEEPSKETCHHASH now;

            NGT::ObjectSpace& objectSpace = index->getObjectSpace();
            uint8_t* object =
                static_cast<uint8_t*>(objectSpace.getObject(objects[i].id));
            for (int j = 0; j < objectSpace.getDimension(); ++j) {
                for (int k = 0; k < 8; ++k) {
                    if (object[j] & (1 << k)) {
                        now.flip(8 * j + k);
                    }
                }
            }
            int nowindex = hashtable[now].back();

            if (nowindex > ret) ret = nowindex;
        }
    }

    if (dist <= THRESHOLD){
        return ret;
    } else{
        return NULL;
    }
}

void ANN::insert(DEEPSKETCHHASH h, DEEPSKETCHCHUNK c) {
    if (hashtable.count(h)) {
        hashtable[h].push_back(c);
        return;
    }

    hashtable[h].push_back(c);
    linear.push_back(h);

    if (linear.size() == LINEAR_SIZE) {
        for (int i = 0; i < linear.size(); ++i) {
            std::vector<uint8_t> query;
            //transfer hash into multiple unsigned long and store in query
            for (int j = 0; j < property->dimension; ++j) {
                query.push_back(
                    (uint8_t)((linear[i] << (DEEPSKETCH_HASH_SIZE - 8 * j - 8)) >>
                              (DEEPSKETCH_HASH_SIZE - 8))
                        .to_ulong());
            }
            index->append(query);
        }
        index->createIndex(NUM_THREAD);

        linear.clear();
    }
}
