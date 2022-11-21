//
// Created by borelset on 2019/3/20.
//

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <openssl/md5.h>
#include <stdint.h>
#include <stdio.h>
#include "../destor.h"
#include "featuring.h"

#define SymbolCount 256
#define DigistLength 16
#define SeedLength 64
#define MaxChunkSizeOffset 3
#define MinChunkSizeOffset 2

uint64_t g_gear_matrix[SymbolCount];
uint32_t g_min_fastcdc_chunk_size;
uint32_t g_max_fastcdc_chunk_size;
uint32_t g_expect_fastcdc_chunk_size;

uint64_t MaskS;
uint64_t MaskL;

enum{
    Mask_64B,
    Mask_128B,
    Mask_256B,
    Mask_512B,
    Mask_1KB,
    Mask_2KB,
    Mask_4KB,
    Mask_8KB,
    Mask_16KB,
    Mask_32KB,
    Mask_64KB,
    Mask_128KB
};

uint64_t gearhash_g_condition_mask[] = {
    //Do not use 1-32B, for aligent usage
        0x0000000000000000,// 1B
        0x0000000001000000,// 2B
        0x0000000003000000,// 4B
        0x0000010003000000,// 8B
        0x0000090003000000,// 16B
        0x0000190003000000,// 32B

        0x0000590003000000,// 64B
        0x0000590003100000,// 128B
        0x0000590003500000,// 256B
        0x0000590003510000,// 512B
        0x0000590003530000,// 1KB
        0x0000590103530000,// 2KB
        0x0000d90103530000,// 4KB
        0x0000d90303530000,// 8KB
        0x0000d90303531000,// 16KB
        0x0000d90303533000,// 32KB
        0x0000d90303537000,// 64KB
        0x0000d90703537000// 128KB
};

uint64_t Mask;
uint64_t back_mask_TTTD;
void gearhash_gear_init(int featureNumber){
    char seed[SeedLength];
    for(int i=0; i<SymbolCount; i++){
        for(int j=0; j<SeedLength; j++){
            seed[j] = i;
        }

        g_gear_matrix[i] = 0;
        unsigned char md5_result[DigistLength];

        MD5_CTX md5_ctx;
        MD5_Init(&md5_ctx);
        MD5_Update(&md5_ctx, seed, SeedLength);
        MD5_Final(md5_result, &md5_ctx);

        memcpy(&g_gear_matrix[i], md5_result, sizeof(uint64_t));
    }

    int index = log2(destor.chunk_avg_size/featureNumber);
    int cOnes = index;
    assert(index>6);
    assert(index<17);
    Mask = gearhash_g_condition_mask[cOnes];
    back_mask_TTTD = gearhash_g_condition_mask[cOnes - 1];
}

/** return the number of features*/
int gear_max_highdedup(unsigned char *p, int n, feature* fea, int maxFeaNum){

    feature fingerprint=0;
    int i=0, feaNum = 0;

    while(i < n && feaNum < maxFeaNum){     //if loop stop because feaNum, then feaNum = maxFeaNum;
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if(fingerprint > fea[feaNum]){
            fea[feaNum] = fingerprint;
        }

        if(!(fingerprint & Mask)){
            feaNum++;
        }
    }

    if(feaNum != maxFeaNum){
        feaNum++;
    }
    return feaNum;
}