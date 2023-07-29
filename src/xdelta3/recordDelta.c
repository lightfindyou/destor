#include "recordDelta.h"
#include "../storage/storageCommon.h"

void recordNULL(struct chunk *c1, struct chunk* c2, void* delta, int deltaSize){

}

void recordChunkAndDelta(struct chunk *compressed, struct chunk* ref, void* delta, int deltaSize){
    char chunkCompressedPath[256];
    char chunkRefPath[256];
    char deltaPath[256];
    FILE* file;

    sprintf(chunkCompressedPath, "%s/%04d", destor.recordPath, compressed->chunkID);
    sprintf(chunkRefPath, "%s/%04d", destor.recordPath, ref->chunkID);
    sprintf(deltaPath, "%s/%04d-%04d.delta", destor.recordPath, compressed->chunkID, ref->chunkID);

    if(!isFileExists(chunkCompressedPath)){
        file = createFile(chunkCompressedPath);
        fwrite(compressed->data, compressed->size, 1, file);
        fclose(file);
    }

    if(!isFileExists(chunkRefPath)){
        file = createFile(chunkRefPath);
        fwrite(ref->data, ref->size, 1, file);
        fclose(file);
    }

    if(!isFileExists(deltaPath)){
        file = createFile(deltaPath);
        fwrite(delta, deltaSize, 1, file);
        fclose(file);
    }
}


FILE* featureFile;
void recordSimilatiry_init(){
    char deltaPath[256];
    sprintf(deltaPath, "%s/%s.delta", destor.recordPath, featureAlgStr[destor.feature_algorithm]);
    printf("delta path: %s\n", deltaPath);
    featureFile = createFile(deltaPath);
}

void recordSimilatiry_close(){
    fclose(featureFile);
}

void recordSimilatiry(struct chunk *compressed, struct chunk* ref, void* delta, int deltaSize){
    double similarity = ((double)deltaSize)/compressed->size;
    similarity = 100-100*similarity;
    fprintf(featureFile, "%.4f\n", similarity);
}