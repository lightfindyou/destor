#include <xxhash.h>
#include "../destor.h"
#include "similariting.h"

void deepsketch_similariting_init(){
	deepsketch_ANN_init();
}

struct chunk* deepsketch_similariting(struct chunk* c){
	return deepsketch_ANN_similariting(c);
}