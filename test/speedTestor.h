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
void start_read_phase();
void stop_read_phase();
extern long curReadDataLen;

#endif	// __SPEEDTESTOR_H