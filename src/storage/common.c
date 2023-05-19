#include "common.h"

int isFileExists(const char* path){
    if (access(path, F_OK) == 0) {
        return 1;
    } else {
        return 0;
    }
}

FILE* createFile(const char* path){
    assert(isFileExists == 0);
    FILE* fp = fopen(path, "a");
    assert(fp);

    return fp;
}