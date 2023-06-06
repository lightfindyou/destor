#include "destor.h"
#include "jcr.h"
#include "index/index.h"
#include "backup.h"
#include "storage/containerstore.h"
#include "similariting/similariting.h"
#include "xdelta3/xdelta3.h"
#include "xdelta3/xdelta_thread.h"

#define XDELTA_THREAD_NUM 12
pthread_t xdelta_tid[XDELTA_THREAD_NUM];

static pthread_t simi_t;
static int64_t chunk_num;

void start_xdelta_phase() {
	xdeltaBase = malloc(destor.baseChunkNum * destor.chunk_max_size);
	init_xdelta_thread(destor.storeDelta);
	xdelta_queue = sync_queue_new(1000);
	start_xdelta_thread();
}

void stop_xdelta_phase() {
	for (int i = 0; i < XDELTA_THREAD_NUM; i++){
		pthread_join(xdelta_tid[i], NULL);
	}
	free(xdeltaBase);
	NOTICE("xdelta phase stops successfully: %d chunks", chunk_num);
}
