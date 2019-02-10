#ifndef HASH_TABLE_H_
#define HASH_TABLE_H_

struct hash_entry {
	void  *key;
	size_t key_s;
	void  *data;
	size_t data_s;
	struct hash_entry_t *next;
};

typedef size_t (*hash_func_t)(void *key, size_t key_s);
typedef void   (*hash_free_t)(void *data);

struct hash_table {
	struct hash_entry **buckets;
	size_t arr_s;
	size_t n_entries;
	hash_func_t hash_fn;
	hash_free_t free_fn;
};

struct hash_table *hash_table_new(size_t width, hash_func_t hash_func);
void		   hash_table_clean (struct hash_table *ht);
void		   hash_table_delete(struct hash_table *ht);
int		   hash_exists_data (struct hash_table *ht, void *key, size_t key_s);
int		   hash_search_data (struct hash_table *ht, void *key, size_t key_s, void **data, size_t *data_s);
/* free old data */
int		   hash_insert_data (struct hash_table *ht, void *key, size_t key_s, void *data, size_t data_s);
int		   hash_delete_data (struct hash_table *ht, void *key, size_t key_s);

#endif /* HASH_TABLE_H_
