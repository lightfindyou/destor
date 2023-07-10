#include "deepsketch_similariting.h"

#include <iostream>

#include "../../featuring/deepsketch/deepsketch_featuring_c.h"
#include "deepsketch_similariting_c.h"

std::string indexPath = "ngtindex";
NGT::Property property;
ANN* ann;

extern "C" void deepsketch_similariting_init() {
    property.dimension = DEEPSKETCH_HASH_SIZE / 8;
    property.objectType = NGT::ObjectSpace::ObjectType::Uint8;
    property.distanceType =
        NGT::Index::Property::DistanceType::DistanceTypeHamming;
    NGT::Index::create(indexPath, property);
    NGT::Index index(indexPath);
    ann =
        new ANN(20, 128, 16, destor.deepsketchANNThreshold, &property, &index);
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
 */
extern "C" void deepsketch_similariting(struct chunk* c) {
    MYHASH fea = (MYHASH)c->fea;
    struct chunk* ret = NULL;
    ret = ann->request(fea);
    if(ret){
        g_queue_push_tail(c->basechunk, ret);
    }
	ann->insert(fea, c);
    
    return;
}

// search the nearest point in ANN
struct chunk* ANN::request(MYHASH h) {
    int dist = 999;
    struct chunk* ret = NULL;

    // scan list
    for (int i = feaCache.size() - 1; i >= 0; --i) {
        int nowdist = (feaCache[i] ^ h).count();  // hammin distance
        if (dist > nowdist) {
            dist = nowdist;
            ret = fea2ChunkTable[feaCache[i]].back();
        }
    }

    std::vector<uint8_t> query;
    for (int i = 0; i < property->dimension; ++i) {
        //Change the serached hash into uint array
        query.push_back((uint8_t)((h << (DEEPSKETCH_HASH_SIZE - 8 * i - 8)) >>
                                  (DEEPSKETCH_HASH_SIZE - 8))
                            .to_ulong());
    }

    NGT::SearchQuery sc(query);
    NGT::ObjectDistances objects;
    sc.setResults(&objects);
    sc.setSize(this->ANN_SEARCH_CNT);
    sc.setEpsilon(0.2);

    index->search(sc);  // here search the result
    // process the search result
    for (int i = 0; i < objects.size(); ++i) {  // what is the size of object?
                                                // 0?
        int nowdist = objects[i].distance;

        if (dist > nowdist) {  // find better result
            MYHASH now;

            NGT::ObjectSpace& objectSpace = index->getObjectSpace();
            uint8_t* object =
                static_cast<uint8_t*>(objectSpace.getObject(objects[i].id));
            for (int j = 0; j < objectSpace.getDimension(); ++j) {
                for (int k = 0; k < 8; ++k) {
                    if (object[j] & (1 << k)) {
                        //Copy the hash code into MYHASH now
                        now.flip(8 * j + k);
                    }
                }
            }
            dist = nowdist;
            ret = fea2ChunkTable[now].back();
        } else if (dist == nowdist) { /** found same result, choose the
                                         newer one, but why use newer one? */
            MYHASH now;

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
            struct chunk* nowindex = fea2ChunkTable[now].back();

            if (nowindex > ret) ret = nowindex;
        }
    }

    if (dist <= THRESHOLD)
        return ret;
    else
        return NULL;
}

void ANN::insert(MYHASH h, struct chunk* c) {
    //For each unique chunk, push back the c
    fea2ChunkTable[h].push_back(c);
    if (fea2ChunkTable.count(h)) { return; }

    feaCache.push_back(h);

    if (feaCache.size() == FEACACHE_SIZE) {
        for (int i = 0; i < feaCache.size(); ++i) {
            std::vector<uint8_t> query;
            for (int j = 0; j < property->dimension; ++j) {
                //Get the lowest j 8bit into ulong
                query.push_back((uint8_t)((feaCache[i] << (DEEPSKETCH_HASH_SIZE -
                                                         8 * j - 8)) >>
                                          (DEEPSKETCH_HASH_SIZE - 8))
                                    .to_ulong());
            }
            index->append(query);
        }
        index->createIndex(NUM_THREAD);

        feaCache.clear();
    }
}