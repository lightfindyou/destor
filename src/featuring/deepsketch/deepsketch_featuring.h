#ifndef __DEEPSKETCH_FEATURING_
#define __DEEPSKETCH_FEATURING_

#include <torch/script.h>
#include "deepsketch_featuring_c.h"
#include "../../destor.h"

typedef std::bitset<DEEPSKETCH_HASH_SIZE> MYHASH;
typedef unsigned char deepsketchFeaUchar[DEEPSKETCH_HASH_SIZE/8];

class NetworkHash {
   private:
    int batch_size;
    torch::jit::script::Module module;
    float *data;
    bool *memout;
    struct chunk** index;
    int cnt;

   public:
    NetworkHash(int batch_size, char *module_name) {
        this->batch_size = batch_size;
        this->module = torch::jit::load(module_name);
        this->module.to(at::kCUDA);
        this->module.eval();
        this->data = new float[batch_size * DEEPSKETCH_BLOCK_SIZE * 2];
        this->memout = new bool[batch_size * DEEPSKETCH_HASH_SIZE];
        this->index = new struct chunk*[batch_size];
        this->cnt = 0;
    }
    ~NetworkHash() {
        delete[] this->data;
        delete[] this->memout;
        delete[] this->index;
    }
    bool push(char *ptr, int size, struct chunk* c);
    int request();
    void setFeature(bool* boolArray, unsigned char* fea);
};

extern NetworkHash* network;




#endif  //__DEEPSKETCH_FEATURING_