#include "chunking.h"
#include "gear_common.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#ifndef CHUNKING_INIT_DEBUG
#define CHUNKING_INIT_DEBUG 0
#endif

static uint32_t gearjumpChunkSize;
static uint64_t mask;
static uint64_t jumpMask;
static int jumpLen;
static uint64_t largeMask;
static uint64_t largeJumpMask;
static int largeJumpLen;

/**
 * mto means "Mask Ones Less Than Chunk Ones"
 */
#if SENTEST
void gearjump_init(int mto) {
#else
void gearjump_init() {
#endif
	gear_matrix_init();

	gearjumpChunkSize = destor.chunk_avg_size;
	int index = log2(gearjumpChunkSize);
	int jOnes = 0, cOnes = index - 1;
	assert(index > 6);
	assert(index < 17);
	mask = g_condition_mask[cOnes];
#if SENTEST
	assert(mto > 0);
	assert(mto < (cOnes));
	jOnes = cOnes - mto;
	jumpMask = g_condition_mask[jOnes];
	jumpLen = pow(2, (cOnes + jOnes)) / (pow(2, cOnes) - pow(2, jOnes));
	if (CHUNKING_INIT_DEBUG) {
		printf("cOnes:%d, jOnes:%d, jumpLen:%d.\n", cOnes, jOnes, jumpLen);
	}
#else
	jumpMask = g_condition_mask[index - 2];
	jumpLen = gearjumpChunkSize / 2;
#endif

	if (CHUNKING_INIT_DEBUG) {
		printf("\nMask:  %16lx\n", mask);
		printf("jumpMask:%16lx\n", jumpMask);
		printf("jumpLen:%d\n\n", jumpLen);
	}
}

#define CHUNKMIN 0

int gearjump_chunk_data(unsigned char *p, int n) {
	uint64_t fingerprint = 0;
	int i = 0;
	int minSize = destor.chunk_min_size;

	if (n <= minSize)
		return n;
#if !CHUNKMIN
	else
		i = minSize;
#endif
	n = n < destor.chunk_max_size ? n : destor.chunk_max_size;

	while (i < n) {
		fingerprint = (fingerprint << 1) + (g_gear_matrix[p[i]]);
		i++;

		if (G_UNLIKELY(!(fingerprint & jumpMask))) {
			if (!(fingerprint & mask)) {
#if CHUNKMIN
				if (i < minSize) {
					i += 2048;
					continue;
				}
#endif
				return i;
			} else {
				fingerprint = 0;
				i += jumpLen;
			}
		}
	}

	return i < n ? i : n;
}

int gearjumpTTTD_chunk_data(unsigned char *p, int n) {
	uint64_t fingerprint = 0;
	int i = 0, m = 0;
	int minSize = destor.chunk_min_size;

	if (n <= minSize)
		return n;
	else
		i = minSize;
	unsigned long end = n < destor.chunk_max_size ? n : destor.chunk_max_size;

	while (i < end) {
		fingerprint = (fingerprint << 1) + (g_gear_matrix[p[i]]);
		i++;

		if (__glibc_unlikely(!(fingerprint & jumpMask))) {
			if (!(fingerprint & mask)) {
				return i;
			}
			m = i;
			fingerprint = 0;
			i += jumpLen;
		}
	}

	if (m != 0) {
		return m;
	}
	return i < end ? i : end;
}

void normalized_gearjump_init(int mto) {
	gear_matrix_init();

	gearjumpChunkSize = destor.chunk_avg_size;
	int index = log2(gearjumpChunkSize);
	int jOnes = 0, cOnes = index - 2;
	assert(index > 6);
	assert(index < 17);
	mask = g_condition_mask[cOnes];
	largeMask = g_condition_mask[cOnes + 2];
#if SENTEST
	assert(mto > 0);
	assert(mto < (cOnes));
	jOnes = cOnes - mto;
	jumpMask = g_condition_mask[jOnes];
	largeJumpMask = g_condition_mask[jOnes + 2];
	jumpLen = pow(2, (cOnes + jOnes)) / (pow(2, cOnes) - pow(2, jOnes));
	largeJumpLen = pow(2, (cOnes + 2 + jOnes + 2)) / (pow(2, cOnes + 2) - pow(2, jOnes + 2));
	if (CHUNKING_INIT_DEBUG) {
		printf("cOnes:%d, jOnes:%d, jumpLen:%d.\n", cOnes, jOnes, jumpLen);
	}
#else
	jumpMask = g_condition_mask[index - 2];
	jumpLen = gearjumpChunkSize / 2;
#endif

	if (CHUNKING_INIT_DEBUG) {
		printf("\n  Mask:%16lx\t    largeMask:%16lx\n", mask, largeMask);
		printf("jumpMask:%16lx\tlargejumpMask:%16lx\n", jumpMask, largeJumpMask);
		printf(" jumpLen:%d\t    largeJumpLen:%d\n\n", jumpLen, largeJumpLen);
	}
}

int normalized_gearjump_chunk_data(unsigned char *p, int n) {
	uint64_t fingerprint = 0;
	int i = 0;
	int minSize = destor.chunk_min_size;
	int middle = destor.chunk_avg_size < n ? destor.chunk_avg_size : n;

	if (n <= minSize)
		return n;
#if !CHUNKMIN
	else
		i = minSize;
#endif
	n = n < destor.chunk_max_size ? n : destor.chunk_max_size;

	while (i < middle) {
		fingerprint = (fingerprint << 1) + (g_gear_matrix[p[i]]);
		i++;

		if (G_UNLIKELY(!(fingerprint & largeJumpMask))) {
			if (!(fingerprint & largeMask)) {
				return i;
			} else {
				fingerprint = 0;
				i += largeJumpLen;
			}
		}
	}

	while (i < n) {
		fingerprint = (fingerprint << 1) + (g_gear_matrix[p[i]]);
		i++;

		if (G_UNLIKELY(!(fingerprint & jumpMask))) {
			if (!(fingerprint & mask)) {
				return i;
			} else {
				fingerprint = 0;
				i += jumpLen;
			}
		}
	}

	return i < n ? i : n;
}