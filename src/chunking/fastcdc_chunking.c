//
// Created by borelset on 2019/3/20.
//

#include <assert.h>
#include <math.h>
#include <memory.h>
#include <openssl/md5.h>
#include <stdint.h>
#include <stdio.h>
#include "chunking.h"
#include "../destor.h"
#include "gear_common.h"

static uint32_t g_min_fastcdc_chunk_size;
static uint32_t g_max_fastcdc_chunk_size;
static uint32_t g_expect_fastcdc_chunk_size;

static uint64_t MaskS;
static uint64_t MaskL;

void fastcdc_init(){
    int index;
    int mask_bits;

    gear_matrix_init();

//    g_min_fastcdc_chunk_size = 2048;
//    g_max_fastcdc_chunk_size = 65536;
//    g_expect_fastcdc_chunk_size = expectCS;
    g_min_fastcdc_chunk_size = destor.chunk_min_size;
    g_max_fastcdc_chunk_size = destor.chunk_max_size;
    g_expect_fastcdc_chunk_size = destor.chunk_avg_size;
    index = log2(g_expect_fastcdc_chunk_size);
    assert(index>6);
    assert(index<17);
    mask_bits = destor.chunk_mask_bits > 0 ? destor.chunk_mask_bits : index - 1;
    assert(mask_bits > 1);
    assert(mask_bits < 17);
    if (destor.chunk_mask_bits > 0) {
        g_expect_fastcdc_chunk_size = 1U << (mask_bits + 1);
    }
    MaskS = g_condition_mask[mask_bits + 1];
    MaskL = g_condition_mask[mask_bits - 1];
}


int fastcdc_chunk_data(unsigned char *p, int n){

    uint64_t fingerprint=0;
    //uint64_t digest __attribute__((unused));
    //int i=g_min_fastcdc_chunk_size;//, Mid=g_min_fastcdc_chunk_size + 8*1024;
    int i=0;//, Mid=g_min_fastcdc_chunk_size + 8*1024;
    int Mid = g_expect_fastcdc_chunk_size;
    //return n;

        if(n<=g_min_fastcdc_chunk_size) { //the minimal  subChunk Size.
        return n;
        }
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
