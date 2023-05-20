#ifndef STORE_COMMON_H_
#define STORE_COMMON_H_

#include "../destor.h"
#include "../jcr.h"

int isFileExists(const char* path);
FILE* createFile(const char* path);

#endif  //STORE_COMMON_H_