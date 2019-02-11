#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_

/* Main container */
struct hash_table;
typedef struct hash_table hash_table_t;

/* Hashfunc, set by default */
typedef size_t (*hash_func_t)(void *key, size_t key_s);

/* Free function for data, NULL by default */
typedef void   (*hash_free_t)(void *data);

/* Allocate new hash_table */
hash_table_t *hash_table_new(size_t width, hash_func_t hash_fn, hash_free_t free_fn);

/* Delete all entries from hash_table */
void hash_table_clean(hash_table_t *ht);

/* Clean and delete hash_table, uses free_fn */ /* FLAG */
void hash_table_delete(hash_table_t *ht);

/* Try to find data in hash_table */
int hash_exists_data(hash_table_t *ht, void *key, size_t key_s);

/* Search for key in hash_table, return values by pointers data, data_s */
int hash_search_data(hash_table_t *ht,
		     void *key, size_t key_s,
		     void **data, size_t *data_s);

/* Insert new key in hash_table, 
 * return old_data if such key exist and old data hasn't been free_fn()'d
 */
int hash_insert_data(hash_table_t *ht,
		     void *key, size_t key_s,
		     void *data, size_t data_s,
		     void **old_data, size_t *old_data_s);

/* Delete key from hash_table */ 
int hash_delete_data(hash_table_t *ht, void *key, size_t key_s);

/* Todo: hash_fn setter, hash_copy, hash_rehash */





/* Move to *.c file */

typedef struct hash_entry {
	void  *key;
	size_t key_s;
	void  *data;
	size_t data_s;
	struct hash_entry_t *next;
} hash_entry;

typedef struct hash_table {
	struct hash_entry **arr;
	size_t arr_s;
	size_t n_entries;
	hash_func_t hash_fn;
	hash_free_t free_fn;
} hash_table;

hash_table_t *hash_table_new(size_t width, hash_func_t hash_fn, hash_free_t free_fn)
{
	struct hash_table *ht = malloc(sizeof(*ht));
	if (!ht)
		return NULL;

	ht->arr_s = width;
	ht->arr = calloc(ht->arr_s, sizeof(*ht->arr));
	if (!ht->arr) {
		free(ht)
		return NULL;
	}

	ht->n_entries = 0;
	ht->hash_fn = hash_fn;
	ht->hash_free = free_fn;
	return ht;
}

void hash_table_clean(hash_table_t *ht)
{
	for (size_t i = 0; i < ht->arr_s; i++) {
		struct hash_entry *ptr, *tmp;
		ptr = ht->arr[i];
		ht->arr[i] = NULL;
		while (ptr) {
			tmp = ptr;
			ptr = ptr->next;
			free(tmp->key);	/* Add flag, copy key by default */
			if (free_fn)
				free_fn(tmp->data);
			free(tmp);
		}
	}
	ht->n_entries = 0;
}

void hash_table_delete(hash_table_t *ht)
{
	hash_table_clean(ht);
	free(ht->arr);
	free(ht);
}

#endif /* HASH_TABLE_H_ */
