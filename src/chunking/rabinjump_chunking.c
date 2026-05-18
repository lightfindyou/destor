#include "chunking.h"
#include "gear_common.h"
#include "rabin_shared.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef CHUNKING_INIT_DEBUG
#define CHUNKING_INIT_DEBUG 0
#endif

static int chunkMax, chunkAvg, chunkMin;
static unsigned long mask;
static unsigned long jumpMask;
static int jumpLen;

void rabinJump_init(int chunkSize) {
	window_init(FINGERPRINT_PT);
	_last_pos = 0;
	_cur_pos = 0;
	windows_reset();
	_num_chunks = 0;
	chunkAvg = chunkSize;
	chunkMax = chunkSize * 2;
	chunkMin = chunkSize / 8;

	int index = log2(chunkAvg);
	assert(index > 6);
	assert(index < 17);
	mask = g_condition_mask[index - 1];
	jumpMask = g_condition_mask[index - 2];
	jumpLen = chunkAvg / 2;

	if (CHUNKING_INIT_DEBUG) {
		printf("Mask:    %16lx\n", mask);
		printf("jumpMask:%16lx\n", jumpMask);
		printf("jumpLen:%d\n\n", jumpLen);
	}
}

int rabinjump_chunk_data(unsigned char *p, int n) {
	uint64_t fingerprint = 0;
	int i = 1, bufPos = -1;
	unsigned char buf[128];

	memset((char*) buf, 0, 128);

	if (n <= chunkMin)
		return n;
	else
		i = chunkMin;

	for (int k = i - 32; k < i; k++) {
		RABIN_SLIDE(p[k - 1], fingerprint, bufPos, buf);
	}

	int end = n > chunkMax ? chunkMax : n;
	while (i < end) {
		RABIN_SLIDE(p[i - 1], fingerprint, bufPos, buf);
		i++;
		if (__glibc_unlikely(!(fingerprint & jumpMask))) {
			if (!(fingerprint & mask)) {
				break;
			} else {
				fingerprint = 0;
				i += jumpLen;
				bufPos = i - 1;
			}
		}
	}

	return i < n ? i : n;
}