#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_

#include <stddef.h>

/* Main container */
struct hash_table;
typedef struct hash_table hash_table_t;

hash_table_t *hash_table_new(size_t n_buckets);
void hash_table_delete(hash_table_t *ht);
void hash_table_clean(hash_table_t *ht);

/* 0 - n/e, 1 - key exist, -1 -failure */
int hash_insert_data(hash_table_t *ht, char *key, size_t key_s, size_t **data_r);
int hash_search_data(hash_table_t *ht, char *key, size_t key_s, size_t **data_r);
int hash_delete_data(hash_table_t *ht, char *key, size_t key_s, size_t **data_r);

#endif /* HASH_TABLE_H_ */
