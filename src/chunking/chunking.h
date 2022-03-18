/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef CHUNK_H_
#define CHUNK_H_

#include "destor.h"

void windows_reset();
void chunkAlg_init();
int rabin_chunk_data(unsigned char *p, int n);
int normalized_rabin_chunk_data(unsigned char *p, int n);

void ae_init();
int ae_chunk_data(unsigned char *p, int n);
int sc_chunk_data(unsigned char *p, int n);

int tttd_chunk_data(unsigned char *p, int n);

void fastcdc_init();
int fastcdc_chunk_data(unsigned char *p, int n);

#if SENTEST
void gearjump_init(int i);
#else
void gearjump_init();
#endif	//SENTEST
int gearjump_chunk_data(unsigned char *p, int n);

void sc_init();
int sc_chunk_data(unsigned char *p, int n);


void leap_init();
int leap_chunk_data(unsigned char *p, int n);
#endif
