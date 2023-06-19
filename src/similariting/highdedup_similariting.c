#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"
#include "../featuring/featuring.h"

//void highdedup_similariting_init(){
//	common_similariting_init();
//}
//
///** return base chunk fingerprint if similary chunk is found
// *  else return 0
//*/
//void highdedup_similariting(struct chunk* c){
//	topK_match_similariting(c, c->feaNum);
//	return;
//}

void highdedup_similariting_init(){
	common_similariting_init_MT(MAX_FEANUM);
}

void highdedup_similariting(struct chunk* c){
	topK_match_similariting_MT(c, c->feaNum);
	return;
}

void highdedup_similariting_stop(){
	common_similariting_stop_MT();
}