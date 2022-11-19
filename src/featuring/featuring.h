/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef FEATURE_H_
#define FEATURE_H_

#include "destor.h"

#define FINESSE_FEATURE_NUM 12
#define FINESSE_SF_NUM 4
#define NTRANSFORM_FEATURE_NUM 12
#define NTRANSFORM_SF_NUM 4

void* finesse_featuring(unsigned char* buf, int size, unsigned char* fea);
void* ntransform_featuring(unsigned char* buf, int size, unsigned char* fea);
void* deepsketch_featuring(unsigned char* buf, int size, unsigned char* fea);
feature rabin_maxfp(unsigned char *p, int n);
void rabin_ntransform(unsigned char *p, int size, sufeature* sf, int sfnum);

#endif
