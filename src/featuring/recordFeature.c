#include "recordFeature.h"
#include "../storage/storageCommon.h"

FILE* featureFile = NULL;
int featureNumber = 0;
int featureLength = 0;

void  recordFeatureToFile_init(int feaNum, int feaLen){
    char featurePath[256];
    sprintf(featurePath, "%s/%s.feature", destor.recordPath, featureAlgStr[destor.feature_algorithm]);
    printf("feature path: %s\n", featurePath);
    featureFile = createFile(featurePath);
    printf("feature file: %p\n", featureFile);
    featureNumber = feaNum;
    featureLength = feaLen;
}

void recordFeatureToFile(struct chunk *c){
    fwrite(c->fea, featureLength, featureNumber, featureFile);
    fprintf(featureFile, "\n");
}

void stopRecordFeatureToFile(){
    if(fclose(featureFile)){
        int err = errno;
        printf("close feature file error: %s\n", strerror(errno));
    }
}