#include "hash_table.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int hash_simple_test()
{
	hash_table_t *ht = hash_table_new(8);

	char word[] = "word";
	char phrase[] = "int main";
	size_t *data = NULL;
	int ret;

	ret = hash_insert_data(ht, word, sizeof(word), &data);
	assert(ret == 0);
	*data = 0xdead;
	ret = hash_search_data(ht, word, sizeof(word), &data);
	assert(ret == 1);
	assert(*data == 0xdead);
	ret = hash_insert_data(ht, phrase, sizeof(phrase), &data);
	assert(ret == 0);
	*data = 0xbeef;
	ret = hash_search_data(ht, phrase, sizeof(phrase), &data);
	assert(ret == 1);
	assert(*data == 0xbeef);
	ret = hash_delete_data(ht, word, sizeof(word));
	assert(ret == 1);
	ret = hash_search_data(ht, word, sizeof(word), &data);
	assert(ret == 0);
	ret = hash_delete_data(ht, word, sizeof(word));
	assert(ret == 0);

	hash_table_delete(ht);

	return 0;
}

size_t rand_stress_word(char *buf, size_t buf_s)
{
	size_t rand_s = rand() % (buf_s - 1) + 1;
	for (size_t tmp = rand_s; tmp != 0; tmp--, buf++)
		*buf = (char) rand();
	*buf = '\0';
	return rand_s;
}

int foreach_sum(const char *key, size_t key_s, size_t *data, void *arg)
{
	*(size_t*) arg += *data;
	return 0;
}

struct hash_item {
	const char *key;
	size_t key_s;
	size_t *data;
};

int foreach_search(const char *key, size_t key_s, size_t *data, void *arg)
{
	struct hash_item *item = arg;
	if (item->key_s == key_s && !memcmp(item->key, key, key_s)) {
		item->data = data;
		return 1;
	}
	return 0;
}

int hash_iter_test()
{
	hash_table_t *ht = hash_table_new(1024);
	assert(ht);

	/* Testing "empty" iter */
	size_t *data, key_s;
	const char *key;
	hash_iter_t *iter = hash_iter_new(ht);
	assert(iter);
	assert(hash_iter_next(iter) == -1);
	assert(hash_iter_data(iter, &key, &key_s, &data) == -1);
	assert(hash_iter_begin(iter) == 0);

	char buf[16] = {};
	size_t checksum = 0;
	int ret;

	/* Fill table with random words */
	for (size_t i = 0; i != 4096; ++i) {
		size_t *data;
		rand_stress_word(buf, sizeof(buf));
		ret = hash_insert_data(ht, buf, sizeof(buf), &data);
		assert(ret != -1);
		if (!ret) {
			*data = (size_t) rand();
			checksum += *data;
		}
	}

	ret = hash_iter_begin(iter);
	assert(ret == 1);

	size_t sum = 0;
	do {
		ret = hash_iter_data(iter, &key, &key_s, &data);
		assert(ret == 0);
		sum += *data;
	} while (hash_iter_next(iter));
		
	assert(sum == checksum);
	
	hash_iter_delete(iter);
	hash_table_delete(ht);

	return 0;
}

int hash_foreach_test()
{
	hash_table_t *ht = hash_table_new(1024);
	assert(ht);

	char buf[16] = {};
	size_t checksum = 0;
	int ret;
	size_t *data;

	/* Fill table with random words */
	for (size_t i = 0; i != 4096; ++i) {
		rand_stress_word(buf, sizeof(buf));
		ret = hash_insert_data(ht, buf, sizeof(buf), &data);
		assert(ret != -1);
		if (!ret) {
			*data = (size_t) rand();
			checksum += *data;
		}
	}

	/* Insert one more word */
	char test_word[] = "word";
	ret = hash_insert_data(ht, test_word, sizeof(test_word), &data);
	assert(ret != -1);
	*data = rand();
	size_t test_data = *data;
	checksum += test_data;

	/* Try to find test_word */
	struct hash_item test_item = { .key = test_word, .key_s = sizeof(test_word), .data = NULL };
	ret = hash_foreach_data(ht, &foreach_search, &test_item);
	assert(ret == 1);
	assert(*test_item.data == test_data);

	/* Check sum */
	size_t sum = 0;
	ret = hash_foreach_data(ht, &foreach_sum, &sum);
	assert(ret == 0);
	assert(sum == checksum);

	hash_table_delete(ht);

	return 0;
}


int main()
{
	hash_simple_test();
	hash_iter_test();
	hash_foreach_test();
	return 0;
}
