#pragma once
#include "hnswlib.h"

namespace hnswlib {

static float
AsymWeightHamming(const void *pVect1v, const void *pVect2v, const void *qty_ptr) {
    int *pVect1 = (int *) pVect1v;
    int *pVect2 = (int *) pVect2v;

    float res = 0;
    for (size_t i = 0; i < FEALEN; i++) {
        int t = ((*pVect1) ^ (*pVect2)) & (*pVect1);

        int baseOffset = FEALEN + i*sizeof(int);
        for(size_t j = 0; (j < 8*sizeof(int)) && t ; j++){
            if(t<0){ res += pVect1[baseOffset + j]; }
            t <<= 1;
        }

        pVect1++;
        pVect2++;
    }
    return res;
}

class AsymWeightHammingSpace : public SpaceInterface<float> {
    DISTFUNC<float> fstdistfunc_;
    size_t data_size_;
    size_t dim_;

 public:
    AsymWeightHammingSpace(size_t dim) {
        fstdistfunc_ = AsymWeightHamming;
        dim_ = dim;
        data_size_ = dim * sizeof(unsigned int);
    }

    size_t get_data_size() {
        return data_size_;
    }

    DISTFUNC<float> get_dist_func() {
        return fstdistfunc_;
    }

    void *get_dist_func_param() {
        return &dim_;
    }

    ~AsymWeightHammingSpace() {}
};
}  // namespace hnswlib
