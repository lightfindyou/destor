#ifndef __SPEEDTESTOR_H
#define __SPEEDTESTOR_H

#include <pthread.h>

#define SIZE (128*1024*1024)
#define SIZEOFRAND (4)
#define CHUNKSIZE (4096)
extern void* duplicateData;
extern char* dedupDir;
extern pthread_cond_t cond;
extern pthread_mutex_t lock;
extern int readOver;
void start_read_phase();

#endif	// __SPEEDTESTOR_H