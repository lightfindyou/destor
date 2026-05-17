#ifndef RABIN_SHARED_H_
#define RABIN_SHARED_H_

#include <stdint.h>

#define FINGERPRINT_PT 0xbfe6b8a5bf378d83LL
#define BREAKMARK_VALUE 0x78

extern uint64_t U[256];
extern int shift;
extern uint64_t T[256];
extern size_t _last_pos;
extern size_t _cur_pos;
extern unsigned int _num_chunks;

void window_init(uint64_t poly);

#define RABIN_SLIDE(m, fp, bufPos, buf) do { \
		unsigned char om; \
		uint64_t x; \
		if (++bufPos >= 48) \
			bufPos = 0; \
		om = buf[bufPos]; \
		buf[bufPos] = m; \
		fp ^= U[om]; \
		x = fp >> shift; \
		fp <<= 8; \
		fp |= m; \
		fp ^= T[x]; \
	} while (0)

#endif