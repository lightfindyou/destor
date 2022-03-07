#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "JC.h"

#define SIZE (8*1024*1024)
#define SIZEOFRAND (8)

void* getAddress(){
	void* p = malloc(SIZE);
	time_t t = time(0);
	srandom((unsigned)t);
	int loop = SIZE/SIZEOFRAND;
	int *curp = (int *)p;
	printf("address start : %p\n", curp);
	for(int i=0; i< loop; i++){
		*curp = random();
		curp++;
	}
	printf("address end   : %p\n", curp);

	return p;
}

void chunkData(void* data, int* chunksNum, void** edge){
	void *head = data;
	void *tail = data + SIZE;
	*chunksNum = 0;

	for(; (unsigned long)head < (unsigned long)tail;){
		int len = gearjump_chunk_data(head, (int)((unsigned long)tail - (unsigned long)head ));
		edge[*chunksNum] = head + len;
		head = edge[*chunksNum];
		(*chunksNum)++;
	}
	printf("Total chunks num:%d\n", *chunksNum);
	printf("Average chunks size:%d\n", SIZE/(*chunksNum));
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
	gearjump_init();
	int chunksNum = 0;
	void* edge[SIZE/4096];
	chunkData(p, &chunksNum, edge);
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