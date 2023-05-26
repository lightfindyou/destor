/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef SIMILARITY_H_
#define SIMILARITY_H_

#include "../destor.h"
#include "../featuring/featuring.h"

void fineness_similariting_init();
struct chunk* fineness_similariting(struct chunk* c);

struct chunk* bruteforce_similariting(struct chunk* c);

void fineness_insert_flatFea(struct chunk* c);
struct chunk* fineness_similariting_flatFea(struct chunk* c);

GHashTable* ntransform_sufeature_tab;
void ntransform_similariting_init();
struct chunk* ntransform_similariting(struct chunk* c);

struct chunk* deepsketch_similariting(struct chunk* c);

//GHashTable* highdedup_sufeature_tab;
struct chunk* searchMostSimiChunk(GHashTable* cand_tab, struct chunk* c, int* curMaxHit,
                                 fpp curCandC, struct chunk* excludedChunk);
void highdedup_similariting_init();
struct chunk* highdedup_similariting(struct chunk* c);

GHashTable* odess_sufeature_tab;
void odess_similariting_init();
struct chunk* odess_similariting(struct chunk* c);
struct chunk* odess_similariting_flatFea(struct chunk* c);

void sec_most_match_similariting(struct chunk* c, int suFeaNum, GHashTable* sufea_tab, struct chunk* baseChunk);


void deepsketch_similariting_init();
struct chunk* deepsketch_similariting(struct chunk* c);

void fineANN_similariting_init();
struct chunk* fineANN_similariting(struct chunk* c);

void statis_similariting_init();
struct chunk* statis_similariting(struct chunk* c);

void weightchunk_similariting_init();
struct chunk* weightchunk_similariting(struct chunk* c);

#ifdef __cplusplus
extern "C"
{
#endif

void deepsketch_ANN_init();
struct chunk* deepsketch_ANN_similariting(struct chunk* c);

#ifdef __cplusplus
}
#endif

#endif
