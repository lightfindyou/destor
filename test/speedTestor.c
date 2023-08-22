#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include "chunking.h"
#include "speedTestor.h"

extern char *optarg;
extern int optind, opterr, optopt;

#define countChunkDis 0

pid_t chunkingTid;
int chunkSize = 4096;
int chunkAlg;
char* dedupDir = "/home/xzjin/gcc_part1/";
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
void* duplicateData;
int sizeCate;
int* sizeDist;
int (*chunking)(unsigned char *p, int n);

enum chunkMethod { 	gear,
					rabin,
					rabin_simple,
					rabinJump,
					nrRabin,
					TTTD,
					AE,
					fastCDC,
					leap,
					JC,
					normalizedgearjump,
					algNum
				};

char* chunkString[] = {
	"gear",
	"rabin",
	"rabin_simple",
	"rabinJump",
	"nrRabin",
	"TTTD",
	"AE",
	"fastCDC",
	"leap",
	"JC",
	"normalized-gearjump",
	"algNum"
};

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

void chunkData(void* data, unsigned long dataSize, unsigned long* chunksNum,
							 enum chunkMethod cM, int leapParIdx){
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

	case normalizedgearjump:
		if(!inited[normalizedgearjump]){
			normalized_gearjump_init(chunkSize);
			inited[normalizedgearjump] = 1;
		}
		chunking = normalized_gearjump_chunk_data;
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
		int sizeIdx = (len)/1024;
		if(sizeIdx<(sizeCate-1)){
			sizeDist[sizeIdx]++;
		}else{
			sizeDist[sizeCate-1]++;
		}
#endif //countChunkDis 
	}
	end = time_nsec();
	chunkTime[cM] += ((double)end-start)/1000000;
//	printf("chunkTime:%f\n", chunkTime[cM]);
	return;
}

void help(){
	printf("Usage: \n \
		speedTestor -d Dir(ends with \"/\") \n \
			-p parameter to use for leapCDC \n \
			-c chunk size\n \
			-a chunking algorithm\n \
		\rChunking alg include:\n");
	for(int i=0; i<algNum; i++){
		printf("\t%s\n", chunkString[i]);
	}
}

int main(int argc, char **argv){
	unsigned long procStart, procEnd;
	double procTime;
	double processedLen_MB = 0;
	unsigned long processedLen_B = 0;
	chunkAlg = algNum;

	if(parsePar(argc, argv)) {return 0;}
	printf("Chunking alg:%s\n \
			\rDeduplication dir:%s\n \
			\rChunk size: %d\n",
			chunkString[chunkAlg], dedupDir, chunkSize);

	sizeCate = (chunkSize*2.5)/1024;
	sizeDist = calloc(sizeCate, sizeof(int));

	duplicateData = getAddress();
	unsigned long chunksNum[algNum] = {0};
	procStart = time_nsec();
    pthread_mutex_lock(&lock);
	start_read_phase();
//	start_cpu_phase();
//	chunkingTid = syscall(SYS_gettid);
//	printf("chunking thread syscall tid: %d\n", chunkingTid);
	pthread_cond_wait(&cond, &lock);
	while(1){
		int dupDataSize = curReadDataLen;
		processedLen_MB += ((double)dupDataSize/1024/1024);
		processedLen_B += dupDataSize;
		if(readOver){ dupDataSize = curReadDataLen;}
		chunkData(duplicateData, dupDataSize, &chunksNum[chunkAlg], chunkAlg, 0);

		if(readOver){
			stop_read_phase();
			break;
		}
		pthread_cond_signal(&cond);
		pthread_cond_wait(&cond, &lock);
	}
	procEnd = time_nsec();
	procTime = ((double)procEnd-procStart)/1000000;

	for(int i=0; i< algNum; i++){
		if(!chunkTime[i]) continue;

		printChunkName(i);
		printf("\rChunking time: %.2f ms, \x1B[32mcpu utilization: %.2f\x1B[37m\n \
				\rProcrss time: %.2f ms \n \
				\rProcrss length: %.2f MB \n \
				\r\x1B[32mChunking throughput %.2f MB/s\x1B[37m\n \
				\rSystem throughput %.2f MB/s\n \
				\rAverage chunk size:%7ld bytes\n",
			  chunkTime[i], chunkTime[i]/procTime, procTime,
			  processedLen_MB,
			  processedLen_MB*1000/chunkTime[i],
			  processedLen_MB*1000/procTime,
			  processedLen_B/chunksNum[i]);
	}
	printSizeDist();
	
	printf("Over.\n");

	return 0;
}

void printChunkName(int chunkIdx){
		switch (chunkIdx) {
		case JC:
			printf("          JC ");
			break;

		case rabin:
			printf("       rabin ");
			break;

		case rabin_simple:
			printf("rabin_simple ");
			break;

		case rabinJump:
			printf("   rabinJump ");
			break;

		case gear:
			printf("        gear ");
			break;

		case leap:
			printf("        leap ");
			break;

		case nrRabin:
			printf("     nrRabin ");
			break;

		case TTTD:
			printf("        TTTD ");
			break;

		case AE:
			printf("          AE ");
			break;

		case fastCDC:
			printf("     fastCDC");
			break;

		default:
			printf("Unknown number:%d.\n",  chunkIdx);
			break;
		}
}

void printSizeDist(){
#if countChunkDis 
	printf("Size distribution:\n");
	for(int i=0; i< sizeCate-1; i++){
		printf("%2d~%2d K: %6d chunks\n", i, i+1, sizeDist[i]);
	}
	printf("%2d~ inf: %6d chunks\n", sizeCate-1, sizeDist[sizeCate-1]);

#endif	//countChunkDis 
}

int parsePar(int argc, char **argv){
	int opt, parIdx=1;
	opt = getopt(argc, argv, "d:c:p:a:");
	do{
		switch (opt) {
		case 'd':
			dedupDir = optarg;
			if(dedupDir[strlen(dedupDir)-1] != '/'){
				goto printHelp;
			}
			break;

		case 'a':
			for(int i=0; i< algNum; i++){
				if(!strcmp(optarg, chunkString[i])){
					chunkAlg = i;
					break;
				}
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
			return -1;
		}
	}while((opt = getopt(argc, argv, "d:c:p:a:"))>0);

	if(chunkAlg == algNum){
		printf("get chunk algorithm ERROR!\n");
		help();
		return -1;
	}

	return 0;
}
