#include "destor.h"
#include "jcr.h"
#include "backup.h"

static pthread_t hash_t;
static int64_t chunk_num;
SyncQueue* hash_queue;

static void* sha1_thread(void* arg) {
	printf("hash thread         tid: %d\n", gettid());
	char code[41];
	while (1) {
		if((destor.curStatus & STATUS_HASH) == 0){
//			printf("hashing: %d\n", destor.curStatus & STATUS_HASH);
			continue;
		}
		struct chunk* c = sync_queue_pop(chunk_queue);

		if (c == NULL) {
			sync_queue_term(hash_queue);
			printf("terminal hash phase");
			break;
		}

		if (CHECK_CHUNK(c, CHUNK_FILE_START) || CHECK_CHUNK(c, CHUNK_FILE_END)) {
			sync_queue_push(hash_queue, c);
			continue;
		}

		TIMER_DECLARE(1);
		TIMER_BEGIN(1);
		SHA_CTX ctx;
		SHA1_Init(&ctx);
		SHA1_Update(&ctx, c->data, c->size);
		SHA1_Final(c->fp, &ctx);
		TIMER_END(1, jcr.hash_time);

		hash2code(c->fp, code);
		code[40] = 0;
		VERBOSE("Hash phase: %ldth chunk identified by %s", chunk_num++, code);

		sync_queue_push(hash_queue, c);
	}

//	printf("hash over!\n");
	return NULL;
}

//xzjin calculate SHA1 and put into hash_queue
void start_hash_phase() {
	hash_queue = sync_queue_new(HASHQUESIZE);
#ifndef NODEDUP
	pthread_create(&hash_t, NULL, sha1_thread, NULL);
#endif	//NODEDUP
}

void stop_hash_phase() {
    SETSTATUS(STATUS_HASH);
	pthread_join(hash_t, NULL);
	NOTICE("hash phase stops successfully!");
}
