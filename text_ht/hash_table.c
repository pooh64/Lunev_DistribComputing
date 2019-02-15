#include "hash_table.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>

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
	printf("%s %s\n", key_1, key_2);
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
	printf("Running insert: %s\n", key);

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
	if (!ptr)
		return -1;
	ht->n_entries++;
	if (data_r)
		*data_r = &ptr->next->data;
	return 0;
}

int hash_delete_data(hash_table_t *ht, char *key, size_t key_s)
{
	printf("Running delete: %s\n", key);

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
	printf("Running search: %s\n", key);

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
