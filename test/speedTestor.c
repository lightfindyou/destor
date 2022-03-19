#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "chunking.h"
#include <pthread.h>
#include "speedTestor.h"

char* dedupDir = "/home/xzjin/gcc_part1/";
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
void* duplicateData;
int (*chunking)(unsigned char *p, int n);

enum chunkMethod { JC, gear, rabin, rabinJump, nrRabin, TTTD, AE, leap };

static inline unsigned long time_nsec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * (unsigned long)(1000000000) + ts.tv_nsec;
}

void* getAddress(){
	void* p = malloc(SIZE);
	assert(p);
	void* tail = p + SIZE;
	assert(sizeof(int) == 4);

	printf("address start : %p\n", p);
	printf("address end   : %p\n\n", tail);

	return p;
}

void chunkData(void* data, int* chunksNum, void** edge, enum chunkMethod cM){
	unsigned long start, end;
	void *head = data;
	void *tail = data + SIZE;
	*chunksNum = 0;
	switch (cM) {
	case JC:
		printf("JC:\n");
		gearjump_init(CHUNKSIZE);
		chunking = gearjump_chunk_data;
		break;
	
	case rabin:
		printf("Rabin:\n");
		chunkAlg_init(CHUNKSIZE);
		chunking = rabin_chunk_data;
		break;

	case rabinJump:
		printf("RabinJump:\n");
		rabinJump_init(CHUNKSIZE);
		chunking = rabinjump_chunk_data;
		break;

	case gear:
		printf("Gear:\n");
		gear_init(CHUNKSIZE);
		chunking = gear_chunk_data;
		break;

	case leap:
		printf("leap:\n");
		leap_init();
		chunking = leap_chunk_data;
		break;

	case nrRabin:
		printf("normalized Rabin:\n");
		chunkAlg_init(CHUNKSIZE);
		chunking = normalized_rabin_chunk_data;
		break;

	case TTTD:
		printf("TTTD:\n");
		chunkAlg_init(CHUNKSIZE);
		chunking = tttd_chunk_data;
		break;

	case AE:
		printf("AE:\n");
		ae_init(CHUNKSIZE);
		chunking = ae_chunk_data;
		break;

	default:
		break;
	}

	start = time_nsec();
	for(; (unsigned long)head < (unsigned long)tail;){
		int len = chunking(head, (int)((unsigned long)tail - (unsigned long)head ));
		edge[*chunksNum] = head + len;
		head = edge[*chunksNum];
		(*chunksNum)++;
	}
	end = time_nsec();
	printf("Total chunks num:%d\n", *chunksNum);
	printf("Average chunks size:%d\n", SIZE/(*chunksNum));
	printf("chunk start:%p, chunk end:%p\n", edge[0], edge[(*chunksNum) -1 ]);
	printf("Chunks time: %ld (us)\n\n", (end-start)/1000);
	return;
}

int main(){

	duplicateData = getAddress();
	int chunksNum;
	void* edge[2*SIZE/CHUNKSIZE];
    pthread_mutex_lock(&lock);
	start_read_phase();
	pthread_cond_wait(&cond, &lock);
	while(1){
    	pthread_mutex_lock(&lock);
		chunkData(duplicateData, &chunksNum, edge, rabin);
		chunkData(duplicateData, &chunksNum, edge, nrRabin);
		chunkData(duplicateData, &chunksNum, edge, TTTD);
		chunkData(duplicateData, &chunksNum, edge, AE);
		chunkData(duplicateData, &chunksNum, edge, gear);
		chunkData(duplicateData, &chunksNum, edge, rabinJump);
		chunkData(duplicateData, &chunksNum, edge, leap);
		chunkData(duplicateData, &chunksNum, edge, JC);
		pthread_cond_signal(&cond);
		pthread_cond_wait(&cond, &lock);
	}

	return 0;
}