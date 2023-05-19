#ifndef RECORD_DELTA_H_
#define RECORD_DELTA_H_

#include "../destor.h"
#include "../jcr.h"
#include "../index/index.h"
#include "../backup.h"
#include "../storage/containerstore.h"
#include "../similariting/similariting.h"
#include "xdelta3.h"

void recordNULL(struct chunk *c1, struct chunk* c2, void* delta, int deltaSize);
void recordChunkAndDelta(struct chunk *compressed, struct chunk* ref, void* delta, int deltaSize);

#endif  //RECORD_DELTA_H_