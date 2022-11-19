#include "destor.h"
#include "jcr.h"
#include "backup.h"

static pthread_t store_t;
static int64_t chunk_num;


void *store_thread(void *arg) {
	while (1) {
		struct chunk* c = sync_queue_pop(xdelta_queue);

		if (c == NULL) {
			//sync_queue_term(xdelta_queue);
			break;
		}

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END)) {
			//sync_queue_push(xdelta_queue, c);
			continue;
		}

		TIMER_DECLARE(1);
		TIMER_BEGIN(1);

		if(CHECK_CHUNK(c, CHUNK_UNIQUE) || CHECK_CHUNK(c, CHUNK_SIMILAR)){
			g_hash_table_replace(fp_tab, &(c->fp), c);
			//TODO add total size
		}

		TIMER_END(1, jcr.store_time);

		VERBOSE("store phase: %ldth chunk", chunk_num++);

	}
	return NULL;

}

void start_store_phase() {
	store_phase_init();
	pthread_create(&store_t, NULL, store_thread, NULL);
}

void stop_store_phase() {
	pthread_join(store_t, NULL);
	NOTICE("store phase stops successfully: %d chunks", chunk_num);
}
