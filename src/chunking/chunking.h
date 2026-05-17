/*
 * Public chunking interfaces grouped by algorithm family.
 */

#ifndef CHUNK_H_
#define CHUNK_H_

#include "../destor.h"

/* Rabin family */
void windows_reset();
void chunkAlg_init();
int rabin_chunk_data(unsigned char *p, int n);
int normalized_rabin_chunk_data(unsigned char *p, int n);
int tttd_chunk_data(unsigned char *p, int n);
void rabinJump_init(int chunkSize);
int rabinjump_chunk_data(unsigned char *p, int n);

/* AE and SC */
void ae_init();
int ae_chunk_data(unsigned char *p, int n);
void sc_init();
int sc_chunk_data(unsigned char *p, int n);

/* FastCDC */
void fastcdc_init();
int fastcdc_chunk_data(unsigned char *p, int n);

/* FastCDC GPU fallback wrapper */
int fastcdc_gpu_init();
void fastcdc_gpu_close();
int fastcdc_gpu_chunk_data(unsigned char *p, int n);

/* JC GPU fallback wrapper */
int jc_gpu_init();
void jc_gpu_close();
int jc_gpu_chunk_data(unsigned char *p, int n);

/* Gear family */
void gear_init();
int gear_chunk_data(unsigned char *p, int n);
int TTTD_gear_chunk_data(unsigned char *p, int n);

#if SENTEST
void gearjump_init(int i);
#else
void gearjump_init();
#endif	//SENTEST
int gearjump_chunk_data(unsigned char *p, int n);
int gearjumpTTTD_chunk_data(unsigned char *p, int n);

void normalized_gearjump_init(int mto);
int normalized_gearjump_chunk_data(unsigned char *p, int n);

/* Leap */
void leap_init(int chunkSize, int parIdx);
int leap_chunk_data(unsigned char *p, int n);

#endif
