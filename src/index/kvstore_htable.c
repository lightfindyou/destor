/*
 * kvstore_htable.c
 *
 *  Created on: Mar 23, 2014
 *      Author: fumin
 */

#include "../destor.h"
#include "index.h"
#include <errno.h>
#include <sys/stat.h>

typedef char* kvpair;

#define get_key(kv) (kv)
#define get_value(kv) ((int64_t*)(kv+destor.index_key_size))

static GHashTable *htable;

static int32_t kvpair_size;

/* mkdir -p style helper for local index paths. */
static int ensure_dir_exists(const char *path) {
	char buf[4096];
	char *p;
	size_t len;

	if (!path || !path[0])
		return -1;

	len = strlen(path);
	if (len >= sizeof(buf))
		return -1;

	strcpy(buf, path);
	if (len > 1 && buf[len - 1] == '/')
		buf[len - 1] = '\0';

	for (p = buf + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(buf, S_IRWXU | S_IRWXG | S_IRWXO) != 0
					&& errno != EEXIST)
				return -1;
			*p = '/';
		}
	}

	if (mkdir(buf, S_IRWXU | S_IRWXG | S_IRWXO) != 0 && errno != EEXIST)
		return -1;

	return 0;
}

/*
 * Create a new kv pair.
 */
static kvpair new_kvpair_full(char* key){
    kvpair kvp = malloc(kvpair_size);
    memcpy(get_key(kvp), key, destor.index_key_size);
    int64_t* values = get_value(kvp);
    int i;
    for(i = 0; i<destor.index_value_length; i++){
    	values[i] = TEMPORARY_ID;
    }
    return kvp;
}

static kvpair new_kvpair(){
	 kvpair kvp = malloc(kvpair_size);
	 int64_t* values = get_value(kvp);
	 int i;
	 for(i = 0; i<destor.index_value_length; i++){
		 values[i] = TEMPORARY_ID;
	 }
	 return kvp;
}

/*
 * IDs in value are in FIFO order.
 * value[0] keeps the latest ID.
 */
static void kv_update(kvpair kv, int64_t id){
    int64_t* value = get_value(kv);
	memmove(&value[1], value,
			(destor.index_value_length - 1) * sizeof(int64_t));
	value[0] = id;
}

static inline void free_kvpair(kvpair kvp){
	free(kvp);
}

//xzjin read from index/htable and insert into hash table.
void init_kvstore_htable(){
    kvpair_size = destor.index_key_size + destor.index_value_length * 8;

    if(destor.index_key_size >=4)
    	htable = g_hash_table_new_full(g_int_hash, g_feature_equal,
			free_kvpair, NULL);
    else
    	htable = g_hash_table_new_full(g_feature_hash, g_feature_equal,
			free_kvpair, NULL);

	sds indexpath = sdsdup(destor.working_directory);
	indexpath = sdscat(indexpath, "index/htable");

	/* Initialize the feature index from the dump file. */
	FILE *fp;
	if ((fp = fopen(indexpath, "r"))) {
		/* The number of features */
		int key_num;
		fread(&key_num, sizeof(int), 1, fp);
		for (; key_num > 0; key_num--) {
			/* Read a feature */
			kvpair kv = new_kvpair();
			fread(get_key(kv), destor.index_key_size, 1, fp);

			/* The number of segments/containers the feature refers to. */
			int id_num, i;
			fread(&id_num, sizeof(int), 1, fp);
			assert(id_num <= destor.index_value_length);

			for (i = 0; i < id_num; i++)
				/* Read an ID */
				fread(&get_value(kv)[i], sizeof(int64_t), 1, fp);

			g_hash_table_insert(htable, get_key(kv), kv);
		}
		fclose(fp);
	}

	sdsfree(indexpath);
}

void close_kvstore_htable() {
	sds indexdir = sdsdup(destor.working_directory);
	indexdir = sdscat(indexdir, "index");

	if (ensure_dir_exists(indexdir) != 0) {
		fprintf(stderr,
				"Can not create index directory: %s\n"
				"reason: %s\n",
				indexdir,
				strerror(errno));
		exit(1);
	}

	sds indexpath = sdsdup(indexdir);
	indexpath = sdscat(indexpath, "/htable");

	FILE *fp;
	if ((fp = fopen(indexpath, "w")) == NULL) {
		fprintf(stderr,
				"Can not open index/htable for write: %s\n"
				"reason: %s\n",
				indexpath,
				strerror(errno));
		exit(1);
	}

	NOTICE("flushing hash table!");
	int key_num = g_hash_table_size(htable);
	fwrite(&key_num, sizeof(int), 1, fp);

	GHashTableIter iter;
	gpointer key, value;
	g_hash_table_iter_init(&iter, htable);
	while (g_hash_table_iter_next(&iter, &key, &value)) {

		/* Write a feature. */
		kvpair kv = value;
		if(fwrite(get_key(kv), destor.index_key_size, 1, fp) != 1){
			perror("Fail to write a key!");
			exit(1);
		}

		/* Write the number of segments/containers */
		if(fwrite(&destor.index_value_length, sizeof(int), 1, fp) != 1){
			perror("Fail to write a length!");
			exit(1);
		}
		int i;
		for (i = 0; i < destor.index_value_length; i++)
			if(fwrite(&get_value(kv)[i], sizeof(int64_t), 1, fp) != 1){
				perror("Fail to write a value!");
				exit(1);
			}

	}

	/* It is a rough estimation */
	destor.index_memory_footprint = g_hash_table_size(htable)
			* (destor.index_key_size + sizeof(int64_t) * destor.index_value_length + 4);

	fclose(fp);

	NOTICE("flushing hash table successfully!");

	sdsfree(indexpath);
	sdsfree(indexdir);

	g_hash_table_destroy(htable);
}

/*
 * For top-k selection method.
 */
int64_t* kvstore_htable_lookup(char* key) {
	kvpair kv = g_hash_table_lookup(htable, key);
	return kv ? get_value(kv) : NULL;
}

void kvstore_htable_update(char* key, int64_t id) {
	kvpair kv = g_hash_table_lookup(htable, key);
	if (!kv) {
		kv = new_kvpair_full(key);
		g_hash_table_replace(htable, get_key(kv), kv);
	}
	kv_update(kv, id);
}

/* Remove the 'id' from the kvpair identified by 'key' */
void kvstore_htable_delete(char* key, int64_t id){
	kvpair kv = g_hash_table_lookup(htable, key);
	if(!kv)
		return;

	int64_t *value = get_value(kv);
	int i;
	for(i=0; i<destor.index_value_length; i++){
		if(value[i] == id){
			value[i] = TEMPORARY_ID;
			/*
			 * If index exploits physical locality,
			 * the value length is 1. (correct)
			 * If index exploits logical locality,
			 * the deleted one should be in the end. (correct)
			 */
			/* NOTICE: If the backups are not deleted in FIFO order, this assert should be commented */
			assert((i == destor.index_value_length - 1)
					|| value[i+1] == TEMPORARY_ID);
			if(i < destor.index_value_length - 1 && value[i+1] != TEMPORARY_ID){
				/* If the next ID is not TEMPORARY_ID */
				memmove(&value[i], &value[i+1], (destor.index_value_length - i - 1) * sizeof(int64_t));
			}
			break;
		}
	}

	/*
	 * If all IDs are deleted, the kvpair is removed.
	 */
	if(value[0] == TEMPORARY_ID){
		/* This kvpair can be removed. */
		g_hash_table_remove(htable, key);
	}
}
