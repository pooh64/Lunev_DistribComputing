#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_

#include <stddef.h>

/* Main container */
struct hash_table;
typedef struct hash_table hash_table_t;

hash_table_t *hash_table_new(size_t n_buckets);
void hash_table_delete(hash_table_t *ht);

#endif /* HASH_TABLE_H_ */
