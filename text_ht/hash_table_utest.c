#include "hash_table.h"
#include <assert.h>

int hash_one_word_test()
{
	hash_table_t *ht = hash_table_new(64);

	char word[] = "exp";
	size_t *data = NULL;
	int ret;

	ret = hash_insert_data(ht, word, sizeof(word), &data);
	assert(ret == 0);
	ret = hash_search_data(ht, word, sizeof(word), &data);
	assert(ret == 1);
	ret = hash_delete_data(ht, word, sizeof(word), &data);
	assert(ret == 1);

	hash_table_delete(ht);

	return 0;
}

int main()
{
	hash_one_word_test();
	return 0;
}
