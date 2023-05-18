#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

//Together used by finess superfeature and flatfeature
pthread_mutex_t fineness_sufeature_tab_mutex;
GList* bruteforce_list = NULL;

void bruteforce_similariting_init(){
	bruteforce_list = NULL;
}

/*Insert chunks into the list*/
void bruteforce_insert_chunk(struct chunk* c){
	bruteforce_list = g_list_append(bruteforce_list, c);
}

/** return base chunk fingerprint if similary chunk is found
 *  else return 0
*/
struct chunk* bruteforce_similariting(struct chunk* c){

	struct chunk *ret;
	char deltaOut[4*destor.chunk_max_size];
	int curDeltaSize = INT_MAX;

	GList* gl = g_list_first(bruteforce_list);
	while(gl){
		struct chunk* basec = gl->data;
		int deltaSize = xdelta3_compress(c->data, c->size, basec->data, basec->size, deltaOut, 1);
		if(deltaSize < curDeltaSize){
			ret = basec;
		}
	}

	/*only if the chunk is unique, add the chunk into sufeature table*/
	bruteforce_insert_chunk(c);

retPoint:
	return ret;
}