#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_
#include <stddef.h>

/* Main container */
struct hash_table;
typedef struct hash_table hash_table_t;
#define HASH_TABLE_DEFAULT_SIZE 512

/* Iterator */
struct hash_iter;
typedef struct hash_iter hash_iter_t;

/* Allocate new hash table
 * Use n_buckets=0 to set size by default */
hash_table_t *hash_table_new(size_t n_buckets);

/* Clean and delete hash table */
void hash_table_delete(hash_table_t *ht);
void hash_table_clean(hash_table_t *ht);

void hash_table_dump_distrib(hash_table_t *ht);

/* Insert, search, delete: possible return values:
 * 0: key not exist (earlier if it's insert or delete)
 * 1: key exists    (earlier if it's insert or delete)
 *-1: failure, may only occur in insert */
int hash_insert_data(hash_table_t *ht, char *key, size_t key_s,
		     size_t **data_r);
int hash_search_data(hash_table_t *ht, char *key, size_t key_s, 
		     size_t **data_r);
int hash_delete_data(hash_table_t *ht, char *key, size_t key_s);

/* Allocate new iterator, this method doesn't initialize it */
hash_iter_t *hash_iter_new(hash_table_t *ht);
void hash_iter_delete(hash_iter_t *iter);

/* Set iter to fisrt entry, possible return values:
 * 0: hash table doesn't contain any entries
 * 1: otherwise, done */
int hash_iter_begin(hash_iter_t *iter);

/* Set iter to the next entry, possible return values:
 * 0: hash table doesn't contain any entries
 * 1: otherwise, done 
 *-1: iterator isn't initialized, error */
int hash_iter_next(hash_iter_t *iter);

/* Get key and data from iterator, possible return values:
 *-1: if iterator isn't initialized 
 * 0: otherwise */
int hash_iter_data(hash_iter_t *iter, const char **key, size_t *key_s,
		   size_t **data_r);

/* Foreach function prototype */
typedef int (hash_foreach_func_t)
	    (const char *key, size_t key_s, size_t *data, void *arg);

/* Execute func for each entry in hash table
 * If func ret != 0 return ret */
int hash_foreach_data(hash_table_t *ht, hash_foreach_func_t *func, void *arg);

#endif /* HASH_TABLE_H_ */
