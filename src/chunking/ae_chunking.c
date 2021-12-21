/*
 * Author: Yucheng Zhang
 * See his INFOCOM paper for more details.
 */

#include "../destor.h"

//#define my_memcmp(x, y) \
//     ({ \
//        int __ret; \
//        uint64_t __a = __builtin_bswap64(*((uint64_t *) x)); \
//        uint64_t __b = __builtin_bswap64(*((uint64_t *) y)); \
//        if (__a > __b) \
//              __ret = 1; \
//        else \
//              __ret = -1; \
//        __ret;\
//      })

#define my_memcmp(x, y) \
     ({ \
        int __ret; \
        uint64_t __a = (*((uint64_t *) x)); \
        uint64_t __b = (*((uint64_t *) y)); \
        if (__a > __b) \
              __ret = 1; \
        else \
              __ret = -1; \
        __ret;\
      })

static int window_size = 0;

/*
 * Calculating the window size
 */
void ae_init(){
	double e = 2.718281828;
	window_size = destor.chunk_avg_size/(e-1);
}

/** n is the size of string p. */
int ae_chunk_data(unsigned char *p, int n) {
	/*
	 * curr points to the current position;
	 * max points to the position of max value;
	 * end points to the end of buffer.
	 */
	unsigned char *curr = p+1, *max = p, *end = p+n-8;
	uint64_t maxValue = (*((uint64_t *)max));

	if (n <= window_size + 8)
		return n;

	for (; curr <= end; curr++) {
//		int comp_res = my_memcmp(curr, max);
		int comp_res = (
			{ int __ret;
			uint64_t __a = (*((uint64_t *) curr));
			//uint64_t __b = (*((uint64_t *) max));
			if (__a > maxValue ) __ret = 1;
			else __ret = -1;
			__ret;
			});
		/** two assigment, one if
		 * for 512 SIMD registers(64byte, 8*8byte),
		 * ae: 16 asigment, 8 if
		 * ae-simd 9 assigment, one compare
		 */
//		{	
//			int __ret;
//			uint64_t __a = (*((uint64_t *) curr));
//			uint64_t __b = (*((uint64_t *) max));
//			if (__a > __b) __ret = 1;
//			else __ret = -1;
//			__ret;
//		}
		if (comp_res < 0) {
			max = curr;
			maxValue = (*((uint64_t *)max));
			continue;
		}
		if (curr == max + window_size || curr == p + destor.chunk_max_size)
			return curr - p;
	}
	return n;
}

/** n is the size of string p. */
int sc_chunk_data(unsigned char *p, int n) {
	/*
	 * curr points to the current position;
	 * max points to the position of max value;
	 * end points to the end of buffer.
	 */
	int err;
	int length = n>4096?4096:n;
	length = ((int)(length/8))*8;
	unsigned char *curr;

	void* buf = calloc(1, 512);
	err = error;
	if(!buf){
		printf("allocate memory error.\n");
		exit(-1);
	}

	for(curr=p, c = buf; curr < p; c++){

	}


	return n;
}
