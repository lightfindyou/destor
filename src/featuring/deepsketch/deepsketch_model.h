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

class NetworkHash {
   private:
    int BATCH_SIZE;
    torch::jit::script::Module module;
    float* data;
    bool* memout;
    DEEPSKETCHCHUNK* index;
    int cnt;

   public:
    NetworkHash(int BATCH_SIZE, char* module_name) {
        this->BATCH_SIZE = BATCH_SIZE;
        this->module = torch::jit::load(module_name);
        this->module.to(at::kCUDA);
        this->module.eval();
        this->data = new float[BATCH_SIZE * BLOCK_SIZE];
        this->memout = new bool[BATCH_SIZE * DEEPSKETCH_HASH_SIZE];
        this->index = new DEEPSKETCHCHUNK[BATCH_SIZE];
        this->cnt = 0;
    }
    ~NetworkHash() {
        delete[] this->data;
        delete[] this->memout;
        delete[] this->index;
    }
    bool push(unsigned char* ptr, DEEPSKETCHCHUNK id);
    std::vector<std::pair<DEEPSKETCHHASH, DEEPSKETCHCHUNK>> request();
};

bool NetworkHash::push(unsigned char* ptr, DEEPSKETCHCHUNK id) {
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        data[cnt * BLOCK_SIZE + i] =
            ((int)(unsigned char)(ptr[i]) - 128) / 128.0;
    }
    index[cnt++] = id;

    if (cnt == BATCH_SIZE)
        return true;
    else
        return false;
}

/** This function get the hash value into
 *  ret pairs in formate <hash value, chunkNum>*/ 
std::vector<std::pair<DEEPSKETCHHASH, DEEPSKETCHCHUNK>> NetworkHash::request() {
    if (cnt == 0) return std::vector<std::pair<DEEPSKETCHHASH, DEEPSKETCHCHUNK>>();

    std::vector<std::pair<DEEPSKETCHHASH, DEEPSKETCHCHUNK>> ret(cnt);

    std::vector<torch::jit::IValue> inputs;
    torch::Tensor t =
        torch::from_blob(data, {cnt, BLOCK_SIZE}).to(torch::kCUDA);
    inputs.push_back(t);

    // it seems here calculates the hash value
    torch::Tensor output = module.forward(inputs).toTensor().cpu();

    // change into 0 or 1
    torch::Tensor comp = output.ge(0.0);
    memcpy(memout, comp.cpu().data_ptr<bool>(), cnt * DEEPSKETCH_HASH_SIZE);

    char* ptr = (char*)(this->memout);

    for (int i = 0; i < cnt; ++i) {
        //copy hash result into ret, note the copy length is byte
        memcpy((ret[i].first.data()), &(ptr[DEEPSKETCH_HASH_SIZE/8*i]), DEEPSKETCH_HASH_SIZE/8);
//        for (int j = 0; j < DEEPSKETCH_HASH_SIZE; ++j) {
//            if (ptr[DEEPSKETCH_HASH_SIZE * i + j]) ret[i].first.flip(j);
//        }
        ret[i].second = index[i];
    }

    cnt = 0;
    return ret;
}