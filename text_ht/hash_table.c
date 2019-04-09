#include "hash_table.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

struct hash_entry {
	char *key;
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
	size_t index; /* Current bucket */
};

static struct hash_entry *_hash_entry_new(char *key, size_t key_s, size_t data)
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

static inline void _hash_entry_delete(struct hash_entry *entry)
{
	free(entry->key);
	free(entry);
}

static inline uint32_t _hash_hashfunc(char *key, size_t key_s)
{
	uint32_t hash = 5381;
	for (; key_s != 0; key++, key_s--)
		hash = hash * 33 + *key;
	return hash;
}

static inline size_t _hash_get_index(struct hash_table *ht, char *key,
				     size_t key_s)
{
	return _hash_hashfunc(key, key_s) % ht->arr_s;
}

static inline int _hash_cmp_keys(char *key_1, size_t key_1_s, char *key_2,
				 size_t key_2_s)
{
	if (key_1_s != key_2_s)
		return 1;
	return memcmp(key_1, key_2, key_1_s);
}

hash_table_t *hash_table_new(size_t n_buckets)
{
	if (!n_buckets)
		n_buckets = HASH_TABLE_DEFAULT_SIZE;

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
			_hash_entry_delete(tmp);
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
	size_t index = _hash_get_index(ht, key, key_s);

	struct hash_entry *ptr = ht->arr[index];
	if (!ptr) {
		ptr = _hash_entry_new(key, key_s, 0);
		if (!ptr)
			return -1;
		if (data_r)
			*data_r = &ptr->data;
		ht->arr[index] = ptr;
		ht->n_entries++;

		return 0;
	}

	while (1) {
		if (!_hash_cmp_keys(ptr->key, ptr->key_s, key, key_s)) {
			if (data_r)
				*data_r = &ptr->data;
			return 1;
		}
		if (!ptr->next)
			break;
		ptr = ptr->next;
	}

	ptr->next = _hash_entry_new(key, key_s, 0);
	if (!ptr->next)
		return -1;

	ht->n_entries++;
	if (data_r)
		*data_r = &ptr->next->data;
	return 0;
}

int hash_delete_data(hash_table_t *ht, char *key, size_t key_s)
{
	size_t index = _hash_get_index(ht, key, key_s);

	struct hash_entry *ptr = ht->arr[index];
	if (!ptr)
		return 0;

	struct hash_entry **prev_next = &ht->arr[index];

	while (1) {
		if (!_hash_cmp_keys(ptr->key, ptr->key_s, key, key_s)) {
			*prev_next = ptr->next;
			_hash_entry_delete(ptr);
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
	size_t index = _hash_get_index(ht, key, key_s);

	struct hash_entry *ptr = ht->arr[index];
	if (!ptr)
		return 0;

	while (1) {
		if (!_hash_cmp_keys(ptr->key, ptr->key_s, key, key_s)) {
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

void hash_table_dump_distrib(hash_table_t *ht, FILE *stream)
{
	fprintf(stream, "---hash_table_dump_distrib:---\n");
	fprintf(stream, "n_buckets=%lu\n", ht->arr_s);
	fprintf(stream, "n_entries=%lu\n", ht->n_entries);

	for (size_t i = 0; i < ht->arr_s; i++) {
		size_t len = 0;
		struct hash_entry *ptr = ht->arr[i];
		while (ptr) {
			ptr = ptr->next;
			len++;
		}
		fprintf(stream, "bucket[%lu]  %lu\n", i, len);
	}

	fprintf(stream, "---hash_table_dump_distrib/---\n");
}

hash_iter_t *hash_iter_new(hash_table_t *ht)
{
	struct hash_iter *iter = malloc(sizeof(*iter));
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

static inline int _hash_search_begin(struct hash_table *ht,
				     struct hash_entry **entry, size_t *index)
{
	for (size_t i = 0; i < ht->arr_s; i++) {
		if (ht->arr[i]) {
			*entry = ht->arr[i];
			*index = i;
			return 1;
		}
	}

	return 0;
}

static inline int _hash_search_next(struct hash_table *ht,
				    struct hash_entry **entry, size_t *index)
{
	if ((*entry)->next) {
		*entry = (*entry)->next;
		return 1;
	}

	for (size_t i = *index + 1; i < ht->arr_s; i++) {
		if (ht->arr[i]) {
			*entry = ht->arr[i];
			*index = i;
			return 1;
		}
	}

	return 0;
}

int hash_iter_begin(hash_iter_t *iter)
{
	return _hash_search_begin(iter->ht, &iter->entry, &iter->index);
}

int hash_iter_next(hash_iter_t *iter)
{
	if (!iter->entry)
		return -1;

	return _hash_search_next(iter->ht, &iter->entry, &iter->index);
}

int hash_iter_data(hash_iter_t *iter, const char **key, size_t *key_s,
		   size_t **data_r)
{
	if (!iter->entry)
		return -1;

	if (key)
		*key = iter->entry->key;
	if (key_s)
		*key_s = iter->entry->key_s;
	if (data_r)
		*data_r = &iter->entry->data;
	return 0;
}

int hash_foreach_data(hash_table_t *ht, hash_foreach_func_t *func, void *arg)
{
	struct hash_entry *ptr = NULL;
	size_t index = 0;

	int ret = _hash_search_begin(ht, &ptr, &index);

	while (ret) {
		ret = func(ptr->key, ptr->key_s, &ptr->data, arg);
		if (ret)
			return ret;
		ret = _hash_search_next(ht, &ptr, &index);
	}

	return 0;
}
