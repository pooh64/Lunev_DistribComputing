#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_

#include <stddef.h>

#define HASH_TABLE_DEFAULT_SIZE 512

/* Main container */
struct hash_table;
typedef struct hash_table hash_table_t;

/* Iterator */
struct hash_iter;
typedef struct hash_iter hash_iter_t;

hash_table_t *hash_table_new(size_t n_buckets);
void hash_table_delete(hash_table_t *ht);
void hash_table_clean(hash_table_t *ht);
void hash_table_dump_distrib(hash_table_t *ht);

/* 0 - n/e, 1 - key exist, -1 -failure */
int hash_insert_data(hash_table_t *ht, char *key, size_t key_s,
		     size_t **data_r);
int hash_search_data(hash_table_t *ht, char *key, size_t key_s, 
		     size_t **data_r);
int hash_delete_data(hash_table_t *ht, char *key, size_t key_s);


hash_iter_t *hash_iter_new(hash_table_t *ht);
void hash_iter_delete(hash_iter_t *iter);

/* 0 - n/e, 1 otherwise */
int hash_iter_begin(hash_iter_t *iter);
int hash_iter_next(hash_iter_t *iter);
int hash_iter_data(hash_iter_t *iter, const char **key, size_t *key_s,
		   size_t **data_r);

#endif /* HASH_TABLE_H_ */
