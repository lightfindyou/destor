#ifndef __DEEPSKETCH_SIMILARITING__
#define __DEEPSKETCH_SIMILARITING__

#include "./NGT/Index.h"
#include "../../featuring/deepsketch/deepsketch_featuring.h"
#include "deepsketch_similariting_c.h"

class ANN {
   private:
    int ANN_SEARCH_CNT, LINEAR_SIZE, NUM_THREAD, THRESHOLD;
    std::vector<MYHASH> linear;
    std::unordered_map<MYHASH, std::vector<int>> hashtable;
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
    int request(MYHASH h);
    void insert(MYHASH h, int label);
};

#endif  //__DEEPSKETCH_SIMILARITING__