#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "speedTestor.h"
#include "pthread.h"
#include "sds.h"

#define TIMER_DECLARE(n) struct timeval b##n,e##n
#define TIMER_BEGIN(n) gettimeofday(&b##n, NULL)
#define TIMER_END(n,t) gettimeofday(&e##n, NULL); \
    (t)+=e##n.tv_usec-b##n.tv_usec+1000000*(e##n.tv_sec-b##n.tv_sec)

TIMER_DECLARE(1);
static pthread_t read_t;
long curReadDataLen;
static void* readPos;	//The position to hold data
int readOver = 0;
static unsigned long curTotalRead = 0;
sds dedupRootPath;

#define THROUGHPUT (1000)
#define TUNINGGAR (0.01)
double readConsumeTime;

static void read_file(sds path) {
	sds filename = sdsdup(path);

	if ( dedupRootPath[sdslen( dedupRootPath) - 1] == '/') {
		/* the backup path points to a direcory */
		sdsrange(filename, sdslen( dedupRootPath), -1);
	} else {
		/* the backup path points to a file */
		int cur = sdslen(filename) - 1;
		while (filename[cur] != '/')
			cur--;
		sdsrange(filename, cur, -1);
	}

	FILE *fp;
	if ((fp = fopen(path, "r")) == NULL) {
		printf("Can not open file %s\n", path);
		perror("The reason is");
		exit(1);
	}

	int size = 0;
	int planToRead = SIZE - curReadDataLen;

	while ((size = fread(readPos, 1, planToRead, fp)) != 0) {
		readConsumeTime = 0;
		curTotalRead += size;
		TIMER_END(1, readConsumeTime);
		double readTimeS = readConsumeTime/1000000;
		double curShouldRead = readTimeS*THROUGHPUT;
		double curTotalReadMB = curTotalRead/1024/1024;
//		printf("readTimeS:%.2f, curShouldRead:%.2f, curTotalReadMB:%.2f\n",
//			 readTimeS, curShouldRead, curTotalReadMB);
		if((curTotalReadMB-curShouldRead)>THROUGHPUT*0.1){
			usleep(100000);
		}

		readPos += size;
		curReadDataLen += size;

		if(planToRead >= size){
			pthread_cond_signal(&cond);
			pthread_cond_wait(&cond, &lock);

			//executed when wake up next time
			curReadDataLen = 0;
			readPos = duplicateData;
		}
		planToRead = SIZE - curReadDataLen;
	}

	fclose(fp);

	sdsfree(filename);
}

static void find_one_file(sds path) {

	if (strcmp(path + sdslen(path) - 1, "/") == 0) {

		DIR *dir = opendir(path);
		struct dirent *entry;

		while ((entry = readdir(dir)) != 0) {
			/*ignore . and ..*/
			if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;
			sds newpath = sdsdup(path);
			newpath = sdscat(newpath, entry->d_name);

			struct stat state;
			if (stat(newpath, &state) != 0) {
				printf("The file %s does not exist! ignored!", newpath);
				return;
			}

			if (S_ISDIR(state.st_mode)) {
				assert(strcmp(newpath + sdslen(newpath) - 1, "/") != 0);
				newpath = sdscat(newpath, "/");
			}

			find_one_file(newpath);

			sdsfree(newpath);
		}

		closedir(dir);
	} else {
		read_file(path);
	}
}

static void* read_thread(void *argv) {

    pthread_mutex_lock(&lock);
	/* Each file will be processed separately */
	curReadDataLen = 0;
	dedupRootPath = sdsnew(dedupDir);
	find_one_file(dedupRootPath);
	readOver = 1;
	printf("readOver!\n");
	pthread_cond_signal(&cond);
    pthread_mutex_unlock(&lock);
	return NULL;
}

//xzjin read file in new thread, add read file to read_queue
void start_read_phase() {
    /* running job */
	TIMER_BEGIN(1);
	readOver = 0;
	readPos = duplicateData;
	pthread_create(&read_t, NULL, read_thread, NULL);
	pid_t readingTid = syscall(SYS_gettid);
	printf(" reading thread syscall tid: %d\n", readingTid);
}

void stop_read_phase() {
	pthread_join(read_t, NULL);
	printf("read phase stops successfully!\n");
}

