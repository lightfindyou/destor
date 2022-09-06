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

int main(int argc, char **argv){

	double pi[25]={0};
	double pc = 0.75;
	double k = 0;

	for(int n =1; n<25; n++ ){
		pi[n] = (1-pc)*pow(pc, n-1);
	}

	for(int n =1; n<25; n++ ){
		k += n*pi[n];
	}
	printf("calcalate %f every 24 bytes.\n", k);
	printf("calcalate %f of input on average.\n", k/(24-k));

	return 0;
}
