/* chunking.h
 the main fuction is to chunking the file!
 */

#ifndef SIMILARITY_H_
#define SIMILARITY_H_

#include "../destor.h"
#include "../featuring/featuring.h"

GHashTable* fineness_sufeature_tab;
void fineness_similariting_init();
fpp fineness_similariting(struct chunk* c);

GHashTable* ntransform_sufeature_tab;
void ntransform_similariting_init();
fpp ntransform_similariting(struct chunk* c);

chunkid deepsketch_similariting(feature fea);

GHashTable* highdedup_sufeature_tab;
void highdedup_similariting_init();
fpp highdedup_similariting(struct chunk* c);

#endif
