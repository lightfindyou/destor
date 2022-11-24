#include "destor.h"
#include "jcr.h"
#include "backup.h"
#include "index/index.h"

static pthread_t simi_t;
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
			/** done in index phase to enable dedup with most recent chunk*/
			g_hash_table_replace_threadsafe(fp_tab, &(c->fp), c, fp_tab_mutex);
		}

		TIMER_END(1, jcr.store_time);

		VERBOSE("store phase: %ldth chunk", chunk_num++);

	}
	return NULL;

}

void start_store_phase() {
	store_phase_init();
	pthread_create(&simi_t, NULL, store_thread, NULL);
}

void stop_store_phase() {
	pthread_join(simi_t, NULL);
	NOTICE("store phase stops successfully: %d chunks", chunk_num);
}
