#include <torch/script.h>
#include <iostream>

#include "deepsketch_featuring.h"
#include "deepsketch_featuring_c.h"
#include "../../destor.h"

extern "C" void deepsketch_featuring_init(char *modelPath) {
    network = new NetworkHash(TEMP_QUEUE_SIZE, modelPath);
}

extern "C" int deepsketch_featuring(unsigned char *buf, int size,
                                     struct chunk *c) {
    sync_queue_push(feature_temp_queue, c);
    /**On success, write fetures into chunks and push chunks and return 1;
     * On false, return 0;
     * */
    if(network->push((char*)buf, size, c)){
        //TODO write fetures into chunks
        return network->request();
    }else{
        return 0;
    }

}

bool NetworkHash::push(char *ptr, int size, struct chunk* c) {
    if (cnt == 0) {
        memset(this->data, 0, sizeof(float) * batch_size * BLOCK_SIZE * 2);
    }
    for (int i = 0; i < size; ++i) {
        data[cnt * BLOCK_SIZE * 2 + i] =
            ((int)(unsigned char)(ptr[i]) - 128) / 128.0;
    }
    index[cnt++] = c;

    if (cnt == batch_size)
        return true;
    else
        return false;
}

// This function get the hash value into ret pairs
int NetworkHash::request() {
    if (cnt == 0) return 0;

    int ret = cnt;
    std::vector<torch::jit::IValue> inputs;
    torch::Tensor t =
        torch::from_blob(data, {cnt, BLOCK_SIZE * 2}).to(torch::kCUDA);
    inputs.push_back(t);

    // it seems here calculates the hash value
    torch::Tensor output = module.forward(inputs).toTensor().cpu();

    // change into 0 or 1
    torch::Tensor comp = output.ge(0.0);
    memcpy(memout, comp.cpu().data_ptr<bool>(), cnt * DEEPSKETCH_HASH_SIZE);

    uint64_t* featureArray = (uint64_t*)this->memout;
//    bool *ptr = this->memout;
    for (int i = 0; i < cnt; ++i) {
        //The corrcopoding i of feature and index means the feature is sequence
        //for (int j = 0; j < DEEPSKETCH_HASH_SIZE; ++j) {
        //    if (ptr[DEEPSKETCH_HASH_SIZE * i + j]) ret[i].first.flip(j);
        //}
        //ret[i].second = index[i];
        struct chunk* c = index[i];
        memcpy(c->fea, &featureArray[DEEPSKETCH_HASH_SIZE/sizeof(uint64_t)*i],
                     DEEPSKETCH_HASH_SIZE/sizeof(uint64_t));
    }

    cnt = 0;
    return ret;
}
