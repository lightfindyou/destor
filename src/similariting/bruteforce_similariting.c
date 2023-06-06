#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

#define BRUTE_SIMI_THREAD_NUM 50
pthread_t brute_simi_thread_tid[BRUTE_SIMI_THREAD_NUM];
GList* bruteforce_list = NULL;
int processingChunk = 0;
pthread_mutex_t mutex;
int curDeltaSize = INT_MAX;
GList* glIter;
struct chunk* basec;
struct chunk *ret;

void bruteforce_similariting_init(){
	bruteforce_list = NULL;
	pthread_mutex_init(&mutex, 0);
	curDeltaSize = INT_MAX;
}

/*Insert chunks into the list*/
void bruteforce_insert_chunk(struct chunk* c){
	bruteforce_list = g_list_append(bruteforce_list, c);
}

static void similariting_thread(void* arg) {
	char deltaOut[4*destor.chunk_max_size];

	while(1){
		//get candidate chunk
		pthread_mutex_lock(&mutex);
		if(!glIter){
			pthread_mutex_unlock(&mutex);
			return;
		}
		struct chunk* candc = glIter->data;
		glIter = g_list_next(glIter);
		pthread_mutex_unlock(&mutex);

		int deltaSize = xdelta3_compress(basec->data, basec->size,
					 candc->data, candc->size, deltaOut, 1);

		//update candidate
		pthread_mutex_lock(&mutex);
		if(deltaSize < curDeltaSize){
			curDeltaSize = deltaSize;
			ret = candc;
		}
		pthread_mutex_unlock(&mutex);
	}
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
void bruteforce_similariting(struct chunk* c){
	processingChunk++;
	printf("processing chunk: %d\r", processingChunk);

	curDeltaSize = INT_MAX;
	ret = NULL;
	basec = c;
	glIter = g_list_first(bruteforce_list);

	for (int i = 0; i < BRUTE_SIMI_THREAD_NUM ; i++){
		pthread_create(&brute_simi_thread_tid[i], NULL, similariting_thread, NULL);
	}
	for (int i = 0; i < BRUTE_SIMI_THREAD_NUM ; i++){
		pthread_join(brute_simi_thread_tid[i], NULL);
	}
	/*only if the chunk is unique, add the chunk into sufeature table*/
	bruteforce_insert_chunk(c);
	g_queue_push_tail(c->basechunk, ret);

	return;
}