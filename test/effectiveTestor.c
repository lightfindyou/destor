#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "chunking.h"

#define SIZE (16*1024*1024)
#define SIZEOFRAND (4)
#define CHUNKSIZE (4096)
//#define CHUNKSIZE (8192)

int (*chunking)(unsigned char *p, int n);

enum chunkMethod { JC, gear, rabin, nrRabin, TTTD, AE };

void* getAddress(){
	void* p = malloc(SIZE);
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

void chunkData(void* data, int* chunksNum, void** edge, enum chunkMethod cM){
	void *head = data;
	void *tail = data + SIZE;
	*chunksNum = 0;
	switch (cM) {
	case JC:
		printf("JC:\n");
		gearjump_init(CHUNKSIZE);
		chunking =  gearjump_chunk_data;
		break;
	
	case rabin:
		printf("Rabin:\n");
		chunkAlg_init(CHUNKSIZE);
		chunking =  rabin_chunk_data;
		break;

	case gear:
		printf("Gear:\n");
		gear_init(CHUNKSIZE);
		chunking =  gear_chunk_data;
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

	for(; (unsigned long)head < (unsigned long)tail;){
		int len = chunking(head, (int)((unsigned long)tail - (unsigned long)head ));
		edge[*chunksNum] = head + len;
		head = edge[*chunksNum];
		(*chunksNum)++;
	}
	printf("Total chunks num:%d\n", *chunksNum);
	printf("Average chunks size:%d\n", SIZE/(*chunksNum));
	printf("chunk start:%p, chunk end:%p\n\n", edge[0], edge[(*chunksNum) -1 ]);
	return;
}

void testData(void* data, void ** edge, int chunksNum,
		 int* unchanged, int* change1, int* change2, int* change3, int* change4){
	*unchanged = 0;
	*change1 = 0;
	*change2 = 0;
	*change3 = 0; 
	*change4 = 0; 
	void *tail = data + SIZE;
	for(int i = 0, j = chunksNum -5; i<j; i++){
		int jumpLen = random()%1024;
		void* start = edge[i] + jumpLen;
		int len = gearjump_chunk_data(start, (int)((unsigned long)tail - (unsigned long)start));
		void* tail = start + len;

		int k = i+1;
		for(; k < chunksNum && (k - i) <5; k++){
			if((unsigned long)tail <= (unsigned long)edge[k]) break;
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
			case 5:
 				(*change4)++; 
				break;
			default:
 				(*change4)++; 
				printf("jumped %d chunks.\n", k-i);
				break;
		}
	}
}

int main(){
	void *p = getAddress();
	int chunksNum;
	void* edge[2*SIZE/CHUNKSIZE];
	chunkData(p, &chunksNum, edge, rabin);
	chunkData(p, &chunksNum, edge, nrRabin);
	chunkData(p, &chunksNum, edge, TTTD);
	chunkData(p, &chunksNum, edge, AE);
	chunkData(p, &chunksNum, edge, gear);
	chunkData(p, &chunksNum, edge, JC);
	int unchanged = 0, change1 = 0, change2 = 0, change3 = 0, change4 = 0;
	testData(p, edge, chunksNum,
		 &unchanged, &change1, &change2, &change3, &change4);

	int total = unchanged + change1 + change2 + change3 + change4;
	printf("total:%d\n", total);
	printf("unchanged:%d, percentage:%.2f%%\n", unchanged, (float)unchanged*100/total);
	printf("change1:%d, percentage:%.2f%%\n", change1, (float)change1*100/total);
	printf("change2:%d, percentage:%.2f%%\n", change2, (float)change2*100/total);
	printf("change3:%d, percentage:%.2f%%\n", change3, (float)change3*100/total);
	printf("change4:%d, percentage:%.2f%%\n", change4, (float)change4*100/total);

	return 0;
}