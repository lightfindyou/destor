#include <assert.h>
#include <math.h>
#include <memory.h>
#include <openssl/md5.h>
#include <stdint.h>
#include <stdio.h>
#include <xxhash.h>
#include "../destor.h"
#include "featuring.h"

#define SymbolCount 256
#define DigistLength 16
#define SeedLength 64
#define MaxChunkSizeOffset 3
#define MinChunkSizeOffset 2

uint64_t gearhash_matrix[SymbolCount];

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

uint64_t gearHighdedupMask;
uint64_t odessMask;
MD5_CTX gearhash_md5_ctx;

void initGearMatrixFea(){

    char seed[SeedLength];
    for(int i=0; i<SymbolCount; i++){
        for(int j=0; j<SeedLength; j++){
            seed[j] = i;
        }

        gearhash_matrix[i] = 0;
        unsigned char md5_result[DigistLength];

        MD5_Init(&gearhash_md5_ctx);
        MD5_Update(&gearhash_md5_ctx, seed, SeedLength);
        MD5_Final(md5_result, &gearhash_md5_ctx);

        memcpy(&gearhash_matrix[i], md5_result, sizeof(uint64_t));
    }

}

void gearhash_gear_init(int featureNumber){

    initGearMatrixFea();
    int index = log2(destor.chunk_avg_size/featureNumber);
    gearHighdedupMask = gearhash_g_condition_mask[index];
    odessMask = gearhash_g_condition_mask[7];
    printf("gearHighdedupMask: %lx\n", gearHighdedupMask);
    printf("        odessMask: %lx\n", odessMask);
}

/** devide chunk into subchunks accroding to fingerprint
 *  choose the biggest fingerprint in subchunk
 *  return the number of features*/
int gear_max_highdedup_12fea_64B_max(unsigned char *p, int n, feature* fea,
                 int maxFeaNum, unsigned long feaLenMask){

    feature fingerprint=0;
    int i=0, feaNum = 0;

    while(i < n && feaNum < maxFeaNum){     //if loop stop because feaNum, then feaNum = maxFeaNum;
        fingerprint = (fingerprint<<1) + (gearhash_matrix[p[i]]);
        i++;

        if( fingerprint > fea[feaNum]){
            fea[feaNum] = fingerprint;
        }

        if(!(fingerprint & gearHighdedupMask)){
            feaNum++;
        }
    }

    if(feaNum != maxFeaNum){
        feaNum++;
    }
    return feaNum;
}

/** devide chunk into subchunks accroding to fingerprint
 *  choose the biggest fingerprint in subchunk
 *  limited feature size by feaLenMask
 *  return the number of features*/
int gear_max_highdedup_32fea_16B_max(unsigned char *p, int n, feature* fea,
                 int maxFeaNum, unsigned long feaLenMask){

    feature fingerprint=0;
    int i=0, feaNum = 0;

    while(i < n && feaNum < maxFeaNum){     //if loop stop because feaNum, then feaNum = maxFeaNum;
        fingerprint = (fingerprint<<1) + (gearhash_matrix[p[i]]);
        i++;

        feature tmp = fingerprint & feaLenMask;
        if( tmp > fea[feaNum]){
            fea[feaNum] = tmp;
        }

        if(!(fingerprint & gearHighdedupMask)){
            feaNum++;
        }
    }

    if(feaNum != maxFeaNum){
        feaNum++;
    }
    return feaNum;
}

int gear_highdedup_max(unsigned char *p, int n, feature* fea,
                 int maxFeaNum, unsigned long feaLenMask){

    feature fingerprint=0;
    int i=0, feaNum = 0;

    while(i < n && feaNum < maxFeaNum){     //if loop stop because feaNum, then feaNum = maxFeaNum;
        fingerprint = (fingerprint<<1) + (gearhash_matrix[p[i]]);
        i++;

        feature tmp = fingerprint & feaLenMask;
        if( tmp > fea[feaNum]){
            fea[feaNum] = tmp;
        }

        if(!(fingerprint & gearHighdedupMask)){
            feaNum++;
        }
    }

    if(feaNum != maxFeaNum){
        feaNum++;
    }
    return feaNum;
}

int gear_max_highdedup_32fea_16B_xxhash(unsigned char *p, int n, feature* fea,
                 int maxFeaNum, unsigned long feaLenMask){

    feature fingerprint=0;
    unsigned char md5Ret[16];
    int i=0, feaNum = 0, startOffset = 0;

    while(i < n && feaNum < maxFeaNum){     //if loop stop because feaNum, then feaNum = maxFeaNum;
        fingerprint = (fingerprint<<1) + (gearhash_matrix[p[i]]);

        if(!(fingerprint & gearHighdedupMask)){
            int len = i - startOffset;
            fea[feaNum] = XXH64(&p[startOffset], len, 0) & feaLenMask;
            startOffset = i;
            feaNum++;
        }

        i++;
    }

    if(feaNum != maxFeaNum){
        int len = i - startOffset;
        fea[feaNum] = XXH64(&p[startOffset], len, 0) & feaLenMask;
        feaNum++;
    }
    return feaNum;
}

/**here fsc means the boundary of subchunk is decided by index,
 * but the fingerprint it self is decided by CDC
*/
int highdedup_32fea_16B_FSC(unsigned char *p, int n, feature* fea,
                 int maxFeaNum){

    feature fingerprint=0;
    int i=0, feaNum = 0, startOffset = 0;
    int subChunkLen = n/maxFeaNum, curBoundary = subChunkLen;

    while(i < n && feaNum < maxFeaNum){     //if loop stop because feaNum, then feaNum = maxFeaNum;
        fingerprint = (fingerprint<<1) + (gearhash_matrix[p[i]]);

        if(fingerprint > fea[feaNum]){
            fea[feaNum] = fingerprint;
        }

        if( i >= curBoundary){
            feaNum++;

            curBoundary += subChunkLen;
            if(feaNum == (maxFeaNum - 1)){
                curBoundary = n;
            }
        }

        i++;
    }

    return maxFeaNum;
}

void gear_odess(unsigned char *p, int n, feature* fea, int featureNum) {

    feature fingerprint=0, s=0;
    int i=0;

    while(i < n){
        fingerprint = (fingerprint<<1) + (gearhash_matrix[p[i]]);
        i++;

        if(!(fingerprint & odessMask)){
	    	for(int j = 0; j< featureNum; j++){
	    		s = (fingerprint*maMatrix[j][0] + maMatrix[j][1]);
	    		if(s > fea[j]){
	    			fea[j] = s;
	    		}
	    	}
        }
    }
	return;
}

void setbit(unsigned char* hash, int index){
    int byteOff = index/8;
    int offset = index%8;
    switch (offset) {
    case 0:
    	hash[byteOff] |= 0x1;
        break;
    
    case 1:
    	hash[byteOff] |= 0x2;
        break;
    
    case 2:
    	hash[byteOff] |= 0x4;
        break;
    
    case 3:
    	hash[byteOff] |= 0x8;
        break;
    
    case 4:
    	hash[byteOff] |= 0x10;
        break;
    
    case 5:
    	hash[byteOff] |= 0x20;
        break;
    
    case 6:
    	hash[byteOff] |= 0x40;
        break;
    
    case 7:
    	hash[byteOff] |= 0x80;
        break;
    
    default:
        printf("setbit error!!!\n");
        break;
    }
}

uint64_t fineANN_mask[] = {
    0x0000000000000000,// 1B
    0x0000000001000000,// 2B
    0x0000000003000000,// 4B
    0x0000000013000000,// 8B
    0x0000000093000000,// 16B
    0x0000000093001000,// 32B

    0x0000000093005000,// 64B
    0x0000000093005100,// 128B
    0x0000000093005500,// 256B
    0x0000000093015500,// 512B
    0x0000000093035500,// 1KB
    0x0000000093035510,// 2KB
    0x000000009303d510,// 4KB
};

void gear_fineANN(unsigned char *p, int n, fineANN_t* fea, int featureNum) {

    //limit the mask to lower 4 byte to constrain window size
    uint64_t mask = fineANN_mask[5];
    uint64_t fingerprint=0;
    for(int i=0; i< n; i++){
        fingerprint = (fingerprint<<1) + (gearhash_matrix[p[i]]);
        if(!(fingerprint & mask)){
//            int index = (fingerprint & WINDOW_MASK)%EFFCTIVE_LEN;
            int index = (fingerprint )%FINEANN_FEATURE_BITS;
            setbit(fea, index);
            int weightOffset = FINEANN_MAX_FEATURE_BITS/8 + index;
            fea[weightOffset] += 1;
        }
    }
}


void getLeastComm(uint64_t* freqArray, uint64_t* idxArray, int len, int* index, int* freq){
    *freq = INT_MAX;
    for(int i = 0; i < len; i++){
        if(freqArray[idxArray[i]] < *freq){
            *index = i;
            *freq = freqArray[idxArray[i]];
        }
    }
}

void gear_statis(unsigned char *p, int n, statis_t* fea, int featureNum) {

    uint64_t* statisTable = calloc(sizeof(uint64_t), STATIS_MAP_MAX);
    //limit the mask to lower 4 byte to constrain window size
    uint64_t mask = fineANN_mask[5];
    uint64_t fingerprint=0;

    for(int i=0; i < n; i++){
        fingerprint = (fingerprint<<1) + (gearhash_matrix[p[i]]);
        int index = (fingerprint & STATIS_WINDOW_MASK)%STATIS_MAP_MAX;
        statisTable[index] += 1;

    }

    /**choose the most frequent point from statisTable*/
    int leastCommIdx, leastComm;
    for(int i=0; i<STATIS_FEATURE_NUM; i++){
        fea[i] = i;
    }
    getLeastComm(statisTable, fea, STATIS_FEATURE_NUM, &leastCommIdx, &leastComm);

    for(int i=STATIS_FEATURE_NUM; i < STATIS_MAP_MAX; i++){
        if(statisTable[i] > leastComm){
            fea[leastCommIdx] = i;
            getLeastComm(statisTable, fea, STATIS_FEATURE_NUM, &leastCommIdx, &leastComm);
        }
    }

}

unsigned long weightChunkMask;

void weightchunkGearInit(){
    initGearMatrixFea();
    weightChunkMask = gearhash_g_condition_mask[5];
}

void delIdentFea(feature* fea,  int* feaNum){

    int i = 0;
    int maxIdx = (*feaNum) - 1;
    feature curFea = fea[maxIdx];
    while(i < maxIdx){
        if(fea[i] == curFea){
            goto delFea;
        }
        i++;
    }
    return;
delFea:
    while(i < maxIdx){
        fea[i] = fea[i+1];
        i++;
    }
    *feaNum -= 1;
}

int weightchunkGearing(unsigned char *p, int n, feature* fea){

    feature fingerprint=0;
    int i=0, feaNum = 0;

    while(i < n && feaNum < WEIGHTCHUNK_FEATURE_NUM ){     //if loop stop because feaNum, then feaNum = maxFeaNum;
        int subChunkLen = 0;
        fingerprint = (fingerprint<<1) + (gearhash_matrix[p[i]]);
        i++;
        subChunkLen++;

        if(!(fingerprint & weightChunkMask)){
            fea[feaNum] = fingerprint;
            feaNum++;
            delIdentFea(fea, &feaNum);
        }
    }

    return feaNum;
}