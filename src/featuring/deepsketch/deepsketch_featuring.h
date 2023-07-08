#ifndef __DEEPSKETCH_FEATURING_
#define __DEEPSKETCH_FEATURING_

#define HASH_SIZE 128
#define BLOCK_SIZE 4096

#ifdef __cplusplus
extern "C"
#endif
void deepsketch_featuring(unsigned char* buf, int size, struct chunk* c);

#ifdef __cplusplus
extern "C"
#endif
void deepsketch_featuring_init(char* modelPath);




#endif  //__DEEPSKETCH_FEATURING_