#ifndef DEDUP_PHASE_H_
#define DEDUP_PHASE_H_

extern struct GHashTable *fpTable;
void *dedup_thread(void *arg);
unsigned int getHashTableSize();

#endif //DEDUP_PHASE_H_
