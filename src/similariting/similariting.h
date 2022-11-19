/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef SIMILARITY_H_
#define SIMILARITY_H_

#include "destor.h"

#define FINESSE_FEATURE_NUM 12
#define FINESSE_SF_NUM 4

GHashTable* fineness_sufeature_tab;
static void fineness_similariting_init();
static unsigned char* fineness_similariting(struct chunk* c);

chunkid ntransform_similariting(feature fea);
chunkid deepsketch_similariting(feature fea);

#endif
