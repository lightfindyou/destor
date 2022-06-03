#ifndef __SPEEDTESTOR_H
#define __SPEEDTESTOR_H

#include <pthread.h>

#define SIZE (128*1024*1024)
#define SIZEOFRAND (4)

extern int chunkSize;
extern void* duplicateData;
extern char* dedupDir;
extern pthread_cond_t cond;
extern pthread_mutex_t lock;
extern int readOver;
extern long curReadDataLen;
extern pid_t chunkingTid;

void start_read_phase();
void stop_read_phase();
void start_cpu_phase();
void stop_cpu_phase();
void printChunkName(int chunkIdx);
int parsePar(int argc, char **argv);

#endif	// __SPEEDTESTOR_H