/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef CHUNK_H_
#define CHUNK_H_

#include "destor.h"

void windows_reset();
void chunkAlg_init();
struct chunk* rabin_chunk_data(unsigned char *p, int n);
struct chunk* normalized_rabin_chunk_data(unsigned char *p, int n);
void rabinJump_init(int chunkSize);
struct chunk* rabinjump_chunk_data(unsigned char *p, int n);

void ae_init();
struct chunk* ae_chunk_data(unsigned char *p, int n);
struct chunk* sc_chunk_data(unsigned char *p, int n);

struct chunk* tttd_chunk_data(unsigned char *p, int n);

void fastcdc_init();
struct chunk* fastcdc_chunk_data(unsigned char *p, int n);

void gear_init();
struct chunk* gear_chunk_data(unsigned char *p, int n);
struct chunk* TTTD_gear_chunk_data(unsigned char *p, int n);

#if SENTEST
void gearjump_init(int i);
#else
void gearjump_init();
#endif	//SENTEST
struct chunk* gearjump_chunk_data(unsigned char *p, int n);
struct chunk* gearjumpTTTD_chunk_data(unsigned char *p, int n);

void normalized_gearjump_init(int mto);
struct chunk* normalized_gearjump_chunk_data(unsigned char *p, int n);

void sc_init();
struct chunk* sc_chunk_data(unsigned char *p, int n);


void leap_init(int chunkSize, int parIdx);
struct chunk* leap_chunk_data(unsigned char *p, int n);

void highdedup_chunk_init();
struct chunk* highdedup_chunk_data(unsigned char *p, int n);
#endif
