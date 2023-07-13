#ifndef __DEEPSKETCH_SIMILARITING_C__
#define __DEEPSKETCH_SIMILARITING_C__

#include "../../destor.h"

#ifdef __cplusplus
extern "C"{
#endif

void deepsketch_similariting_NGT_init();
void deepsketch_similariting_NGT(struct chunk* c);
void deepsketch_similariting_NGT_stop();

void deepsketch_similariting_hnsw_init();
void deepsketch_similariting_hnsw(struct chunk* c);

#ifdef __cplusplus
}
#endif

#endif  //__DEEPSKETCH_SIMILARITING_C__