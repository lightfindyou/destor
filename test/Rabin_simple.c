#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include "chunking.h"

#define WINDOWSIZE (32)
#define PRIME 23
#define N 2048
#define M 1021

#define BREAKMARK_VALUE 0x78

typedef unsigned int UINT32;
typedef unsigned long long int UINT64;

UINT64 fp;
int bufpos;
int shift;

/***********************************************the rabin**********************************************/
static int rabin_mask = 0;

unsigned long subCoeff;
static int chunkMax, chunkAvg, chunkMin;
extern unsigned long g_condition_mask[];
extern unsigned long Mask, jumpMask;
extern int jumpLen;

void rabin_simple_init(int chunkSize) {
	chunkAvg = chunkSize;
	chunkMax = chunkSize*2;
	chunkMin = chunkSize/8;
	rabin_mask = chunkAvg - 1;

    int index = log2(chunkAvg);
    assert(index>6);
    assert(index<17);
    Mask = g_condition_mask[index-1];
    jumpMask = g_condition_mask[index-2];
    jumpLen = chunkAvg/4;
    subCoeff = 1;
    for(int i=0; i < WINDOWSIZE ; i++){
        subCoeff = (PRIME * subCoeff) % M;
    }
}

/* The standard rabin chunking */
int rabin_simple_chunk_data(unsigned char *p, int n) {

	UINT64 fp = 0;
	int i = 1;

	if (n <= chunkMin){
		return n;
	} else{
		i = chunkMin;
	}

	for(int k = i-WINDOWSIZE; k<i; k++){
		fp = (fp * PRIME + p[k])%M;
	}

	int end = n > chunkMax ? chunkMax : n;
	while (i < end) {
		fp = (fp * PRIME - subCoeff * p[i-WINDOWSIZE-1] + p[i]) % M;
		if ((fp & rabin_mask) == BREAKMARK_VALUE)
			break;
		i++;
	}
	return i;
}