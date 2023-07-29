#include "recordFeature.h"

FILE* featureFile;
int featureNumber = 0;
int featureLength = 0;

void  recordFeatureToFile_init(int feaNum, int feaLen){
    char featurePath[256];
    sprintf(featurePath, "%s/%s.feature", destor.recordPath, featureAlgStr[destor.feature_algorithm]);
    printf("feature path: %s\n", featurePath);
    featureFile = createFile(featurePath);
    featureNumber = feaNum;
    featureNumber = feaLen;
}

void recordFeatureToFile(struct chunk *c){
    fwrite(c->fea, featureLength, featureNumber, featureFile);
}

void stopRecordFeatureToFile(){
    fclose(featureFile);
}