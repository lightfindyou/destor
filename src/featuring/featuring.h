/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef FEATURE_H_
#define FEATURE_H_

#include "destor.h"

void* deepsketch_featuring(unsigned char* buf, int size, struct chunk* c);

#define FINESSE_FEATURE_NUM 12
#define FINESSE_SF_NUM 4
void rabin_init();
feature rabin_finesse(unsigned char *p, int n);
static void finesse_featuring(unsigned char* buf, int size, struct chunk* c);

#define NTRANSFORM_FEATURE_NUM 12
#define NTRANSFORM_SF_NUM 4
void rabin_ntransform(unsigned char *p, int size, sufeature* sf, int sfnum);
void ntransform_featuring(unsigned char* buf, int size, struct chunk* c);

#define HIGHDEDUP_FEATURE_NUM 12
void gear_init(int featureNumber);
int gear_max_highdedup(unsigned char *p, int n, feature* fea, int maxFeaNum);
static void highdedup_featuring(unsigned char* buf, int size, struct chunk* c);
#endif
