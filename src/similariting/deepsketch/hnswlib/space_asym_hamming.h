#pragma once
#include "hnswlib.h"

namespace hnswlib {

static float
AsymHamming(const void *pVect1v, const void *pVect2v, const void *qty_ptr) {
    unsigned int *pVect1 = (unsigned int *) pVect1v;
    unsigned int *pVect2 = (unsigned int *) pVect2v;

    float res = 0;
    for (size_t i = 0; i < FEALEN; i++) {
        unsigned int t = ((*pVect1) ^ (*pVect2)) & (*pVect1);
        pVect1++;
        pVect2++;
        res += __builtin_popcount(t);
    }
    return (res);
}

class AsymHammingSpace : public SpaceInterface<float> {
    DISTFUNC<float> fstdistfunc_;
    size_t data_size_;
    size_t dim_;

 public:
    AsymHammingSpace(size_t dim) {
        fstdistfunc_ = AsymHamming;
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

    ~AsymHammingSpace() {}
};
}  // namespace hnswlib
