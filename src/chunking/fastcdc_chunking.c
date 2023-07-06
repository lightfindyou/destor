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
#include "../featuring/featuring.h"

#define SymbolCount 256
#define DigistLength 16
#define SeedLength 64
#define MaxChunkSizeOffset 3
#define MinChunkSizeOffset 2

uint64_t g_gear_matrix[SymbolCount];
uint64_t gearSubchunkMask;
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

void fastcdc_init(){
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

//    g_min_fastcdc_chunk_size = 2048;
//    g_max_fastcdc_chunk_size = 65536;
//    g_expect_fastcdc_chunk_size = expectCS;
    g_min_fastcdc_chunk_size = destor.chunk_min_size;
    g_max_fastcdc_chunk_size = destor.chunk_max_size;
    g_expect_fastcdc_chunk_size = destor.chunk_avg_size;
    index = log2(g_expect_fastcdc_chunk_size);
    assert(index>6);
    assert(index<17);
    MaskS = g_condition_mask[index+1];
    MaskL = g_condition_mask[index-1];
}


struct chunk* fastcdc_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    //uint64_t digest __attribute__((unused));
    //int i=g_min_fastcdc_chunk_size;//, Mid=g_min_fastcdc_chunk_size + 8*1024;
    int i=0;//, Mid=g_min_fastcdc_chunk_size + 8*1024;
    int Mid = g_expect_fastcdc_chunk_size;
    //return n;

    if(n<=g_min_fastcdc_chunk_size){ //the minimal  subChunk Size.
        return new_chunk(n);
    }

    if(n > g_max_fastcdc_chunk_size){
        n = g_max_fastcdc_chunk_size;
    } else if(n<Mid){
        Mid = n;
    }

    while(i<Mid){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        if ((!(fingerprint & MaskS /*0x0000d90f03530000*/))) { //AVERAGE*2, *4, *8
            return new_chunk(i);
        }
        i++;
    }

    while(i<n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        if ((!(fingerprint & MaskL /*0x0000d90003530000*/))) { //Average/2, /4, /8
            return new_chunk(i);
        }
        i++;
    }
    //printf("\r\n==chunking FINISH!\r\n");
    return new_chunk(i);
}

uint64_t Mask;
uint64_t back_mask_TTTD;
void gear_init(){
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

    int index = log2(destor.chunk_avg_size);
    int cOnes = index;
    assert(index>6);
    assert(index<17);
    Mask = g_condition_mask[cOnes];
    back_mask_TTTD = g_condition_mask[cOnes - 1];

    printf("\nMask:  %16lx\n", Mask);
}

void highdedup_chunk_init(){
    gear_init();
    int subChunkIndex = log2(destor.chunk_avg_size/destor.featureNum);
    gearSubchunkMask = g_condition_mask[subChunkIndex];
}

struct chunk* gear_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0;
    int minSize = destor.chunk_min_size;

	if (n <= minSize)
		return new_chunk(n);
#if !CHUNKMIN 
	else
		i = minSize;
#endif  //MINJUMP 
    n = n<destor.chunk_max_size?n:destor.chunk_max_size;

    while(i < n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if( G_UNLIKELY(!(fingerprint & Mask)) ){
            return new_chunk(i);
        }
    }
    return new_chunk(n);
}

struct chunk* highdedup_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0;
    int feaNum = 0;
    struct chunk* ret = new_chunk(0);
    feature* fea = ret->fea;

    n = n<destor.chunk_max_size?n:destor.chunk_max_size;

    while(i < n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if( G_UNLIKELY(!(fingerprint & Mask)) ){
            goto retPoint;
        }

        if(G_UNLIKELY(feaNum >= HIGHDEDUP_FEATURE_NUM)){continue;}
        feature tmp = fingerprint & HIGHDEDUP_FEATURE_MASK;
        if(tmp > fea[feaNum]){
            fea[feaNum] = tmp;
        }

        if(!(fingerprint & gearSubchunkMask)){
            feaNum++;
        }
    }

retPoint:
    ret->feaNum = feaNum + 1;
    if(ret->feaNum > HIGHDEDUP_FEATURE_NUM){
        ret->feaNum = HIGHDEDUP_FEATURE_NUM;
    }
    ret->size = i;
    ret->data = malloc(i);
    return ret;
}

struct chunk* TTTD_gear_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0, m = 0;
    int minSize = destor.chunk_min_size;

	if (n <= minSize)
		return new_chunk(n);
#if !CHUNKMIN 
	else
		i = minSize;
#endif  //MINJUMP 
    n = n<destor.chunk_max_size?n:destor.chunk_max_size;

    while(i < n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if( G_UNLIKELY(!(fingerprint & back_mask_TTTD)) ){
            if(!(fingerprint & Mask)){
                return new_chunk(i);
            }
            m = i;
        }
    }

	if (m != 0)
		return new_chunk(m);
	else
		return new_chunk(i);
}

uint32_t gearjumpChunkSize;
uint64_t jumpMask;
int jumpLen = 0;

/**
 * mto means "Mask Ones Less Than Chunk Ones"
*/
#if SENTEST
void gearjump_init(int mto){
#else
void gearjump_init(){
#endif //SENTEST
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

    gearjumpChunkSize = destor.chunk_avg_size;
    int index = log2(gearjumpChunkSize);
    int jOnes = 0, cOnes = index - 1;
    assert(index>6);
    assert(index<17);
    Mask = g_condition_mask[cOnes];
#if SENTEST
    assert(mto>0);
    assert(mto<(cOnes));
    jOnes = cOnes - mto;
    jumpMask = g_condition_mask[jOnes];
//  jumpLen = power(2, jMaskOnes);
    jumpLen = pow(2, (cOnes + jOnes))/(pow(2, cOnes) - pow(2, jOnes));
    printf("cOnes:%d, jOnes:%d, jumpLen:%d.\n", cOnes, jOnes, jumpLen);
#else
    jumpMask = g_condition_mask[index-2];
    jumpLen = gearjumpChunkSize/2;
#endif  //SENTEST

//    gearjumpChunkSize = 4096;
//    jumpLen = gearjumpChunkSize/2;
//    Mask = g_condition_mask[11];
//    jumpMask = g_condition_mask[10];
    printf("\nMask:  %16lx\n", Mask);
    printf("jumpMask:%16lx\n", jumpMask);
    printf("jumpLen:%d\n\n", jumpLen);
}

#define CHUNKMIN 0
struct chunk* gearjump_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0;
    int minSize = destor.chunk_min_size;

	if (n <= minSize)
		return new_chunk(n);
#if !CHUNKMIN 
	else
		i = minSize;
#endif  //MINJUMP 
    n = n<destor.chunk_max_size?n:destor.chunk_max_size;

    while(i < n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if( G_UNLIKELY(!(fingerprint & jumpMask)) ){
            if ((!(fingerprint & Mask))) { //AVERAGE*2, *4, *8
#if CHUNKMIN 
                if(i<minSize){
                    i += 2048;
                    continue;
                }
#endif  //MINJUMP 
                return new_chunk(i);
            } else {
                fingerprint = 0;
                //TODO xzjin here need to set the fingerprint to 0 ?
                i += jumpLen;
            }
        }
    }

    i = i<n?i:n;
    return new_chunk(i);
}

struct chunk* gearjumpTTTD_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0, m = 0;
    int minSize = destor.chunk_min_size;

	if (n <= minSize)
		return new_chunk(n);
	else
		i =  minSize;
    unsigned long end = n < destor.chunk_max_size? n:destor.chunk_max_size;

    while(i < end){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if(__glibc_unlikely(!(fingerprint & jumpMask)) ){

            if ((!(fingerprint & Mask))) { //AVERAGE*2, *4, *8
                return new_chunk(i);
            } 
            m = i;
            fingerprint=0;
            //TODO xzjin here need to set the fingerprint to 0 ?
            i += jumpLen;
        }
    }

    if(m != 0){
        return new_chunk(m);
    }
    i = i<end?i:end;
    return new_chunk(i);
}

uint64_t largeMask;
uint64_t largeJumpMask;
int largeJumpLen = 0;
/**
 * mto means "Mask Ones Less Than Chunk Ones"
*/
void normalized_gearjump_init(int mto){
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

    gearjumpChunkSize = destor.chunk_avg_size;
    int index = log2(gearjumpChunkSize);
    int jOnes = 0, cOnes = index - 2;
    assert(index>6);
    assert(index<17);
    Mask = g_condition_mask[cOnes];
    largeMask = g_condition_mask[cOnes+2];
#if SENTEST
    assert(mto>0);
    assert(mto<(cOnes));
    jOnes = cOnes - mto;
    jumpMask = g_condition_mask[jOnes];
    largeJumpMask = g_condition_mask[jOnes+2];
//  jumpLen = power(2, jMaskOnes);
    jumpLen = pow(2, (cOnes + jOnes))/(pow(2, cOnes) - pow(2, jOnes));
    largeJumpLen = pow(2, (cOnes + 2 + jOnes + 2))/(pow(2, cOnes+2) - pow(2, jOnes+2));
    printf("cOnes:%d, jOnes:%d, jumpLen:%d.\n", cOnes, jOnes, jumpLen);
#else
    jumpMask = g_condition_mask[index-2];
    jumpLen = gearjumpChunkSize/2;
#endif  //SENTEST

//    gearjumpChunkSize = 4096;
//    jumpLen = gearjumpChunkSize/2;
//    Mask = g_condition_mask[11];
//    jumpMask = g_condition_mask[10];
    printf("\n  Mask:%16lx\t    largeMask:%16lx\n", Mask, largeMask);
    printf("jumpMask:%16lx\tlargejumpMask:%16lx\n", jumpMask, largeJumpMask);
    printf(" jumpLen:%d\t    largeJumpLen:%d\n\n", jumpLen, largeJumpLen);
}

struct chunk* normalized_gearjump_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0;
    int minSize = destor.chunk_min_size;
    int middle = destor.chunk_avg_size<n?destor.chunk_avg_size:n;

	if (n <= minSize)
		return new_chunk(n);
#if !CHUNKMIN 
	else
		i = minSize;
#endif  //MINJUMP 
    n = n<destor.chunk_max_size?n:destor.chunk_max_size;

    while(i < middle){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if( G_UNLIKELY(!(fingerprint & largeJumpMask)) ){
            if ((!(fingerprint & largeMask))) { //AVERAGE*2, *4, *8
                return new_chunk(i);
            } else {
                fingerprint = 0;
                i += largeJumpLen;
            }
        }
    }

    while(i < n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if( G_UNLIKELY(!(fingerprint & jumpMask)) ){
            if ((!(fingerprint & Mask))) { //AVERAGE*2, *4, *8
                return new_chunk(i);
            } else {
                fingerprint = 0;
                i += jumpLen;
            }
        }
    }

    i = i<n?i:n;
    return new_chunk(i);
}
