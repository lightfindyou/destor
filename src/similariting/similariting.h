/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef SIMILARITY_H_
#define SIMILARITY_H_

#include "destor.h"
#include "featuring/featuring.h"

GHashTable* fineness_sufeature_tab;
static void fineness_similariting_init();
static unsigned char* fineness_similariting(struct chunk* c);

GHashTable* ntransform_sufeature_tab;
static void ntransform_similariting_init();
chunkid ntransform_similariting(feature fea);
chunkid deepsketch_similariting(feature fea);

#endif
