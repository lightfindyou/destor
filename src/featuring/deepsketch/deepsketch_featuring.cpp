#include <torch/script.h>
#include <iostream>

#include "deepsketch_featuring.h"
#include "../../destor.h"

typedef std::bitset<HASH_SIZE> MYHASH;

extern "C" void deepsketch_featuring_init(char *modelPath) {}

extern "C" void deepsketch_featuring(unsigned char *buf, int size,
                                     struct chunk *c) {
    std::cout << "calling c++ functions " << std::endl;
}

class NetworkHash {
   private:
    int BATCH_SIZE;
    torch::jit::script::Module module;
    float *data;
    bool *memout;
    int *index;
    int cnt;

   public:
    NetworkHash(int BATCH_SIZE, char *module_name) {
        this->BATCH_SIZE = BATCH_SIZE;
        this->module = torch::jit::load(module_name);
        this->module.to(at::kCUDA);
        this->module.eval();
        this->data = new float[BATCH_SIZE * BLOCK_SIZE * 2];
        this->memout = new bool[BATCH_SIZE * HASH_SIZE];
        this->index = new int[BATCH_SIZE];
        this->cnt = 0;
    }
    ~NetworkHash() {
        delete[] this->data;
        delete[] this->memout;
        delete[] this->index;
    }
    bool push(char *ptr, int size, int label);
    std::vector<std::pair<MYHASH, int>> request();
};

bool NetworkHash::push(char *ptr, int size, int label) {
    if (cnt == 0) {
        memset(this->data, 0, sizeof(float) * BATCH_SIZE * BLOCK_SIZE * 2);
    }
    for (int i = 0; i < size; ++i) {
        data[cnt * BLOCK_SIZE * 2 + i] =
            ((int)(unsigned char)(ptr[i]) - 128) / 128.0;
    }
    index[cnt++] = label;

    if (cnt == BATCH_SIZE)
        return true;
    else
        return false;
}

// This function get the hash value into ret pairs
std::vector<std::pair<MYHASH, int>> NetworkHash::request() {
    if (cnt == 0) return std::vector<std::pair<MYHASH, int>>();

    std::vector<std::pair<MYHASH, int>> ret(cnt);

    std::vector<torch::jit::IValue> inputs;
    torch::Tensor t =
        torch::from_blob(data, {cnt, BLOCK_SIZE * 2}).to(torch::kCUDA);
    inputs.push_back(t);

    // it seems here calculates the hash value
    torch::Tensor output = module.forward(inputs).toTensor().cpu();

    // change into 0 or 1
    torch::Tensor comp = output.ge(0.0);
    memcpy(memout, comp.cpu().data_ptr<bool>(), cnt * HASH_SIZE);

    bool *ptr = this->memout;

    for (int i = 0; i < cnt; ++i) {
        for (int j = 0; j < HASH_SIZE; ++j) {
            if (ptr[HASH_SIZE * i + j]) ret[i].first.flip(j);
        }
        ret[i].second = index[i];
    }

    cnt = 0;
    return ret;
}
