#pragma once
#include "hnswlib.h"

namespace hnswlib {

static float
AsymWeightHamming(const void *pVect1v, const void *pVect2v, const void *qty_ptr) {
    unsigned char *pVect1 = (unsigned char *) pVect1v;
    unsigned char *pVect2 = (unsigned char *) pVect2v;

    float res = 0;
    for (size_t i = 0; i < FEALEN; i++) {
        unsigned char t = ((pVect1[i]) ^ (pVect2[i])) & (pVect1[i]);

        int baseOffset = FEALEN + i*sizeof(unsigned char);
        for(size_t j = 0; (j < 8*sizeof(unsigned char)) && t ; j++){
            if(t&0x80){ res += pVect1[baseOffset + j]; }
            t <<= 1;
        }
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
