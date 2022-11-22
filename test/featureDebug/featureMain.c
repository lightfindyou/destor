#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include "featuring.h"

#define K 1024

#define avgChunkSize (8*K)
#define maxChunkSize (2*avgChunkSize)
#define minChunkSize (512)
#define jumpLen (avgChunkSize/2)

double jumpPro = 0, cutPro = 0, normalPro = 0;
int *dedupData;
int dedupSize;
int featureAlg;

enum featureMethod {  finesse,
					ntransform,
					algNum
				};

char* chunkString[] = {
	"finesse",
	"ntransform",
	"algNum"
};

void help(){
	printf("Usage: \n \
		fea -a featuring algorithm\n \
		\rFeaturing alg include:\n");
	for(int i=0; i<algNum; i++){
		printf("\t%s\n", chunkString[i]);
	}
}

int parsePar(int argc, char **argv){
	int opt;
	opt = getopt(argc, argv, "a:");
	featureAlg = algNum;
	do{
		switch (opt) {

		case 'a':
			for(int i=0; i< algNum; i++){
				if(!strcmp(optarg, chunkString[i])){
					featureAlg = i;
					break;
				}
			}
			break;
		
		default:
			break;
		}
	}while((opt = getopt(argc, argv, "d:c:p:a:"))>0);

	if(featureAlg == algNum){
		printf("get featuring algorithm ERROR!\n");
		help();
		exit(-1);
	}

	return 0;
}

void init(){
	switch (featureAlg) {
		case finesse:
			rabinhash_rabin_init();
			break;

		case ntransform:
			rabinhash_rabin_init();
			break;

		default:
			printf("Featuring alg: %s not supported!\n", chunkString[featureAlg]);
			exit(-1);

			break;
	}
	dedupSize = maxChunkSize/K + 1;
	dedupData = calloc(dedupSize, sizeof(int));
	srand(time(NULL));
	for(int i=0; i<dedupSize; i++){
		dedupData[i] = rand();
	}
	printf("     dedupSize:%8d\n", dedupSize);
}


#define CALM 0


int main(int argc, char **argv){
	parsePar(argc, argv);
	init();
	double realAvgChunkSize = 0;
	feature fea[12] = {0};
	memset(fea, 0, sizeof(feature)*12);
	switch (featureAlg) {
		case finesse:
			rabin_finesse(dedupData, 480, &fea[0]);
			printf("feature is: %ld\n", fea[0]);
			break;
		case ntransform:
			rabin_ntransform(dedupData, 4096, fea, 12);
			for(int i=0; i<12; i++){
				printf("feature is: %ld\n", fea[i]);
			}
			break;

		default:
			break;
	}

	return 0;
}
