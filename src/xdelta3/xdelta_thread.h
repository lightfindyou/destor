#ifndef XDELTA_THREAD_
#define XDELTA_THREAD_

#include "../destor.h"
#include "../jcr.h"
#include "../index/index.h"
#include "../backup.h"
#include "../storage/containerstore.h"
#include "../similariting/similariting.h"
#include "xdelta3.h"

#define XDELTA_THREAD_NUM 12
extern pthread_t xdelta_tid[XDELTA_THREAD_NUM];
void init_xdelta_thread(int recDeltaInfo);
void start_xdelta_thread();
extern char* xdeltaBase;
extern pthread_mutex_t xdeltaTimeMutex;

#endif //XDELTA_THREAD_