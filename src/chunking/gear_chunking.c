#include "chunking.h"
#include "gear_common.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#ifndef CHUNKING_INIT_DEBUG
#define CHUNKING_INIT_DEBUG 0
#endif

static uint64_t mask;
static uint64_t back_mask_tttd;

void gear_init() {
	gear_matrix_init();

	int index = log2(destor.chunk_avg_size);
	assert(index > 6);
	assert(index < 17);
	mask = g_condition_mask[index];
	back_mask_tttd = g_condition_mask[index - 1];

	if (CHUNKING_INIT_DEBUG) {
		printf("\nMask:  %16lx\n", mask);
	}
}

int gear_chunk_data(unsigned char *p, int n) {
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

		if (G_UNLIKELY(!(fingerprint & mask))) {
			return i;
		}
	}
	return n;
}

int TTTD_gear_chunk_data(unsigned char *p, int n) {
	uint64_t fingerprint = 0;
	int i = 0, m = 0;
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

		if (G_UNLIKELY(!(fingerprint & back_mask_tttd))) {
			if (!(fingerprint & mask)) {
				return i;
			}
			m = i;
		}
	}

	if (m != 0)
		return m;
	else
		return i;
}