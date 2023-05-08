#ifndef DEEPSKETCH_H_
#define DEEPSKETCH_H_

#include "destor.h"

#define DEEPSKETCH_HASH_SIZE 128
#define BLOCK_SIZE 4096
//typedef struct deepsketchhash {unsigned char data[DEEPSKETCH_HASH_SIZE/8];} DEEPSKETCHHASH;
//typedef unsigned char DEEPSKETCHHASH[DEEPSKETCH_HASH_SIZE/8];
typedef std::array<unsigned char, DEEPSKETCH_HASH_SIZE/8> DEEPSKETCHHASH;

typedef void* DEEPSKETCHCHUNK;

#endif	//DEEPSKETCH_H_