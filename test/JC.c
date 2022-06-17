#include <assert.h>
#include <math.h>
#include <memory.h>
#include <openssl/md5.h>
#include <stdint.h>
#include <stdio.h>
#include "chunking.h"

#define SymbolCount 256
#define DigistLength 16
#define SeedLength 64
#define MaxChunkSizeOffset 3
#define MinChunkSizeOffset 2

#define CHUNKMIN 0
#define TTTD 0
#define NOTContain 0
#define BREAKDOWN 1

uint64_t g_gear_matrix[SymbolCount];
static int chunkMax, chunkAvg, chunkMin;

uint64_t MaskS;
uint64_t MaskL;

uint32_t gearjumpChunkSize;
uint64_t Mask;
uint64_t Mask_backup;
uint64_t jumpMask;
int jumpLen = 0;

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

#if SENTEST
void gearjump_init(int jMaskOnes){
#else
void gearjump_init(int chunkSize){
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

	chunkAvg = chunkSize;
	chunkMax = chunkSize*2;
	chunkMin = chunkSize/8;

    gearjumpChunkSize = chunkSize;
    int index = log2(gearjumpChunkSize);
    assert(index>6);
    assert(index<17);
    Mask = g_condition_mask[index-1];
    Mask_backup = g_condition_mask[index-2];
#if SENTEST
    jumpMask = g_condition_mask[jMaskOnes];
//    jumpLen = power(2, jMaskOnes);
    jumpLen = exp2(jMaskOnes);
#else
    jumpMask = g_condition_mask[index-2];
    jumpLen = gearjumpChunkSize/2;
#endif  //SENTEST

//    gearjumpChunkSize = 4096;
//    jumpLen = gearjumpChunkSize/2;
//    Mask = g_condition_mask[11];
//    jumpMask = g_condition_mask[10];
    printf("Mask:    %16lx\n", Mask);
    printf("jumpMask:%16lx\n", jumpMask);
    printf("jumpLen:%d\n\n", jumpLen);
}

#if BREAKDOWN
int gearjump_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0;

	if (n <= chunkMin)
		return n;
#if !CHUNKMIN 
	else
		i = chunkMin;
#endif  //MINJUMP 
    n = n<chunkMax?n:chunkMax;

    while(i < n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if ((!(fingerprint & Mask))) { //AVERAGE*2, *4, *8
            return i;
        } 
            
        if(__glibc_unlikely(!(fingerprint & jumpMask)) ){
                fingerprint=0;
                //TODO xzjin here need to set the fingerprint to 0 ?
                i += jumpLen;
        }
    }

    return i<n?i:n;
}

#else

int gearjump_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0;
#if TTTD
    int smallerChunkPoint = 0;
#endif //TTTD

	if (n <= chunkMin)
		return n;
#if !CHUNKMIN 
	else
		i = chunkMin;
#endif  //MINJUMP 
    n = n<chunkMax?n:chunkMax;

    while(i < n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if(__glibc_unlikely(!(fingerprint & jumpMask)) ){
            if ((!(fingerprint & Mask))) { //AVERAGE*2, *4, *8
                return i;
            } else {
#if TTTD 
                smallerChunkPoint = i;
#endif //TTTD 
                fingerprint=0;
                //TODO xzjin here need to set the fingerprint to 0 ?
                i += jumpLen;
            }
        }
    }

#if TTTD 
    if(smallerChunkPoint){
        return smallerChunkPoint;
    }
#endif //TTTD 
    return i<n?i:n;
}
#endif  //BREAKDOWN

uint64_t MaskLeap, jumpLeapMask, jumpLeapLen;

void gearleap_init(int chunkSize){

	chunkAvg = chunkSize;
	chunkMax = chunkSize*2;
	chunkMin = chunkSize/8;

    gearjumpChunkSize = chunkSize;
    int index = log2(gearjumpChunkSize);
    assert(index>6);
    assert(index<17);
    jumpLeapMask = g_condition_mask[2];
    jumpLeapLen = gearjumpChunkSize/2;

    printf("jumpLeapMask:%16lx\n", jumpLeapMask);
    printf("jumpLeapLen:%ld\n\n", jumpLeapLen);
}

int gearleap_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0;
#if TTTD
    int smallerChunkPoint = 0;
#endif //TTTD

	if (n <= chunkMin)
		return n;

    while(i < n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

#if NOTContain
        if ((!(fingerprint & Mask))) { //AVERAGE*2, *4, *8
            return i;
        }

        if((!(fingerprint & jumpMask)) ){
                fingerprint=0;
                i += jumpLen;
        }
#else  //NOTContain

        if((!(fingerprint & jumpMask)) ){
            if ((!(fingerprint & Mask))) { //AVERAGE*2, *4, *8
                return i;
            } else {
#if TTTD 
                smallerChunkPoint = i;
#endif //TTTD 
                fingerprint=0;
                //TODO xzjin here need to set the fingerprint to 0 ?
                i += jumpLen;
            }
        }
#endif  //NOTContain
    }
#if TTTD 
    if(smallerChunkPoint){
        return smallerChunkPoint;
    }
#endif //TTTD 
    return i<n?i:n;
}

void gear_init(int chunkSize){
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
        if(i==0x0 || i==0x3D || i==0xFE || i==0xFF){
            printf("i:%X, hash:%lX\n", i, g_gear_matrix[i]);
        }
    }

	chunkAvg = chunkSize;
	chunkMax = chunkSize*2;
	chunkMin = chunkSize/8;

    gearjumpChunkSize = chunkSize;
    int index = log2(gearjumpChunkSize);
    assert(index>6);
    assert(index<17);
    Mask = g_condition_mask[index];
}

int gear_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    int i=0;

	if (n <= chunkMin)
		return n;
	else
		i = chunkMin;

    while(i < n){
        fingerprint = (fingerprint<<1) + (g_gear_matrix[p[i]]);
        i++;

        if(__glibc_unlikely(!(fingerprint & Mask)) ){
            return i;
        }
    }

    return i;
}
