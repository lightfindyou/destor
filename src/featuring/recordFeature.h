#ifndef __RECORD_FEATURE__
#define __RECORD_FEATURE__

#include "../destor.h"

void  recordFeatureToFile_init(int feaNum, int feaLen);
void recordFeatureToFile(struct chunk *c);
void stopRecordFeatureToFile();

#endif  //__RECORD_FEATURE__