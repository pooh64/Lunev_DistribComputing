#include "hash_table.h"

#include <stdlib.h>
#include <string.h>

typedef struct hash_entry {
	void  *key;
	size_t key_s;
	void  *data;
	size_t data_s;
	struct hash_entry_t *next;
} hash_entry;


typedef struct hash_table {
	struct hash_entry **arr;	/* Buckets 		   */
	size_t arr_s;			/* must fit in uint32_t    */
	size_t n_entries;		/* Total number of entries */
	hash_func_t hash_fn;
	hash_free_t free_fn;		/* NULL by default 	   */
} hash_table;


/* Width must be != 0 and must fit in uint32_t */
hash_table_t *hash_table_new(size_t width)
{
	if (!width || width > UINT32_MAX)
		return NULL;

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
	ht->hash_fn = hash_func_default;
	ht->free_fn = NULL;
	return ht;
}

void hash_table_clean(hash_table_t *ht)
{
	assert(ht);
	for (size_t i = 0; i < ht->arr_s; i++) {
		struct hash_entry *ptr, *tmp;
		ptr = ht->arr[i];
		ht->arr[i] = NULL;
		while (ptr) {
			tmp = ptr;
			ptr = ptr->next;
			free(tmp->key);
			if (free_fn)
				free_fn(tmp->data);
			free(tmp);
		}
	}
	ht->n_entries = 0;
}

void hash_table_delete(hash_table_t *ht)
{
	assert(ht);
	hash_table_clean(ht);
	free(ht->arr);
	free(ht);
}

int hash_table_set_hash_fn(hash_table_t *ht, hash_func_t *hash_fn)
{
	assert(ht);
	
	if (!ht->entries)
		return -1;

	if (hash_fn)
		ht->hash_fn = hash_fn;
	else
		ht->hash_fn = hash_func_default;
	return 0;
}

int hash_table_set_free_fn(hash_table_t *ht, hash_free_t *free_fn)
{
	assert(ht);
	ht->free_fn = free_fn;
	return 0;
}



/* Returns 0 if keys are equal */
static inline int hash_key_cmp(void *key_a, size_t key_a_s, void *key_b, size_t key_b_s)
{
	if (key_a_s != key_b_s)
		return 1;
	if (key_a == key_b)
		return 0;
	return memcmp(key_a, key_b, key_a_s);
}

static inline size_t hash_get_index(hash_table_t *ht, void *key, size_t key_s)
{
	return ht->hash_fn(key, key_s) % ht->arr_s;
}

/* Search for key in hash_table, return values by pointers data, data_s */
int hash_search_data(hash_table_t *ht,
		     void *key, size_t key_s,
		     void ***data, size_t **data_s)
{
	assert(ht);

	hash_entry_t *ptr = hash_get_index(ht, key, key_s);
	while (ptr) {
		if (hash_key_cmp(ptr->key, ptr->key_s, key, key_s))
			break;
		ptr = ptr->next;
	}
	
	if (!ptr)
		return 0;
	
	if (data) {
		*data = &ptr->data;
		if (data_s)
			*data_s = &ptr->data_s;
	}
	
	return 1;
}

