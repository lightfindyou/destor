#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include "speedTestor.h"
#include "pthread.h"
#include "sds.h"


static pthread_t read_t;
long curReadDataLen;
static void* readPos;	//The position to hold data
int readOver = 0;
sds dedupRootPath;


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
	readOver = 0;
	readPos = duplicateData;
	pthread_create(&read_t, NULL, read_thread, NULL);
}

void stop_read_phase() {
	pthread_join(read_t, NULL);
	printf("read phase stops successfully!\n\n");
}

