#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "speedTestor.h"
#include "pthread.h"
#include "sds.h"


static pthread_t cpu_t;

static void* utilization_thread(void *argv) {
    char fname[200] ;
    snprintf(fname, sizeof(fname), "/proc/self/task/%d/stat", (int)chunkingTid) ;
	sleep(3);
	while(1){
		FILE *fp = fopen(fname, "r") ;
		if ( !fp ){
			perror("open file error");
			printf("open file:%s error\n", fname);
			exit(-1);
		}
		long clkPerSec = sysconf(_SC_CLK_TCK);
		int ucpu = 0, scpu=0, tot_cpu = 0 ;
		if ( fscanf(fp, "%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %d %d",
			&ucpu, &scpu) == 2 ){
			tot_cpu = ucpu + scpu ;
		}else{
			printf("read file ERROR\n");
		}
		fclose(fp) ;
		printf("cpu utilizaton percentage: %d, clock per second: %ld, cpu usage: %f s\n",
			 tot_cpu, clkPerSec, ((double)tot_cpu)/clkPerSec);
		sleep(1);
	}
	return NULL;
}

//xzjin read file in new thread, add read file to read_queue
void start_cpu_phase() {
    /* running job */
	pthread_create(&cpu_t, NULL,  utilization_thread, NULL);
}

void stop_cpu_phase() {
	pthread_join(cpu_t, NULL);
	printf("utilization phase stops successfully!\n");
}

