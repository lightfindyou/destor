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

void gearhash_gear_init(int featureNumber){
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

    int index = log2(destor.chunk_avg_size/featureNumber);
    gearHighdedupMask = gearhash_g_condition_mask[index];
    odessMask = gearhash_g_condition_mask[7];
    printf("gearHighdedupMask: %lx\n", gearHighdedupMask);
    printf("        odessMask: %lx\n", odessMask);
}

/** return the number of features*/
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