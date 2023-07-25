#include "destor.h"
#include "jcr.h"
#include "index/index.h"
#include "backup.h"
#include "storage/containerstore.h"
#include "similariting/similariting.h"
#include "xdelta3/xdelta3.h"
#include "xdelta3/xdelta_thread.h"

#define XDELTA_THREAD_NUM 12

static pthread_t simi_t;
static int64_t chunk_num;
pthread_mutex_t xdeltaTimeMutex;
SyncQueue* xdelta_queue;

void start_xdelta_phase() {
	if (pthread_mutex_init(&xdeltaTimeMutex, 0)){
		puts("Failed to init mutex in XdeltaPhase!");
		return NULL;
	}
	xdeltaBase = malloc(destor.baseChunkNum * destor.chunk_max_size);
	init_xdelta_thread(destor.storeDelta);
	xdelta_queue = sync_queue_new(XDElTAQUESIZE);
	start_xdelta_thread();
}

void stop_xdelta_phase() {
    SETSTATUS(STATUS_XDELTA);
	for (int i = 0; i < XDELTA_THREAD_NUM; i++){
		pthread_join(xdelta_tid[i], NULL);
	}
	free(xdeltaBase);
	stop_xdelta_thread();
	pthread_mutex_destroy(&xdeltaTimeMutex);
	NOTICE("xdelta phase stops successfully: %d chunks", chunk_num);
}
