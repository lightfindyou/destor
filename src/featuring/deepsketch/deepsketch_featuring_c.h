#ifndef __DEEPSKETCH_FEATURING_C_
#define __DEEPSKETCH_FEATURING_C_

#include "../../utils/sync_queue.h"
#ifdef __cplusplus
extern "C"{
#endif

#define BATCH_SIZE 256
#define TEMP_QUEUE_SIZE BATCH_SIZE*3

void deepsketch_featuring_init(char* modelPath);
int deepsketch_featuring(unsigned char* buf, int size, struct chunk* c);
extern SyncQueue* feature_temp_queue;

#ifdef __cplusplus
}
#endif



#endif  //__DEEPSKETCH_FEATURING_C_