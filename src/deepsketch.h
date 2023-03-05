
#ifndef DEEPSKETCH_H_
#define DEEPSKETCH_H_

#include "../destor.h"

#define DEEPSKETCH_HASH_SIZE 128
typedef struct deepsketchhash {unsigned char[DEEPSKETCH_HASH_SIZE/8]} DEEPSKETCHHASH;
typedef void* DEEPSKETCHCHUNK;

#endif	//DEEPSKETCH_H_