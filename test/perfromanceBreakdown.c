#include <assert.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include "chunking.h"
#include "speedTestor.h"
#include "xxhash.h"


extern char *optarg;
extern int optind, opterr, optopt;

#define countChunkDis 0

pid_t chunkingTid;
int chunkSize = 4096;
int chunkAlg;
int mto = 1;
char* dedupDir = "/home/xzjin/gcc_part1/";
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
void* duplicateData;
int sizeCate;
int* sizeDist;

unsigned long startTime, endTime;

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
					JCTTTD,
					normalizedgearjump,
					TTTDGear,
					algNum
				};

int (*chunking)(unsigned char *p, int n);
void fingerprinting(enum chunkMethod cM, int chunkNum);
void indexing(enum chunkMethod cM, int chunkNum);
void writing(enum chunkMethod cM, int chunkNum);

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
	"JCTTTD",
	"normalized-gearjump",
	"TTTDGear",
	"algNum"
};

double chunkTime[algNum] = {0};
double hashingTime[algNum] = {0};
double indexingTime[algNum] = {0};
double readingTime[algNum] = {0};
double writingTime[algNum] = {0};
unsigned long dupSize[algNum] = {0};
int inited[algNum] = {0};
GHashTable* tableList[algNum] = {0};
unsigned long chunkDis[algNum][65] = {0};

gboolean g_feature_equal(char* a, char* b){
	return !memcmp(a, b, sizeof(unsigned long long));
}

int fd;

static inline unsigned long time_nsec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * (unsigned long)(1000000000) + ts.tv_nsec;
}

struct chunk{
	unsigned char* c;
	unsigned long size;
	unsigned long long fp;
	unsigned char unique;
};

#define CHUNKNUM 101
struct chunk chunkList[CHUNKNUM];

void* getChunkData(){
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
	void *head = data;
	void *tail = data + dataSize;
	memset(chunkList, 0, sizeof(struct chunk)*CHUNKNUM);

	switch (cM) {
	case JC:
		if(!inited[JC]){
			tableList[JC] = g_hash_table_new(g_int64_hash, g_int64_equal);
			gearjump_init(chunkSize, mto);
			inited[JC] = 1;
		}
		chunking = gearjump_chunk_data;
		break;

	case JCTTTD:
		if(!inited[JC]){
			gearjump_init(chunkSize, mto);
			inited[JC] = 1;
		}
		chunking = gearjumpTTTD_chunk_data;
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
			tableList[rabin] = g_hash_table_new(g_int64_hash, g_int64_equal);
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
			tableList[gear] = g_hash_table_new(g_int64_hash, g_int64_equal);
			gear_init(chunkSize);
			inited[gear] = 1;
		}
		chunking = gear_chunk_data;
		break;

	case TTTDGear:
		if(!inited[gear]){
			gear_init(chunkSize);
			inited[gear] = 1;
		}
		chunking = TTTD_gear_chunk_data;
		break;

	case leap:
		if(!inited[leap]){
			tableList[leap] = g_hash_table_new(g_int64_hash, g_int64_equal);
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
			tableList[TTTD] = g_hash_table_new(g_int64_hash, g_int64_equal);
			chunkAlg_init(chunkSize);
			inited[TTTD] = 1;
		}
		chunking = tttd_chunk_data;
		break;

	case AE:
		if(!inited[AE]){
			tableList[AE] = g_hash_table_new(g_int64_hash, g_int64_equal);
			ae_init(chunkSize);
			inited[AE] = 1;
		}
		chunking = ae_chunk_data;
		break;

	case fastCDC:
		if(!inited[fastCDC]){
			tableList[fastCDC] = g_hash_table_new(g_int64_hash, g_int64_equal);
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

chunking:
	startTime = time_nsec();
	int curChunkNum = 0;
	for(; (unsigned long)head < (unsigned long)tail && curChunkNum < 100; curChunkNum++){
		int len = chunking(head, (int)((unsigned long)tail - (unsigned long)head ));
//		printf("chunk size:%d\n", len);
//		edge[*chunksNum] = head + len;
//		head = edge[*chunksNum];
		chunkList[curChunkNum].c = head;
		chunkList[curChunkNum].size = len;
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
	endTime = time_nsec();
	chunkTime[cM] += ((double)endTime-startTime)/1000000;
	fingerprinting(cM, curChunkNum);
	indexing(cM, curChunkNum);
	writing(cM, curChunkNum);
	if((unsigned long)head < (unsigned long)tail){
		goto chunking;
	}
//	printf("chunkTime:%f\n", chunkTime[cM]);
	return;
}

void fingerprinting(enum chunkMethod cM, int chunkNum){

	startTime = time_nsec();
	for (int i = 0; i < chunkNum; i++){
		chunkList[i].fp = XXH64(chunkList[i].c, chunkList[i].size, 0);
	}

	endTime = time_nsec();
	hashingTime[cM] += ((double)endTime-startTime)/1000000;
}

void indexing(enum chunkMethod cM, int chunkNum){

	GHashTable* table = tableList[cM];
	startTime = time_nsec();
	unsigned long * reduandancySize = &dupSize[cM];
	for (int i = 0; i < chunkNum; i++){
		if(g_hash_table_contains(table, &(chunkList[i].fp))){
			*reduandancySize += chunkList[i].size;
		}else{
			unsigned long long * fingerprint = malloc(sizeof(unsigned long long));
			assert(fingerprint);
			*fingerprint = chunkList[i].fp;
			g_hash_table_insert(table, fingerprint, NULL);
		}
	}
	endTime = time_nsec();
	indexingTime[cM] += ((double)endTime-startTime)/1000000;
}

void writing(enum chunkMethod cM, int chunkNum){

	startTime = time_nsec();
	unsigned long * reduandancySize = &dupSize[cM];
	for (int i = 0; i < chunkNum; i++){
		write(fd, chunkList[i].c, chunkList[i].size);
	}
	endTime = time_nsec();
	writingTime[cM] += ((double)endTime-startTime)/1000000;
}

void help(){
	printf("Usage: \n \
		speedTestor -d Dir(ends with \"/\") \n \
			-p parameter to use for leapCDC \n \
			-c chunk size\n \
			-m the ones jMask less than cMask, default 1\n \
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

	remove("dedup.output");
	fd = open("dedup.output", O_WRONLY | O_APPEND | O_CREAT, "00666");
	assert(fd);

	sizeCate = (chunkSize*2.5)/1024;
	sizeDist = calloc(sizeCate, sizeof(int));

	duplicateData = getChunkData();
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

	printChunkName(chunkAlg);
	printf("\rChunking time: %.2f ms, \x1B[32mcpu utilization: %.2f\x1B[37m\n \
			\rProcrss time: %.2f ms \n \
			\rProcrss length: %.2f MB \n \
			\r\x1B[32mChunking throughput %.2f MB/s\x1B[37m\n \
			\rSystem throughput %.2f MB/s\n \
			\rAverage chunk size:%7ld bytes\n",
		  chunkTime[chunkAlg], chunkTime[chunkAlg]/procTime, procTime,
		  processedLen_MB,
		  processedLen_MB*1000/chunkTime[chunkAlg],
		  processedLen_MB*1000/procTime,
		  processedLen_B/chunksNum[chunkAlg]);
	printf("\r chunkTime %.2f ms\n \
			\r hashTime %.2f ms\n \
			\r indexTime %.2f ms\n \
			\r writeTime %.2f ms\n",
			chunkTime[chunkAlg],
			hashingTime[chunkAlg],
			indexingTime[chunkAlg],
			writingTime[chunkAlg]);

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

		case TTTDGear:
			printf("    TTTDGear ");
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


	opt = getopt(argc, argv, "d:c:p:a:m:");
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

		case 'm':
			mto = atoi(optarg);
			printf("mto: %d\n", mto);
			break;
		
printHelp:
		default:
			help();
			return -1;
		}
	}while((opt = getopt(argc, argv, "d:c:p:a:m:"))>0);

	if(chunkAlg == algNum){
		printf("get chunk algorithm ERROR!\n");
		help();
		return -1;
	}

	return 0;
}
