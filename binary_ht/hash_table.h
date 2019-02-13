#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_

#include <stdint.h>
#include <stddef.h>

/* Main container */
struct hash_table;
typedef struct hash_table hash_table_t;

/* Hash function prototype, has default value */
typedef uint32_t (*hash_func_t)(void *key, size_t key_s);

/* Free function prototype, NULL by default */
typedef void     (*hash_free_t)(void *data);


/* Allocate new hash_table
 * Width must fit in uint32_t
 */
hash_table_t *hash_table_new(size_t width);

/* Delete all entries from hash_table */
void hash_table_clean(hash_table_t *ht);

/* Clean and delete hash_table
 * Uses free_fn 
 */
void hash_table_delete(hash_table_t *ht);

/* Set hash_fn, use NULL to set to default value */
int hash_table_set_hash_fn(hash_table_t *ht, hash_func_t hash_fn);

/* Set free_fn, use NULL to disable */
int hash_table_set_free_fn(hash_table_t *ht, hash_free_t free_fn);

/* Try to find data in hash_table */
int hash_exists_data(hash_table_t *ht, void *key, size_t key_s);

/* Search for key in hash_table
 * Places pointers to .data and .data_s entry fields in corresponding pointers
 *	if data != NULL (data_s != NULL)
 */
int hash_search_data(hash_table_t *ht, void *key, size_t key_s,
		     void ***data, size_t **data_s);

/* Insert new key in hash_table
 * Place old_data if such key exist and old data hasn't been free'd
 * Return values:
 * 	0 - key was not used in ht, new entry successfully added
 * 	1 - key was used in ht, old data freed or returned by pointer
 *     -1 - insert failed
 */
int hash_insert_data(hash_table_t *ht, void *key, size_t key_s,
		     void *data, size_t data_s,
		     void **old_data, size_t *old_data_s);

/* Delete key from hash_table
 * Uses free_fn()
 */
int hash_delete_data(hash_table_t *ht, void *key, size_t key_s);

/* Todo: hash_fn setter, hash_copy, hash_rehash */


#endif /* HASH_TABLE_H_ */
