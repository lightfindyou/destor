#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"
#include "../featuring/featuring.h"
#include "../jcr.h"

volatile feature* simiFeature;
struct chunk* simiSearchRet_firstfitfeature = NULL;

void firstfitfeature_similariting(struct chunk* c){
	if(c){
		simiFeature = c->fea;
	}else{
		simiFeature = NULL;
		return;
	}
	simiSearchRet_firstfitfeature = NULL;

	//cal hit time
	for(int i = 0; i<c->feaNum; i++){
		chunkList *cl = g_hash_table_lookup(commonSimiSufeatureTab, &(simiFeature[i]));
		if(cl){
			int listLen = cl->length;
			struct chunk** candList = cl->list;
			jcr.candNum += listLen;		//Number of chunks that share same feature with existing chunk

			if(listLen>0) {
				simiSearchRet_firstfitfeature = candList[0];
				break;
			}
		}
	}

	//printf("check similar result\n");
	if(simiSearchRet_firstfitfeature) {
		//printf("insert into base chunk\n");
		g_queue_push_tail(c->basechunk, simiSearchRet_firstfitfeature);
	}

	TIMER_DECLARE(3);
	TIMER_BEGIN(3);
	insert_sufeatureHashList(c, c->feaNum, commonSimiSufeatureTab);
	TIMER_END(3, jcr.insertFea_time);

	/*Only if the chunk is unique, add the chunk into sufeature table*/
	/*UNNECESSARY, for high dedup ratio, always add*/
	return NULL;
}

void firstfitfeature_similariting_init(){
	commonSimiSufeatureTab = g_hash_table_new(g_int64_hash, g_int64_equal);
}

void firstfitfeature_similariting_stop(){
}
