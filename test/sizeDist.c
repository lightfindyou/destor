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

#define K 1024

#define avgChunkSize (8*K)
#define maxChunkSize (2*avgChunkSize)
#define minChunkSize (512)
#define jumpLen (avgChunkSize/2)

double jumpPro = 0, cutPro = 0, normalPro = 0;
double *sizeDist;
int sizeNum;
int chunkAlg;

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
	"algNum"
};

void help(){
	printf("Usage: \n \
		sizeDist -a chunking algorithm\n \
		\rChunking alg include:\n");
	for(int i=0; i<algNum; i++){
		printf("\t%s\n", chunkString[i]);
	}
}

int parsePar(int argc, char **argv){
	int opt;
	opt = getopt(argc, argv, "a:");
	chunkAlg = algNum;
	do{
		switch (opt) {

		case 'a':
			for(int i=0; i< algNum; i++){
				if(!strcmp(optarg, chunkString[i])){
					chunkAlg = i;
					break;
				}
			}
			break;
		
		default:
			break;
		}
	}while((opt = getopt(argc, argv, "d:c:p:a:"))>0);

	if(chunkAlg == algNum){
		printf("get chunk algorithm ERROR!\n");
		help();
		exit(-1);
	}

	return 0;
}

void init(){
	printf("maxChunkSize:%8d\n", maxChunkSize);
	printf("avgChunkSize:%8d\n", avgChunkSize);
	printf("minChunkSize:%8d\n", minChunkSize);
	switch (chunkAlg) {
		case rabin:
			cutPro = 1.0/(avgChunkSize);
			normalPro = 1 - cutPro;
			break;

		case JC:
			cutPro = 2.0/(avgChunkSize);
			jumpPro = cutPro;
			normalPro = 1 - cutPro -jumpPro;
			printf("     jumpLen:%8d\n", jumpLen);
			printf("    jump pro:%.8f\n", jumpPro);
			break;

		default:
			printf("Chunk alg: %s not supported!\n", chunkString[chunkAlg]);
			exit(-1);

			break;
	}
	sizeNum = maxChunkSize/K + 1;
	sizeDist = calloc(sizeNum, sizeof(double));
	printf("     cut pro:%.8f\n", cutPro);
	printf("     sizeNum:%8d\n", sizeNum);
}

// Calculate C_{n}^{m}
//	=(n!)/(m!*(n-m)!)
unsigned long C(int m, int n){
	unsigned long ret = 1, tmp = n;

	for(int i = 0; i<m; tmp--, i++){
		ret *= tmp;
	}

	for(; m>1; m--){
		ret /= m;
	}
	return ret;
}

double getProOfSize_JC(int x){
	double j0 = 0, j1 = 0, j2 = 0, j3 =0;
	if (x <= minChunkSize) {
		return 0;
	}else if(x <= jumpLen+minChunkSize){		// 0 jump
		return cutPro*pow(normalPro, x - minChunkSize - 1);
	}else if(x <= 2*jumpLen+minChunkSize){	// 0 or 1 jump
		j0 = pow(normalPro, x - minChunkSize -1)*cutPro;
		j1 = C(1, x-minChunkSize-jumpLen-1)*jumpPro*pow(normalPro, x-minChunkSize-jumpLen-1)*cutPro;
		return j0+j1;
	}else if(x <= 3*jumpLen+minChunkSize){	// 0, 1 or 2 jump
		j0 = pow(normalPro, x - minChunkSize -1)*cutPro;
		j1 = C(1, x-minChunkSize-jumpLen-1)*jumpPro*pow(normalPro, x-minChunkSize-jumpLen-2)*cutPro;
		j2 = C(2, x-minChunkSize-2*jumpLen-1)*pow(jumpPro, 2)*pow(normalPro, x-minChunkSize-jumpLen-3)*cutPro;
		return j0+j1+j2;
	}else if(x < maxChunkSize){	// 0, 1, 2 or 3 jump
		j0 = pow(normalPro, x - minChunkSize -1)*cutPro;
		j1 = C(1, x-minChunkSize-jumpLen-1)*jumpPro*pow(normalPro, x-minChunkSize-jumpLen-2)*cutPro;
		j2 = C(2, x-minChunkSize-2*jumpLen-1)*pow(jumpPro, 2)*pow(normalPro, x-minChunkSize-jumpLen-3)*cutPro;
		j3 = C(3, x-minChunkSize-3*jumpLen-1)*pow(jumpPro, 3)*pow(normalPro, x-minChunkSize-jumpLen-4)*cutPro;
		return j0+j1+j2+j3;
	}else{
		printf("maxChunkSize:%d, chunkSize to get:%d\nERROR!\n", maxChunkSize, x);
		exit(-1);
	}
}

double getProOfSize_Rabin(int x){
	if (x <= minChunkSize) {
		return 0;
	}else if(x < maxChunkSize){
		return cutPro*pow(normalPro, x - minChunkSize - 1);
	}else{
		printf("maxChunkSize:%d, chunkSize to get:%d\nERROR!\n", maxChunkSize, x);
		exit(-1);
	}
}

void getMaxPro(){
	double maxPro = 1;
	int maxIdx = maxChunkSize/K;
	for(int i=0; i<maxIdx; i++){
		maxPro -= sizeDist[i];
	}
	sizeDist[maxIdx] = maxPro;
}

int main(int argc, char **argv){
	parsePar(argc, argv);
	init();
	switch (chunkAlg) {
		case rabin:
			for(int i = 0; i<maxChunkSize-1; i++){
				double pro = getProOfSize_Rabin(i);
				sizeDist[i/K] += pro;
				if(pro>0.05){
					printf("size: %6d, pro:%2.2f\n", i, pro*100);
				}
			}
			getMaxPro();
			break;

		case JC:
			for(int i = 0; i<maxChunkSize-1; i++){
				double pro = getProOfSize_JC(i);
				sizeDist[i/K] += pro;
				if(pro>0.05){
					printf("size: %6d, pro:%2.2f\n", i, pro*100);
				}
			}
			getMaxPro();
			break;

		default:
			break;
	}

	for(int i=0; i< sizeNum; i++){
		printf("%2d ~ %2d K: %2.2f %%\n", i, i+1, 100*sizeDist[i]);
	}

	return 0;
}