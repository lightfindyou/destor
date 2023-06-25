/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef SIMILARITY_H_
#define SIMILARITY_H_

#include "../destor.h"
#include "../featuring/featuring.h"

#define INITCHUNKLISTSIZE 50
struct _chunkList {
	struct chunk** list;
	int capacity;
	int length;
};

typedef struct _chunkList chunkList;

void fineness_similariting_init();
void fineness_similariting(struct chunk* c);

void bruteforce_similariting(struct chunk* c);

void fineness_insert_flatFea(struct chunk* c);
void fineness_similariting_flatFea(struct chunk* c);

extern GHashTable* ntransform_sufeature_tab;
void ntransform_similariting_init();
void ntransform_similariting(struct chunk* c);

void deepsketch_similariting(struct chunk* c);

void insert_sufeature(struct chunk* c, int suFeaNum, GHashTable* sufea_tab);
void insert_sufeatureHashList(struct chunk* c, int suFeaNum, GHashTable* sufea_tab);
void insertFeaToTab(GHashTable* tab, struct chunk* c);
//GHashTable* commonSimiSufeatureTab;
gboolean true( gpointer key, gpointer value, gpointer user_data);
struct chunk* searchMostSimiChunk(GHashTable* cand_tab, struct chunk* c, int* curMaxHit,
                                 fpp curCandC);
void highdedup_similariting_init();
void highdedup_similariting(struct chunk* c);
void highdedup_similariting_stop();

extern GHashTable* odess_sufeature_tab;
void odess_similariting_init();
void odess_similariting(struct chunk* c);
void odess_similariting_flatFea(struct chunk* c);

struct chunk* first_match_similariting(struct chunk* c, int suFeaNum, GHashTable* sufea_tab);
struct chunk* most_match_similariting(struct chunk* c, int suFeaNum, GHashTable* sufea_tab);
struct chunk* topK_match_similariting(struct chunk* c, int suFeaNum);
void sec_most_match_similariting(struct chunk* c, int suFeaNum, GHashTable* sufea_tab, struct chunk* baseChunk);


void deepsketch_similariting_init();
void deepsketch_similariting(struct chunk* c);

void fineANN_similariting_init();
void fineANN_similariting(struct chunk* c);

void statis_similariting_init();
void statis_similariting(struct chunk* c);

void weightchunk_similariting_init();
void weightchunk_similariting(struct chunk* c);

void common_similariting_init();

void common_similariting_init_MT(int feaNum);
void common_similariting_stop_MT();
struct chunk* topK_match_similariting_MT(struct chunk* c, int suFeaNum);

extern GHashTable* cand_tab;
extern GHashTable* existing_fea_tab;
extern GHashTable* commonSimiSufeatureTab;
extern GAsyncQueue* candQueue;

#ifdef __cplusplus
extern "C"
{
#endif

void deepsketch_ANN_init();
void deepsketch_ANN_similariting(struct chunk* c);

#ifdef __cplusplus
}
#endif

#endif
