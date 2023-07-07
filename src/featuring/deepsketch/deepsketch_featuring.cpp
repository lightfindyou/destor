#include <iostream>
#include "deepsketch_featuring.h"
#include "../../destor.h"

extern "C"
void deepsketch_featuring_init(char* modelPath){

}

extern "C"
void deepsketch_featuring(unsigned char* buf, int size, struct chunk* c) {
    std::cout << "caling c++ functions " << std::endl;
}