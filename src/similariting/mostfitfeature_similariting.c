#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"
#include "../featuring/featuring.h"
#include "../jcr.h"

#define LINEARSEARCH 0

#if LINEARSEARCH 
GList* mostfitfeature_list = NULL;
GList* glIter;
struct chunk* basec;
struct chunk *ret;
int mostFitNum = 0;

void mostfitfeature_similariting_init(){
	mostfitfeature_list = NULL;
}

/*Insert chunks into the list*/
void mostfitfeature_insert_chunk(struct chunk* c){
	mostfitfeature_list = g_list_append(mostfitfeature_list, c);
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
void mostfitfeature_similariting(struct chunk* c){

	ret = NULL;
	basec = c;
	glIter = g_list_first(mostfitfeature_list);
	mostFitNum = 0;

	while(glIter){
		//get candidate chunk
		struct chunk* candc = glIter->data;
		glIter = g_list_next(glIter);

		int curFitNum = 0;
		for(int i = 0; i<c->feaNum; i++){
			for(int j = 0; j<candc->feaNum; j++){
				if(c->fea[i] == candc->fea[j]){
					curFitNum++;
				}
			}
		}

		//update candidate
		if(curFitNum > mostFitNum){
			ret = candc;
		}
	}

	/*only if the chunk is unique, add the chunk into sufeature table*/
	mostfitfeature_insert_chunk(c);
	if(ret){
		g_queue_push_tail(c->basechunk, ret);
	}

	return;
}

void mostfitfeature_similariting_stop(){
}

#else	//define LINEARSEARCH

volatile feature* simiFeature;
int curMaxHitTime_mostfitfeature = 0;
struct chunk* simiSearchRet_mostfitfeature = NULL;

void mostfitfeature_similariting(struct chunk* c){
	if(c){
		simiFeature = c->fea;
	}else{
		simiFeature = NULL;
		return;
	}

	simiSearchRet_mostfitfeature = NULL;
	curMaxHitTime_mostfitfeature = 0;
	printf("most fit feature number: %d\n", c->feaNum);

	//clear hitTime
	for(int i = 0; i<c->feaNum; i++){
		chunkList *cl = g_hash_table_lookup(commonSimiSufeatureTab, &(simiFeature[i]));
		if(cl){
			int listLen = cl->length;
			struct chunk** candList = cl->list;
			jcr.candNum += listLen;		//Number of chunks that share same feature with existing chunk

			for (int i = 0; i < listLen; i++) {

				struct chunk* candChunk = candList[i];
				candChunk->simiHitTime = 0;
			}
		}
	}

	//cal hit time
	for(int i = 0; i<c->feaNum; i++){
		chunkList *cl = g_hash_table_lookup(commonSimiSufeatureTab, &(simiFeature[i]));
		if(cl){
			int listLen = cl->length;
			struct chunk** candList = cl->list;
			jcr.candNum += listLen;		//Number of chunks that share same feature with existing chunk

			for (int i = 0; i < listLen; i++) {

				struct chunk* candChunk = candList[i];
				int hitTime = ++(candChunk->simiHitTime);

				if(hitTime > curMaxHitTime_mostfitfeature){
					curMaxHitTime_mostfitfeature = hitTime;
					simiSearchRet_mostfitfeature = candChunk;
					printf("update similar result\n");
				}
			}
		}
	}

	printf("check similar result\n");
	if(simiSearchRet_mostfitfeature) {
		printf("insert into base chunk\n");
		g_queue_push_tail(c->basechunk, simiSearchRet_mostfitfeature);
	}

	TIMER_DECLARE(3);
	TIMER_BEGIN(3);
	insert_sufeatureHashList(c, c->feaNum, commonSimiSufeatureTab);
	TIMER_END(3, jcr.insertFea_time);

	/*Only if the chunk is unique, add the chunk into sufeature table*/
	/*UNNECESSARY, for high dedup ratio, always add*/
	return NULL;
}

void mostfitfeature_similariting_init(){
	commonSimiSufeatureTab = g_hash_table_new(g_int64_hash, g_int64_equal);
}

void mostfitfeature_similariting_stop(){
}

#endif	//define LINEARSEARCH