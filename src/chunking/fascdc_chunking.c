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

uint64_t g_condition_mask[] = {
    //Do not use 1-32B, for aligent usage
        0x00001803110,// 1B
        0x00001803110,// 2B
        0x00001803110,// 4B
        0x00001803110,// 8B
        0x00001803110,// 16B
        0x00001803110,// 32B

        0x00001803110,// 64B
        0x000018035100,// 128B
        0x00001800035300,// 256B
        0x000019000353000,// 512B
        0x0000590003530000,// 1KB
        0x0000d90003530000,// 2KB
        0x0000d90103530000,// 4KB
        0x0000d90303530000,// 8KB
        0x0000d90313530000,// 16KB
        0x0000d90f03530000,// 32KB
        0x0000d90303537000,// 64KB
        0x0000d90703537000// 128KB
};

void fastcdc_init(uint32_t expectCS){
    char seed[SeedLength];
    int index;
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

    g_min_fastcdc_chunk_size = destor.chunk_min_size;
    g_max_fastcdc_chunk_size = destor.chunk_max_size;
    g_expect_fastcdc_chunk_size = destor.chunk_avg_size;
    index = log2(g_expect_fastcdc_chunk_size);
    assert(index>6);
    assert(index<17);
    MaskS = g_condition_mask[index+1];
    MaskL = g_condition_mask[index-1];
}


int fastcdc_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    //uint64_t digest __attribute__((unused));
    int i=g_min_fastcdc_chunk_size;//, Mid=g_min_fastcdc_chunk_size + 8*1024;
    int Mid = g_expect_fastcdc_chunk_size;
    //return n;

    if(n<=g_min_fastcdc_chunk_size) //the minimal  subChunk Size.
        return n;
    //windows_reset();
    if(n > g_max_fastcdc_chunk_size)
        n = g_max_fastcdc_chunk_size;
    else if(n<Mid)
        Mid = n;

    while(i<Mid){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        if ((!(fingerprint & MaskS /*0x0000d90f03530000*/))) { //AVERAGE*2, *4, *8
            return i;
        }
        i++;
    }

    while(i<n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        if ((!(fingerprint & MaskL /*0x0000d90003530000*/))) { //Average/2, /4, /8
            return i;
        }
        i++;
    }
    //printf("\r\n==chunking FINISH!\r\n");
    return i;

}
