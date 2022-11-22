#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include "chunking.h"

extern char *optarg;
extern int optind, opterr, optopt;
int insRangeL = 1, insRangeH = 20;

//#define SIZE (16*1024*1024)
#define SIZE (128*1024*1024)
#define SIZEOFRAND (4)
//#define CHUNKSIZE (4096)
#define CHUNKSIZE (8192)

enum featureMethod { 	gear,
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


int (*chunking)(unsigned char *p, int n);
int parsePar(int argc, char **argv);

static inline unsigned long time_nsec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec * (unsigned long)(1000000000) + ts.tv_nsec;
}

void* getChunkData(){
	void* p = malloc(SIZE+CHUNKSIZE*2);
	assert(p);
	p += CHUNKSIZE;
	void* tail = p + SIZE;
	time_t t = time(0);
	srandom((unsigned)t);
//	int loop = SIZE/SIZEOFRAND;
	int *curp = (int *)p;
	assert(sizeof(int) == 4);
	printf("address start : %p\n", curp);
	for( ; (unsigned long)curp < (unsigned long)tail; ){
		*curp = random();
//		printf("address:%p, num:%X\n", curp, *curp);
		curp++;
	}
	printf("address end   : %p\n\n", curp);

	return p;
}

void chunkData(void* data, int* chunksNum, void** edge, enum featureMethod cM, int mto){
	unsigned long start, end;
	void *head = data;
	void *tail = data + SIZE;
	*chunksNum = 0;
	switch (cM) {
	case JC:
		printf("JC:\n");
		gearjump_init(CHUNKSIZE, mto);
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
		leap_init(CHUNKSIZE, 1);
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

	case rabin_simple:
		printf("rabin_simple:\n");
		rabin_simple_init(CHUNKSIZE);
		chunking = rabin_simple_chunk_data;
		break;

	case fastCDC:
		printf("fastCDC:\n");
		fastcdc_init(CHUNKSIZE);
		chunking = fastcdc_chunk_data;
		break;

		rabin_simple_init(CHUNKSIZE);
		chunking = rabin_simple_chunk_data;
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

void testData(void* data, void ** edge, int chunksNum,
		 int* unchanged, int* change1, int* change2, int* change3, int* change4){
	*unchanged = 0;
	*change1 = 0;
	*change2 = 0;
	*change3 = 0; 
	*change4 = 0; 

	int len, k;
//	int debug = 0;
	void *tail = data + SIZE;
	void* chunkTail;
	for(int i = 0, j = chunksNum -5; i<j; i++){
		int insertPos = random()%(edge[i+1] - edge[i]);
		int insertLen = (random()%(insRangeH - insRangeL + 1)) + insRangeL;
		memmove(edge[i]-insertLen, edge[i], insertPos);
		//fill random data
		char *changep = (char *)edge[i] + insertPos;
		for(int k=0; k< insertLen; k++){
			*changep = random();
			changep++;
		}

		void* start = edge[i] - insertLen;
chunk:
		len = chunking(start, (int)((unsigned long)tail - (unsigned long)start));
		chunkTail = start + len;

		k = i+1;
		// i is the begin of the inserted chunk
		for(; k < chunksNum && (k - i) <4; k++){
//			printf("i: %d, k: %d\n", i, k);
			if((unsigned long)chunkTail <= (unsigned long)edge[k]){
				if((unsigned long)chunkTail == (unsigned long)edge[k]){
//					if(debug){
//						printf("k: %d, equal to %p\n\n", k, edge[k]);
//						debug = 0;
//					}
//					printf("break\n");
					break;
				}else{
//					printf("restart, k: %d, start: %p, tail: %p, len: %d, compared to edge :%p\n", k, start, chunkTail, len, edge[k]);
//					debug = 1;
					start = chunkTail;
					goto chunk;
				}
			}
		}

		switch (k-i){
			case 1:
				(*unchanged)++;
				break;
			case 2:
				(*change1)++;
				break;
			case 3:
				(*change2)++;
				break;
			case 4:
				(*change3)++;
				break;
			default:
 				(*change3)++; 
				printf("jumped %d chunks.\n", k-i);
				break;
		}
	}
}

void help(){
	printf("Usage: effectiveTestor -l insert length (20 by default) \n \
		\r\t\t\t-a chunking algorithm\n \
		\r\t\t\t-m the ones jMask less than cMask, default 1\n \
		\rChunking alg include:\n");
	for(int i=0; i<algNum; i++){
		printf("\t%s\n", chunkString[i]);
	}
}

/**
 * mto means "Mask Ones Less Than Chunk Ones"
*/
int featureAlg, mto = 1;
int main(int argc, char **argv){
	void *p = getChunkData();
	int chunksNum;
	float average = 0;
	void* edge[2*SIZE/CHUNKSIZE];
//	char dedupRatio[7][100];

	if(parsePar(argc, argv)){
		printf("ERROR parse para!\n");
		return -1;
	}
	chunkData(p, &chunksNum, edge, featureAlg, mto);
	int unchanged = 0, change1 = 0, change2 = 0, change3 = 0, change4 = 0;
	testData(p, edge, chunksNum,
		 &unchanged, &change1, &change2, &change3, &change4);

	int total = unchanged + change1 + change2 + change3;
	printf("total: %d\n", total);
	printf("insert length: %d\n", insRangeH);
	
	double perc1 = (float)unchanged*100/total;
	double perc2 = (float)change1*100/total;
	double perc3 = (float)change2*100/total;
	double perc4 = (float)change3*100/total;

	average += 1*perc1 + 2*perc2 + 3*perc3 + 4*perc4;
	printf("unchanged:%d, percentage:%.2f%%\n", unchanged, perc1);
	printf("change1:%d, percentage:%.2f%%\n", change1, perc2);
	printf("change2:%d, percentage:%.2f%%\n", change2, perc3);
	printf("change3:%d, percentage:%.2f%%\n", change3, perc4);

	average /=100;
	printf("\nAverage changed:%.2f\n", average);
	printf("\n& %.2f\\%% & %.2f\\%% & %.2f\\%% & %.2f\\%% & %.2f \\\\\n", perc1, perc2, perc3, perc4, average);


	return 0;
}

int parsePar(int argc, char **argv){
	int opt;
	featureAlg = algNum;

	opt = getopt(argc, argv, "a:l:m:");
	do{
		switch (opt) {
		case 'a':
			for(int i=0; i< algNum; i++){
				if(!strcasecmp(optarg, chunkString[i])){
					featureAlg = i;
					break;
				}
			}
			break;

		case 'l':
			insRangeH = atoi(optarg);
			break;

		case 'm':
			mto = atoi(optarg);
			printf("mto: %d\n", mto);
			break;
		
		default:
			help();
			return -1;
		}
	}while((opt = getopt(argc, argv, "a:l:m:"))>0);

	if(featureAlg == algNum){
		printf("get chunk algorithm ERROR!\n");
		help();
		return -1;
	}

	return 0;
}
