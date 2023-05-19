/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef FEATURE_H_
#define FEATURE_H_

#include "../destor.h"

extern const int maMatrix[12][2];
#define FINESSE_FEATURE_NUM 12
#define FINESSE_SF_NUM 4
void rabinhash_rabin_init();
void rabin_finesse(unsigned char *p, int n, feature* fea);
void finesse_featuring(unsigned char* buf, int size, struct chunk* c);
void finesse_featuring_flatFea(unsigned char* buf, int size, struct chunk* c);

#define NTRANSFORM_FEATURE_NUM 12
#define NTRANSFORM_SF_NUM 4
void rabin_ntransform(unsigned char *p, int size, sufeature* sf, int sfnum);
void ntransform_featuring(unsigned char* buf, int size, struct chunk* c);

//#define HIGHDEDUP_FEATURE_NUM 12
//#define HIGHDEDUP_FEATURE_MASK (0xffffffffffffffff)
#define HIGHDEDUP_FEATURE_NUM 32
//#define HIGHDEDUP_FEATURE_MASK (0xffff)
#define HIGHDEDUP_FEATURE_MASK (0XFFFFFFFFFFFFFFFF)
void initGearMatrixFea();
void gearhash_gear_init(int featureNumber);

int gear_highdedup_max(unsigned char *p, int n, feature* fea, int maxFeaNum, unsigned long feaLenMask);
int gear_max_highdedup_12fea_64B_max(unsigned char *p, int n, feature* fea, int maxFeaNum, unsigned long feaMask);
int gear_max_highdedup_32fea_16B_max(unsigned char *p, int n, feature* fea, int maxFeaNum, unsigned long feaMask);
int gear_max_highdedup_32fea_16B_xxhash(unsigned char *p, int n, feature* fea, int maxFeaNum, unsigned long feaMask);
int highdedup_32fea_16B_FSC(unsigned char *p, int n, feature* fea, int maxFeaNum);
void highdedup_featuring(unsigned char* buf, int size, struct chunk* c);
void highdedup_featuring_fsc(unsigned char* buf, int size, struct chunk* c);

void bruteforce_featuring(unsigned char* buf, int size, struct chunk* c);

#define ODESS_FEATURE_NUM 12
#define ODESS_SF_NUM 4
void gear_odess(unsigned char *p, int n, feature* fea, int fetureNum);
void odess_featuring(unsigned char* buf, int size, struct chunk* c);
void odess_featuring_flatFea(unsigned char* buf, int size, struct chunk* c);


void deepsketch_featuring_init(char* modelPath);
void deepsketch_featuring(unsigned char* buf, int size, struct chunk* c);

typedef unsigned char fineANN_t;
#define FINEANN_FEATURE_BITS 191
#define FINEANN_MAX_FEATURE_BITS 256
#define FINEANN_FEATURE_LEN (FINEANN_MAX_FEATURE_BITS/8)+FINEANN_MAX_FEATURE_BITS

void gear_fineANN(unsigned char *p, int n, fineANN_t* fea, int featureNum);
void fineANN_featuring(unsigned char* buf, int size, struct chunk* c);

typedef uint64_t statis_t;
#define STATIS_FEATURE_NUM 6
void statis_featuring(unsigned char* buf, int size, struct chunk* c);
void gear_statis(unsigned char *p, int n, statis_t* fea, int featureNum);

#ifdef __cplusplus
extern "C"
{
#endif

void deepsketch_modelInit(char* modelPath);
void deepsketch_getHash(struct chunk* c, int chunkLen, char* hash);

#ifdef __cplusplus
}
#endif

#endif
