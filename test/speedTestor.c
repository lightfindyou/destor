#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include "chunking.h"
#include <pthread.h>
#include "speedTestor.h"

#define countChunkDis 0

int chunkSize = 4096;
char* dedupDir = "/home/xzjin/gcc_part1/";
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
void* duplicateData;
int (*chunking)(unsigned char *p, int n);

enum chunkMethod { gear, rabin, rabin_simple, rabinJump, nrRabin, TTTD, AE, fastCDC, leap, JC, algNum };
double chunkTime[algNum] = {0};
int inited[algNum] = {0};
unsigned long chunkDis[algNum][65] = {0};

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

void chunkData(void* data, unsigned long dataSize, unsigned long* chunksNum, enum chunkMethod cM, int leapParIdx){
	unsigned long start, end;
	void *head = data;
	void *tail = data + dataSize;
	switch (cM) {
	case JC:
		if(!inited[JC]){
			gearjump_init(chunkSize);
			inited[JC] = 1;
		}
		chunking = gearjump_chunk_data;
		break;
	
	case rabin:
		if(!inited[rabin]){
			chunkAlg_init(chunkSize);
			inited[rabin] = 1;
		}
		chunking = rabin_chunk_data;
		break;

	case rabinJump:
		if(!inited[rabinJump]){
			rabinJump_init(chunkSize);
			inited[rabinJump] = 1;
		}
		chunking = rabinjump_chunk_data;
		break;

	case gear:
		if(!inited[gear]){
			gear_init(chunkSize);
			inited[gear] = 1;
		}
		chunking = gear_chunk_data;
		break;

	case leap:
		if(!inited[leap]){
			leap_init(chunkSize, leapParIdx);
			inited[leap] = 1;
		}
		chunking = leap_chunk_data;
		break;

	case nrRabin:
		if(!inited[nrRabin]){
			chunkAlg_init(chunkSize);
			inited[nrRabin] = 1;
		}
		chunking = normalized_rabin_chunk_data;
		break;

	case TTTD:
		if(!inited[TTTD]){
			chunkAlg_init(chunkSize);
			inited[TTTD] = 1;
		}
		chunking = tttd_chunk_data;
		break;

	case AE:
		if(!inited[AE]){
			ae_init(chunkSize);
			inited[AE] = 1;
		}
		chunking = ae_chunk_data;
		break;

	case fastCDC:
		if(!inited[fastCDC]){
			fastcdc_init(chunkSize);
			inited[fastCDC] = 1;
		}
		chunking = fastcdc_chunk_data;
		break;

	case rabin_simple:
		if(!inited[rabin_simple]){
			rabin_simple_init(chunkSize);
			inited[rabin_simple] = 1;
		}
		chunking = rabin_simple_chunk_data;
		break;

	default:
		break;
	}

	start = time_nsec();
	for(; (unsigned long)head < (unsigned long)tail;){
		int len = chunking(head, (int)((unsigned long)tail - (unsigned long)head ));
//		printf("chunk size:%d\n", len);
//		edge[*chunksNum] = head + len;
//		head = edge[*chunksNum];
		head = head + len;
		(*chunksNum)++;
#if countChunkDis 
		int lenIdx = len/1024;
		lenIdx = lenIdx<64?lenIdx:64;
		chunkDis[cM][lenIdx]++;
#endif //countChunkDis 
	}
	end = time_nsec();
	chunkTime[cM] += ((double)end-start)/1000000;
	return;
}

void help(){
	printf("Usage: \n \
		speedTestor -d Dir(ends with \"/\") \n \
					-p parameter to use for leapCDC \n");
}

void printChunkName(int chunkIdx){
		switch (chunkIdx) {
		case JC:
			printf("          JC time: ");
			break;

		case rabin:
			printf("       rabin time: ");
			break;

		case rabin_simple:
			printf("rabin_simple time: ");
			break;

		case rabinJump:
			printf("   rabinJump time: ");
			break;

		case gear:
			printf("        gear time: ");
			break;

		case leap:
			printf("        leap time: ");
			break;

		case nrRabin:
			printf("     nrRabin time: ");
			break;

		case TTTD:
			printf("        TTTD time: ");
			break;

		case AE:
			printf("          AE time: ");
			break;

		case fastCDC:
			printf("     fastCDC");
			break;

		default:
			printf("Unknown number:%d.\n",  chunkIdx);
			break;
		}
}

int main(int argc, char **argv){

	int opt, parIdx=1;
	double processedLen_MB = 0;
	unsigned long processedLen_B = 0;
	opt = getopt(argc, argv, "d:c:p:");
	do{
		switch (opt) {
		case 'd':
			dedupDir = optarg;
			if(dedupDir[strlen(dedupDir)-1] != '/'){
				goto printHelp;
			}
			break;
		
		case 'c':
			chunkSize = atoi(optarg);
			printf("chunk size: %d\n", chunkSize);
			break;

		case 'p':
			parIdx = atoi(optarg);
			printf("parIdx: %d\n", parIdx);
			break;
printHelp:
		default:
			help();
			return 0;
		}
	}while((opt = getopt(argc, argv, "d:c:p:"))>0);

	printf("Deduplication dir:%s\n", dedupDir);
	printf("chunk size: %d\n", chunkSize);
	duplicateData = getAddress();
	unsigned long chunksNum[algNum] = {0};
    pthread_mutex_lock(&lock);
	start_read_phase();
	pthread_cond_wait(&cond, &lock);
	while(1){
		int dupDataSize = curReadDataLen;
		processedLen_MB += (dupDataSize/1024/1024);
		processedLen_B += dupDataSize;
		if(readOver){ dupDataSize = curReadDataLen;}
//		chunkData(duplicateData, dupDataSize, &chunksNum[rabin], rabin, 0);
//		chunkData(duplicateData, dupDataSize, &chunksNum[rabin_simple], rabin_simple, 0);
//		chunkData(duplicateData, dupDataSize, &chunksNum[nrRabin], nrRabin, 0);
//		chunkData(duplicateData, dupDataSize, &chunksNum[TTTD], TTTD, 0);
//		chunkData(duplicateData, dupDataSize, &chunksNum[AE], AE ,0);
//		chunkData(duplicateData, dupDataSize, &chunksNum[gear], gear, 0);
//		chunkData(duplicateData, dupDataSize, &chunksNum[rabinJump], rabinJump, 0);
//		chunkData(duplicateData, dupDataSize, &chunksNum[fastCDC], fastCDC, 0);
		chunkData(duplicateData, dupDataSize, &chunksNum[leap], leap, parIdx);
		chunkData(duplicateData, dupDataSize, &chunksNum[JC], JC, 0);

//		printf("chunks number:%ld\n", chunksNum[leap]);
		if(readOver){
			stop_read_phase();
			break;
		}
		pthread_cond_signal(&cond);
		pthread_cond_wait(&cond, &lock);
	}

	for(int i=0; i< algNum; i++){
		if(!chunkTime[i]) continue;

		printChunkName(i);
		printf(" time: %.2f s, throughput %.2f MB/s, average chunk size:%7ld bytes\n",
			 chunkTime[i], processedLen_MB*1000/chunkTime[i],  processedLen_B/chunksNum[i]);
	}
	
	printf("Over.\n");

	return 0;
}
