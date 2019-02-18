#include "hash_table.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

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

	hash_table_dump_distrib(ht);
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
		assert(ret == 1);
		sum += *data;
	} while (hash_iter_next(iter));
		
	assert(sum == checksum);
	
	hash_iter_delete(iter);
	hash_table_delete(ht);

	return 0;
}


int main()
{
	hash_simple_test();
	hash_iter_test();
	return 0;
}
