#include "hash_table.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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
	assert(key);

	struct hash_entry *entry = malloc(sizeof(*entry));
	if (!entry)
		return NULL;
	
	entry->key = malloc(key_s);
	if (!entry->key) {
		free(entry);
		return NULL;
	}
	memcpy(entry->key, key, key_s);
	return entry;
}

static inline void hash_entry_delete(struct hash_entry *entry)
{
	free(entry->key);
	free(entry);
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
		}
	}
}

void hash_table_delete(hash_table_t *ht)
{
	hash_table_clean(ht);
	free(ht->arr);
	free(ht);
}
