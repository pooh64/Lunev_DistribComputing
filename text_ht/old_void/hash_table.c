#include "hash_table.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct hash_entry {
	void  *key;
	size_t key_s;
	void  *data;
	size_t data_s;
	struct hash_entry *next;
} hash_entry;


typedef struct hash_table {
	struct hash_entry **arr;	/* Buckets 		   */
	size_t arr_s;			/* must fit in uint32_t    */
	size_t n_entries;		/* Total number of entries */
	hash_func_t hash_fn;
	hash_free_t free_fn;		/* NULL by default 	   */
} hash_table;


uint32_t hash_func_default(void *key, size_t key_s)
{
	register uint32_t hash = 0;
	register uint8_t *ptr = key;
	register uint8_t ch;
	for (ch = *ptr; key_s != 0; --key_s, ch = *++ptr) {
		hash = (hash >> 1) + ((hash & 1) << 31);
		hash += ch;
	}
	return hash;
}

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
		free(ht);
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
			if (ht->free_fn)
				ht->free_fn(tmp->data);
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


int hash_table_set_hash_fn(hash_table_t *ht, hash_func_t hash_fn)
{
	assert(ht);
	
	if (!ht->n_entries)
		return -1;

	if (hash_fn)
		ht->hash_fn = hash_fn;
	else
		ht->hash_fn = hash_func_default;
	return 0;
}


int hash_table_set_free_fn(hash_table_t *ht, hash_free_t free_fn)
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


int hash_search_data(hash_table_t *ht, void *key, size_t key_s,
		     void ***data, size_t **data_s)
{
	assert(ht);

	struct hash_entry *ptr = ht->arr[hash_get_index(ht, key, key_s)];
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


int hash_insert_data(hash_table_t *ht, void *key, size_t key_s,
		     void *data, size_t data_s, 
		     void **old_data, size_t *old_data_s)
{
	assert(ht);

	size_t index = hash_get_index(ht, key, key_s);
	struct hash_entry *ptr = ht->arr[index];
	if (!ptr) {
		ptr = hash_entry_new(key, key_s, data, data_s);
		if (!ptr)
			return -1;
		ht->arr[index] = ptr;
		return 0;
	}

	while (1) {
		if (hash_key_cmp(ptr->key, ptr->key_s, key, key_s)) {
			if (ht->free_fn) {
				ht->free_fn(ptr->data);
				ptr->data   = data;
				ptr->data_s = data_s;
				return 1;
			}
			*old_data   = ptr->data;
			*old_data_s = ptr->data_s;
			ptr->data   = data;
			ptr->data_s = data_s;
			return 1;
		}
		if (!ptr->next) {
			ptr->next = hash_entry_new(key, key_s, data, data_s);
			if (!ptr->next)
				return -1;
			return 0;
		}
		ptr = ptr->next;
	}

	assert(0);
	return -1;
}
