#pragma once
#include "hnswlib.h"

namespace hnswlib {

static float
Hamming(const void *pVect1v, const void *pVect2v, const void *qty_ptr) {
    unsigned char *pVect1 = (unsigned char *) pVect1v;
    unsigned char *pVect2 = (unsigned char *) pVect2v;
    size_t qty = *((size_t *) qty_ptr);

    float res = 0;
    for (size_t i = 0; i < qty; i++) {
        unsigned int t = (*pVect1) ^ (*pVect2);
        pVect1++;
        pVect2++;
        res += __builtin_popcount(t);
    }
    return (res);
}

class HammingSpace : public SpaceInterface<float> {
    DISTFUNC<float> fstdistfunc_;
    size_t data_size_;
    size_t dim_;

 public:
    HammingSpace(size_t dim) {
        fstdistfunc_ = Hamming;
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

    ~HammingSpace() {}
};
}  // namespace hnswlib
