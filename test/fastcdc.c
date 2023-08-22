//
// Created by borelset on 2019/3/20.
//

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <openssl/md5.h>
#include <stdint.h>
#include <stdio.h>

#define SymbolCount 256
#define DigistLength 16
#define SeedLength 64
#define MaxChunkSizeOffset 3
#define MinChunkSizeOffset 2

uint64_t g_gear_matrix_fast[SymbolCount];

uint64_t MaskS_fast;
uint64_t MaskL_fast;

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

extern uint64_t g_condition_mask[];

static int chunkMax, chunkAvg, chunkMin;
void fastcdc_init(int chunkSize){
    char seed[SeedLength];
    int index;
    for(int i=0; i<SymbolCount; i++){
        for(int j=0; j<SeedLength; j++){
            seed[j] = i;
        }

        g_gear_matrix_fast[i] = 0;
        unsigned char md5_result[DigistLength];

        MD5_CTX md5_ctx;
        MD5_Init(&md5_ctx);
        MD5_Update(&md5_ctx, seed, SeedLength);
        MD5_Final(md5_result, &md5_ctx);

        memcpy(&g_gear_matrix_fast[i], md5_result, sizeof(uint64_t));
    }

//    chunkMin = 2048;
//   chunkMax = 65536;
//    chunkAvg = expectCS;
	chunkAvg = chunkSize;
	chunkMax = chunkSize*2;
	chunkMin = chunkSize/8;
    index = log2(chunkAvg);
    assert(index>6);
    assert(index<17);
    MaskS_fast = g_condition_mask[index+1];
    MaskL_fast = g_condition_mask[index-1];
}


int fastcdc_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    //uint64_t digest __attribute__((unused));
    //int i=chunkMin;//, Mid=chunkMin + 8*1024;
    int i=0;//, Mid=chunkMin + 8*1024;
    int Mid = chunkAvg;
    //return n;

    if(n<=chunkMin) //the minimal  subChunk Size.
        return n;
    //windows_reset();
    if(n >chunkMax)
        n =chunkMax;
    else if(n<Mid)
        Mid = n;

    while(i<Mid){
        fingerprint = (fingerprint<<1) + (g_gear_matrix_fast[p[i]]);
        if ((!(fingerprint & MaskS_fast /*0x0000d90f03530000*/))) { //AVERAGE*2, *4, *8
            return i;
        }
        i++;
    }

    while(i<n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix_fast[p[i]]);
        if ((!(fingerprint & MaskL_fast /*0x0000d90003530000*/))) { //Average/2, /4, /8
            return i;
        }
        i++;
    }
    //printf("\r\n==chunking FINISH!\r\n");
    return i;
}