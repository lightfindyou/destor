/*
 * Author: Yucheng Zhang
 * See his INFOCOM paper for more details.
 */

//#define ae_memcmp(x, y) 
//     ({ 
//        int __ret; 
//        unsigned long __a = __builtin_bswap64(*((unsigned long *) x)); 
//        unsigned long __b = __builtin_bswap64(*((unsigned long *) y)); 
//        if (__a > __b) 
//              __ret = 1; 
//        else 
//              __ret = -1; 
//        __ret;
//      })

#define ae_memcmp(x, y) \
     ({ \
        int __ret; \
        unsigned long __a = (*((unsigned long *) x)); \
        unsigned long __b = (*((unsigned long *) y)); \
        if (__a > __b) \
              __ret = 1; \
        else \
              __ret = -1; \
        __ret;\
      })

static int window_size = 0;

static int chunkMax, chunkAvg, chunkMin;
/*
 * Calculating the window size
 */
void ae_init(int chunkSize){
	double e = 2.718281828;
	chunkAvg = chunkSize;
	chunkMax = chunkSize*2;
	chunkMin = chunkSize/8;
	window_size = chunkAvg/(e-1);
}

/** n is the size of string p. */
int ae_chunk_data(unsigned char *p, int n) {
	/*
	 * curr points to the current position;
	 * max points to the position of max value;
	 * end points to the end of buffer.
	 */
	unsigned char *curr = p+1, *max = p, *end = p+n-8;
//	unsigned long maxValue = (*((unsigned long *)max));

	if (n <= window_size + 8)
		return n;

	for (; curr <= end; curr++) {
		int comp_res = ae_memcmp(curr, max);
//		int comp_res = (
//			{ int __ret;
//			unsigned long __a = (*((unsigned long *) curr));
//			//unsigned long __b = (*((unsigned long *) max));
//			if (__a > maxValue ) __ret = 1;
//			else __ret = -1;
//			__ret;
//			});
		/** two assigment, one if
		 * for 512 SIMD registers(64byte, 8*8byte),
		 * ae: 16 asigment, 8 if
		 * ae-simd 9 assigment, one compare
		 */
//		{	
//			int __ret;
//			unsigned long __a = (*((unsigned long *) curr));
//			unsigned long __b = (*((unsigned long *) max));
//			if (__a > __b) __ret = 1;
//			else __ret = -1;
//			__ret;
//		}
		if (comp_res < 0) {
			max = curr;
//			maxValue = (*((unsigned long *)max));
			continue;
		}
		if (curr == max + window_size || curr == p + chunkMax)
			return curr - p;
	}
	return n;
}

#define ae_memcmp(x, y) \
     ({ \
        int __ret; \
        unsigned long __a = (*((unsigned long *) x)); \
        unsigned long __b = (*((unsigned long *) y)); \
        if (__a > __b) \
              __ret = 1; \
        else \
              __ret = -1; \
        __ret;\
      })

int expectedBiggerLen;
void sc_init() {
    int size = chunkAvg;
	expectedBiggerLen = 0;
	while(size){
		size >>= 1;
		expectedBiggerLen++;
	}
}

/*Sequencial Content compare*/
/** n is the size of string p. */
int sc_chunk_data(unsigned char *p, int n) {
	/*
	 * curr points to the current position;
	 * end points to the end of buffer.
	 */
	unsigned char *curr = p++, *end = p+n-8;
	int curBiggerLen = 0;

	if (n <= expectedBiggerLen)
		return n;

	for (; curr <= end; curr++) {
		int comp_res = ae_memcmp((curr-1), curr);
		if (comp_res > 0) {
			curBiggerLen++;
			if(curBiggerLen == expectedBiggerLen) {
				return (curr - p - 1);
			}
		}
		curBiggerLen = 0;
	}
	return n;
}
