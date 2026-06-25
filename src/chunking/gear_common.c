#include "gear_common.h"

#include <memory.h>
#include <openssl/md5.h>
#include <pthread.h>

enum {
	GEAR_SYMBOL_COUNT = 256,
	GEAR_DIGEST_LENGTH = 16,
	GEAR_SEED_LENGTH = 64,
};

uint64_t g_gear_matrix[GEAR_SYMBOL_COUNT];

unsigned long g_condition_mask[] = {
		0x0000000000000000,
		0x0000000001000000,
		0x0000000003000000,
		0x0000010003000000,
		0x0000090003000000,
		0x0000190003000000,
		0x0000590003000000,
		0x0000590003100000,
		0x0000590003500000,
		0x0000590003510000,
		0x0000590003530000,
		0x0000590103530000,
		0x0000d90103530000,
		0x0000d90303530000,
		0x0000d90303531000,
		0x0000d90303533000,
		0x0000d90303537000,
		0x0000d90703537000,
};

static pthread_once_t g_gear_matrix_once = PTHREAD_ONCE_INIT;

static void gear_matrix_init_impl(void) {
	char seed[GEAR_SEED_LENGTH];

	for (int i = 0; i < GEAR_SYMBOL_COUNT; i++) {
		for (int j = 0; j < GEAR_SEED_LENGTH; j++) {
			seed[j] = i;
		}

		g_gear_matrix[i] = 0;
		unsigned char md5_result[GEAR_DIGEST_LENGTH];

		MD5_CTX md5_ctx;
		MD5_Init(&md5_ctx);
		MD5_Update(&md5_ctx, seed, GEAR_SEED_LENGTH);
		MD5_Final(md5_result, &md5_ctx);

		memcpy(&g_gear_matrix[i], md5_result, sizeof(uint64_t));
	}
}

void gear_matrix_init() {
	pthread_once(&g_gear_matrix_once, gear_matrix_init_impl);
}