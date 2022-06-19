#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#include "chunking.h"

#define MSB64 0x8000000000000000LL
#define MAXBUF (128*1024)

#define FINGERPRINT_PT  0xbfe6b8a5bf378d83LL
#define BREAKMARK_VALUE 0x78

#define SLIDE(m,fp,bufPos,buf) do{	\
	    unsigned char om;   \
	    unsigned long x;	 \
		if (++bufPos >= size)  \
        bufPos = 0;				\
        om = buf[bufPos];		\
        buf[bufPos] = m;		 \
		fp ^= U[om];	 \
		x = fp >> shift;  \
		fp <<= 8;		   \
		fp |= m;		  \
		fp ^= T[x];	 \
}while(0)

typedef unsigned int UINT32;
typedef unsigned long long int UINT64;
//time_t backup_now;

enum {
	size = 48
};
UINT64 fp;
int bufpos;
UINT64 U[256];
unsigned char buf[size];
int shift;
UINT64 T[256];
UINT64 poly;

unsigned long _last_pos;
unsigned long _cur_pos;

unsigned int _num_chunks;

const char bytemsb[0x100] = { 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5,
		5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6,
		6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7,
		7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
		7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
		8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, };

/***********************************************the rabin**********************************************/
static unsigned int fls32(UINT32 v) {
	if (v & 0xffff0000) {
		if (v & 0xff000000)
			return 24 + bytemsb[v >> 24];
		else
			return 16 + bytemsb[v >> 16];
	}
	if (v & 0x0000ff00)
		return 8 + bytemsb[v >> 8];
	else
		return bytemsb[v];
}

static unsigned int fls64(UINT64 v) {
	UINT32 h;
	if ((h = v >> 32))
		return 32 + fls32(h);
	else
		return fls32((UINT32) v);
}

UINT64 polymod(UINT64 nh, UINT64 nl, UINT64 d) {

	int k = fls64(d) - 1;
	int i;

	//printf ("polymod : k = %d\n", k);

	d <<= 63 - k;

	//printf ("polymod : d = %llu\n", d);
	//printf ("polymod : MSB64 = %llu\n", MSB64);

	if (nh) {
		if (nh & MSB64)
			nh ^= d;

		//printf ("polymod : nh = %llu\n", nh);

		for (i = 62; i >= 0; i--)
			if (nh & ((UINT64) 1) << i) {
				nh ^= d >> (63 - i);
				nl ^= d << (i + 1);

				//printf ("polymod : i = %d\n", i);
				//printf ("polymod : shift1 = %llu\n", (d >> (63 - i)));
				//printf ("polymod : shift2 = %llu\n", (d << (i + 1)));
				//printf ("polymod : nh = %llu\n", nh);
				//printf ("polymod : nl = %llu\n", nl);

			}
	}
	for (i = 63; i >= k; i--) {
		if (nl & (long long int) (1) << i)
			nl ^= d >> (63 - i);

		//printf ("polymod : nl = %llu\n", nl);

	}

	//printf ("polymod : returning %llu\n", nl);

	return nl;
}

void polymult(UINT64 *php, UINT64 *plp, UINT64 x, UINT64 y) {

	int i;

	//printf ("polymult (x %llu y %llu)\n", x, y);

	UINT64 ph = 0, pl = 0;
	if (x & 1)
		pl = y;
	for (i = 1; i < 64; i++)
		if (x & ((long long int) (1) << i)) {

			//printf ("polymult : i = %d\n", i);
			//printf ("polymult : ph = %llu\n", ph);
			//printf ("polymult : pl = %llu\n", pl);
			//printf ("polymult : y = %llu\n", y);
			//printf ("polymult : ph ^ y >> (64-i) = %llu\n", (ph ^ y >> (64-i)));
			//printf ("polymult : pl ^ y << i = %llu\n", (pl ^ y << i));

			ph ^= y >> (64 - i);
			pl ^= y << i;

			//printf ("polymult : ph %llu pl %llu\n", ph, pl);

		}
	if (php)
		*php = ph;
	if (plp)
		*plp = pl;

	//printf ("polymult : h %llu l %llu\n", ph, pl);

}

UINT64 append8(UINT64 p, unsigned char m) {
	return ((p << 8) | m) ^ T[p >> shift];
}

UINT64 slide8(unsigned char m) {
	unsigned char om;
	//printf("this char is %c\n",m);
	if (++bufpos >= size)
		bufpos = 0;
	om = buf[bufpos];
	buf[bufpos] = m;
	return fp = append8(fp ^ U[om], m);
}

UINT64 polymmult(UINT64 x, UINT64 y, UINT64 d) {

	//printf ("polymmult (x %llu y %llu d %llu)\n", x, y, d);

	UINT64 h, l;
	polymult(&h, &l, x, y);
	return polymod(h, l, d);
}

void calcT(UINT64 poly) {

	int j;
	UINT64 T1;

	//printf ("rabinpoly::calcT ()\n");

	int xshift = fls64(poly) - 1;
	shift = xshift - 8;
	T1 = polymod(0, (long long int) (1) << xshift, poly);
	for (j = 0; j < 256; j++) {
		T[j] = polymmult(j, T1, poly) | ((UINT64) j << xshift);

		//printf ("rabinpoly::calcT tmp = %llu\n", polymmult (j, T1, poly));
		//printf ("rabinpoly::calcT shift = %llu\n", ((UINT64) j << xshift));
		//printf ("rabinpoly::calcT xshift = %d\n", xshift);
		//printf ("rabinpoly::calcT T[%d] = %llu\n", j, T[j]);

	}

	//printf ("rabinpoly::calcT xshift = %d\n", xshift);
	//printf ("rabinpoly::calcT T1 = %llu\n", T1);
	//printf ("rabinpoly::calcT T = {");
	//for (i=0; i< 256; i++)
	//printf ("\t%llu \n", T[i]);
	//printf ("}\n");

}

void rabinpoly_init(UINT64 p) {
	poly = p;
	calcT(poly);
}

void window_init(UINT64 poly) {

	int i;
	UINT64 sizeshift;

	rabinpoly_init(poly);
	fp = 0;
	bufpos = -1;
	sizeshift = 1;
	for (i = 1; i < size; i++)
		sizeshift = append8(sizeshift, 0);
	for (i = 0; i < 256; i++)
		U[i] = polymmult(i, sizeshift, poly);
	memset((char*) buf, 0, sizeof(buf));
}

void windows_reset() {
	fp = 0;
	//memset((char*) buf,0,sizeof (buf));
	//memset((char*) chunk,0,sizeof (chunk));
}

static int rabin_mask = 0;

static int chunkMax, chunkAvg, chunkMin;
extern unsigned long g_condition_mask[];
extern unsigned long Mask, jumpMask;
extern int jumpLen;

void chunkAlg_init(int chunkSize) {
	window_init(FINGERPRINT_PT);
	_last_pos = 0;
	_cur_pos = 0;
	windows_reset();
	_num_chunks = 0;
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
}

void rabinJump_init(int chunkSize) {
	window_init(FINGERPRINT_PT);
	_last_pos = 0;
	_cur_pos = 0;
	windows_reset();
	_num_chunks = 0;
	chunkAvg = chunkSize;
	chunkMax = chunkSize*2;
	chunkMin = chunkSize/8;
	rabin_mask = chunkAvg - 1;

    int index = log2(chunkAvg);
    assert(index>6);
    assert(index<17);
    Mask = g_condition_mask[index-1];
    jumpMask = g_condition_mask[index-2];
    jumpLen = chunkAvg/2;

    printf("Mask:    %16lx\n", Mask);
    printf("jumpMask:%16lx\n", jumpMask);
    printf("jumpLen:%d\n\n", jumpLen);
}


/* The standard rabin chunking */
int rabin_chunk_data(unsigned char *p, int n) {

	UINT64 fp = 0;
	int i = 1, bufPos = -1;

	unsigned char buf[128];
	memset((char*) buf, 0, 128);

	if (n <= chunkMin)
		return n;
	else
		i = chunkMin;

	for(int k = i-32; k<i; k++){
		SLIDE(p[k - 1], fp, bufPos, buf);
//		do{
//			unsigned char om;
//			unsigned long x;
//			if (++bufPos >= size) bufPos = 0;
//			om = buf[bufPos];
//			buf[bufPos] = p[k - 1];
//			fp ^= U[om];
//			x = fp >> shift;
//			fp <<= 8;
//			fp |= p[k - 1];
//			fp ^= T[x];
//		}while(0);
	}

	int end = n > chunkMax ? chunkMax : n;
	while (i < end) {

		SLIDE(p[i - 1], fp, bufPos, buf);
		if ((fp & rabin_mask) == BREAKMARK_VALUE)
			break;
		i++;
	}
	return i;
}

int rabinjump_chunk_data(unsigned char *p, int n) {

//	printf("p:%p\n", p);
	UINT64 fp = 0;
//	UINT64 fp = ULLONG_MAX;
	int i = 1, bufPos = -1;

	unsigned char buf[128];
	memset((char*) buf, 0, 128);

	if (n <= chunkMin)
		return n;
	else
		i = chunkMin;

	//pre-calculate
	//fingerprint of rabin is small at begin, pre-calculate to avoid small chunks.
	for(int k = i-32; k<i; k++){
		SLIDE(p[k - 1], fp, bufPos, buf);
	}

	int end = n > chunkMax ? chunkMax : n;
	while (i < end) {

		SLIDE(p[i - 1], fp, bufPos, buf);
		i++;
        if(__glibc_unlikely(!(fp & jumpMask)) ){
            if ((!(fp & Mask))) { //AVERAGE*2, *4, *8
				break;
            } else {
                //TODO xzjin here need to set the fingerprint to 0 ?
				fp = 0;
                i += jumpLen;
				bufPos = i - 1;
            }
        }
	}
//	printf("fp: %llx, chunk length: %d\n", fp, i);
    return i<n?i:n;
}
/*
 * A variant of rabin chunking.
 * We use a larger avg chunk size when the current size is small,
 * and a smaller avg chunk size when the current size is large.
 * */
int normalized_rabin_chunk_data(unsigned char *p, int n) {

	UINT64 fp = 0;
	int i = 1, bufPos = -1;

	unsigned char buf[128];
	memset((char*) buf, 0, 128);

	if (n <= chunkMin)
		return n;
	else
		i = chunkMin;

	int small_mask = chunkAvg*2 - 1;
	int large_mask = chunkAvg/2 - 1;
	int end = n > chunkMax ? chunkMax : n;
	while (i < end) {

		SLIDE(p[i - 1], fp, bufPos, buf);

		if (i < chunkAvg) {
			if ((fp & small_mask) == BREAKMARK_VALUE)
				break;
			i++;
		} else {
			if ((fp & large_mask) == BREAKMARK_VALUE)
				break;
			i++;
		}

	}
	return i;
}

/*
 * TTTD from HP
 * See their paper:
 * 	A Framework for Analyzing and Improving Content-Based Chunking Algorithms
 */
int tttd_chunk_data(unsigned char *p, int n) {

	UINT64 fingerprint = 0;
	int i = 1, bufPos = -1, m = 0;

	unsigned char buf[128];
	memset((char*) buf, 0, 128);

	if (n <= chunkMin)
		return n;
	else
		i = chunkMin;

	int back_mask = chunkAvg/2 - 1;
	int end = n > chunkMax ? chunkMax : n;
	while (i < end) {

		SLIDE(p[i - 1], fingerprint, bufPos, buf);
		if ((fingerprint &  back_mask) == BREAKMARK_VALUE) {
			if ((fingerprint & rabin_mask) == BREAKMARK_VALUE)
				return i;
			m = i;
		}

		i++;
	}
	if (m != 0)
		return m;
	else
		return i;
}
