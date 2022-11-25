/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef FEATURE_H_
#define FEATURE_H_

#include "../destor.h"

void deepsketch_featuring(unsigned char* buf, int size, struct chunk* c);

extern const int maMatrix[12][2];
#define FINESSE_FEATURE_NUM 12
#define FINESSE_SF_NUM 4
void rabinhash_rabin_init();
void rabin_finesse(unsigned char *p, int n, feature* fea);
void finesse_featuring(unsigned char* buf, int size, struct chunk* c);

#define NTRANSFORM_FEATURE_NUM 12
#define NTRANSFORM_SF_NUM 4
void rabin_ntransform(unsigned char *p, int size, sufeature* sf, int sfnum);
void ntransform_featuring(unsigned char* buf, int size, struct chunk* c);

//#define HIGHDEDUP_FEATURE_NUM 12
//#define HIGHDEDUP_FEATURE_MASK (0xffffffffffffffff)
#define HIGHDEDUP_FEATURE_NUM 32
//#define HIGHDEDUP_FEATURE_MASK (0xffff)
#define HIGHDEDUP_FEATURE_MASK (0xffffffffffffffff)
void gearhash_gear_init(int featureNumber);
int gear_max_highdedup_12fea_64B_max(unsigned char *p, int n, feature* fea, int maxFeaNum, unsigned long feaMask);
int gear_max_highdedup_32fea_16B_max(unsigned char *p, int n, feature* fea, int maxFeaNum, unsigned long feaMask);
int gear_max_highdedup_32fea_16B_xxhash(unsigned char *p, int n, feature* fea, int maxFeaNum, unsigned long feaMask);
void highdedup_featuring(unsigned char* buf, int size, struct chunk* c);


#define ODESS_FEATURE_NUM 12
#define ODESS_SF_NUM 4
void gear_odess(unsigned char *p, int n, feature* fea, int fetureNum);
void odess_featuring(unsigned char* buf, int size, struct chunk* c);
#endif
