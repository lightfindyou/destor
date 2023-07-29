#include "storageCommon.h"
#include "string.h"

int isFileExists(const char* path){
    if (access(path, F_OK) == 0) {
        return 1;
    } else {
        return 0;
    }
}

FILE* createFile(const char* path){
    assert(isFileExists(path) == 0);
    FILE* fp = fopen(path, "w+");
    if(!fp){
        printf("create file %s error: %s\n", path, strerror(errno));
        exit(-1);
    }else{
        printf("create file: %p\n", fp);
    }

    return fp;
}