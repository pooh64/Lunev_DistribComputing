#include "hash_table.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#define TRACE_LINE fprintf(stderr, "trace_line: %d\n", __LINE__);

struct hash_entry {
	char  *key;
	size_t key_s;
	size_t data;
	struct hash_entry *next;
};

struct hash_table {
	struct hash_entry **arr;
	size_t arr_s;
	size_t n_entries;
};

struct hash_iter {
	struct hash_table *ht;
	struct hash_entry *entry;
	size_t index;
};

static struct hash_entry *hash_entry_new(char *key, size_t key_s, size_t data)
{
	struct hash_entry *entry = malloc(sizeof(*entry));
	if (!entry)
		return NULL;

	entry->key_s = key_s;
	entry->key = malloc(key_s);

	if (!entry->key) {
		free(entry);
		return NULL;
	}
	memcpy(entry->key, key, key_s);
	entry->data = data;
	entry->next = NULL;
	return entry;
}

static inline void hash_entry_delete(struct hash_entry *entry)
{
	free(entry->key);
	free(entry);
}

static inline uint32_t hash_hashfunc(char *key, size_t key_s)
{
	uint32_t hash = 5381;
	for (; key_s != 0; ++key, --key_s)
		hash = hash * 33 + *key;
	return hash;
}

static inline size_t 
hash_get_index(struct hash_table *ht, char *key, size_t key_s)
{
	return hash_hashfunc(key, key_s) % ht->arr_s;
}

static inline int hash_cmp_keys(char *key_1, size_t key_1_s,
				char *key_2, size_t key_2_s)
{
	if (key_1_s != key_2_s)
		return 1;
	return memcmp(key_1, key_2, key_1_s);
}



hash_table_t *hash_table_new(size_t n_buckets)
{
	if (!n_buckets)
		return NULL;

	struct hash_table *ht = malloc(sizeof(*ht));
	if (!ht)
		return NULL;

	ht->arr_s = n_buckets;
	ht->arr = calloc(ht->arr_s, sizeof(*ht->arr));
	if (!ht->arr) {
		free(ht);
		return NULL;
	}

	ht->n_entries = 0;
	return ht;
}

void hash_table_clean(hash_table_t *ht)
{
	for (size_t i = 0; i < ht->arr_s; i++) {
		struct hash_entry *ptr = ht->arr[i];
		while (ptr) {
			struct hash_entry *tmp = ptr;
			ptr = ptr->next;
			hash_entry_delete(tmp);
			ht->n_entries--;
		}
	}
}

void hash_table_delete(hash_table_t *ht)
{
	hash_table_clean(ht);
	free(ht->arr);
	free(ht);
}


int hash_insert_data(hash_table_t *ht, char *key, size_t key_s, size_t **data_r)
{
	size_t index = hash_get_index(ht, key, key_s);

	struct hash_entry *ptr = ht->arr[index];
	if (!ptr) {
		ptr = hash_entry_new(key, key_s, 0);
		if (!ptr)
			return -1;
		if (data_r)
			*data_r = &ptr->data;
		ht->arr[index] = ptr;
		ht->n_entries++;

		return 0;
	}

	while (1) {
		if (!hash_cmp_keys(ptr->key, ptr->key_s, key, key_s)) {
			if (data_r)
				*data_r = &ptr->data;
			return 1;
		}
		if (!ptr->next)
			break;
		ptr = ptr->next;
	}

	ptr->next = hash_entry_new(key, key_s, 0);
	if (!ptr->next)
		return -1;

	ht->n_entries++;
	if (data_r)
		*data_r = &ptr->next->data;
	return 0;
}

int hash_delete_data(hash_table_t *ht, char *key, size_t key_s)
{
	size_t index = hash_get_index(ht, key, key_s);

	struct hash_entry *ptr = ht->arr[index];
	if (!ptr)
		return 0;

	struct hash_entry **prev_next = &ht->arr[index];

	while (1) {
		if (!hash_cmp_keys(ptr->key, ptr->key_s, key, key_s)) {
			*prev_next = ptr->next;
			hash_entry_delete(ptr);
			ht->n_entries--;
			return 1;
		}
		if (!ptr->next)
			break;
		prev_next = &ptr->next;
		ptr = ptr->next;
	}

	return 0;
}

int hash_search_data(hash_table_t *ht, char *key, size_t key_s, size_t **data_r)
{
	size_t index = hash_get_index(ht, key, key_s);

	struct hash_entry *ptr = ht->arr[index];
	if (!ptr)
		return 0;

	while (1) {
		if (!hash_cmp_keys(ptr->key, ptr->key_s, key, key_s)) {
			if (data_r)
				*data_r = &ptr->data;
			return 1;
		}
		if (!ptr->next)
			break;
		ptr = ptr->next;
	}

	return 0;
}


struct hash_iter_t *hash_iter_new(hash_table_t *ht)
{
	struct hash_iter *iter = malloc(*iter);
	if (!iter)
		return NULL;
	iter->ht = ht;
	iter->entry = NULL;
	iter->index = 0;
	return iter;
}

void hash_iter_delete(hash_iter_t *iter)
{
	free(iter);
}

int hash_iter_begin(hash_iter_t *iter)
{
	for (size_t i = 0; i < iter->ht->arr_s; i++) {
		if (iter->ht->arr[i]) {
			iter->entry = iter->ht->arr[i];
			iter->index = i;
			return 1;
		}
	}

	return 0;
}

int hash_iter_next(hash_iter_t *iter)
{
	if (!ht->entry)
		return -1;

	if (ht->entry->next) {
		ht->entry = ht->entry->next;
		return 1;
	}

	for (size_t i = iter->index + 1; i < iter->ht->arr_s; i++) {
		if (iter->ht->arr[i]) {
			iter->entry = iter->ht->arr[i];
			iter->index = i;
			return 1;
		}
	}

	return 0;
}

int hash_iter_data(hash_iter_t *iter, const char **key, size_t *data);
