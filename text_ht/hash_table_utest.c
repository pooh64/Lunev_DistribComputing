#include "hash_table.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>


int hash_ctor_test()
{
	hash_table_t *ht = hash_table_new(0);
	assert(ht == NULL);

	return 0;
}

int hash_simple_test()
{
	assert(!"Rewrite this");
	hash_table_t *ht = hash_table_new(64);

	char word[] = "word";
	char phrase[] = "int main";
	size_t *data = NULL;
	int ret;

	ret = hash_insert_data(ht, word, sizeof(word), &data);
	assert(ret == 0);
	ret = hash_search_data(ht, word, sizeof(word), &data);
	assert(ret == 1);
	ret = hash_insert_data(ht, phrase, sizeof(phrase), &data);
	assert(ret == 0);
	ret = hash_search_data(ht, phrase, sizeof(phrase), &data);
	assert(ret == 1);
	ret = hash_delete_data(ht, word, sizeof(word));
	assert(ret == 1);
	ret = hash_search_data(ht, word, sizeof(word), &data);
	assert(ret == 0);
	ret = hash_delete_data(ht, word, sizeof(word));
	assert(ret == 0);

	hash_table_delete(ht);

	return 0;
}


#define STRESS_WORD_S 8
#define STRESS_WORD_N 64
#define STRESS_CYCLES 1024
#define STRESS_HASH_S 1024

struct stress_word {
	size_t cur_s;
	size_t *data;
	size_t  data_old;
	char buf[STRESS_WORD_S];
};

size_t rand_stress_word(char *buf, size_t buf_s)
{
	size_t rand_s = rand() % (buf_s - 1) + 1;
	for (size_t tmp = rand_s; tmp != 0; --tmp, ++buf)
		*buf = (char) (rand() % 4) + 'o';
	*buf = '\0';
	return rand_s;
}

int hash_rand_stress_test()
{
	struct stress_word words[STRESS_WORD_N] = {};

	/* Alloc hash_table */
	hash_table_t *ht;
	size_t fail_counter = 0;
	while (1) {
		ht = hash_table_new(STRESS_HASH_S);
		if (ht)
			break;
		fprintf(stderr, "hash_table_new failed\n");
		if (fail_counter++ == 10) {
			fprintf(stderr, "Can't alloc ht\n");
			assert(0);
		}
	}
	
	for (size_t cycle = STRESS_CYCLES; cycle != 0; --cycle) {

		/* Generate new random words */
		for (size_t i = 0; i != STRESS_WORD_N; ++i)
			words[i].cur_s =
				rand_stress_word(words[i].buf, STRESS_WORD_S);

		/* Insert new words in table, remember data values */
		for (size_t i = 0; i != STRESS_WORD_N; ++i) {
			int ret = hash_insert_data(ht,
				words[i].buf, words[i].cur_s, &words[i].data);
			if (ret == -1) {
				fprintf(stderr, "hash_insert_data failed\n");
				words[i].data = NULL;
			} else if (ret == 1) {
				words[i].data = NULL;
			} else if (ret == 0) {
				words[i].data_old = rand();
				*words[i].data = words[i].data_old;
			} else
				assert(!"Impossible ret");
		}

		/* Particullary delete words from table
		 * Try to search and delete again */
		for (size_t i = 0; i != STRESS_WORD_N; ++i) {
			if (rand() % 2 || words[i].data == NULL)
				continue;
			int ret = hash_delete_data(ht,
				words[i].buf, words[i].cur_s);
			assert(ret == 1);
			words[i].data = NULL;
			ret = hash_search_data(ht,
				words[i].buf, words[i].cur_s, &words[i].data);
			assert(ret == 0);
			ret = hash_delete_data(ht,
				words[i].buf, words[i].cur_s);
			assert(ret == 0);
		}

		for (size_t i = 0; i != STRESS_WORD_N; ++i) {
			if (!words[i].data)
				continue;
			int ret = hash_search_data(ht,
				words[i].buf, words[i].cur_s, &words[i].data);
			assert(ret == 1);
			assert(*words[i].data == words[i].data_old);
		}
	}

	hash_table_delete(ht);
	return 0;
}

int main()
{
	hash_ctor_test();
	//hash_simple_test();
	hash_rand_stress_test();
	return 0;
}
