#include "hash_table.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

int hash_ctors_test()
{
	for (int i = 2048; i != 0; i--) {
		hash_table_t *ht = hash_table_new(1024);
		hash_iter_t *iter;
		if (ht) {
			hash_table_delete(ht);
			iter = hash_iter_new(ht);
			if (iter)
				hash_iter_delete(iter);
		}
		ht = hash_table_new(0);
		if (ht)
			hash_table_delete(ht);
	}

	return 0;
}


#define STRESS_WORD_S 8
#define STRESS_WORD_N 64
#define STRESS_CYCLES 1024
#define STRESS_HASH_S 1024
#define STRESS_DISTRIB_LOG "./hash_stress_distrib.log"

struct stress_word {
	size_t cur_s;
	size_t *data;
	size_t  data_old;
	char buf[STRESS_WORD_S];
};

size_t rand_stress_word(char *buf, size_t buf_s)
{
	size_t rand_s = rand() % (buf_s - 1) + 1;
	for (size_t tmp = rand_s; tmp != 0; tmp--, buf++)
		*buf = (char) rand();
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
		for (size_t i = 0; i != STRESS_WORD_N; ++i) {
			words[i].cur_s =
				rand_stress_word(words[i].buf, STRESS_WORD_S);
		}

		/* Insert new words in table, remember data values */
		for (size_t i = 0; i != STRESS_WORD_N; ++i) {
			int ret = hash_insert_data(ht,
				words[i].buf, words[i].cur_s, &words[i].data);
			if (ret == -1) {
				/* fprintf(stderr, "hash_insert_data failed\n"); */
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

		/* Search */
		for (size_t i = 0; i != STRESS_WORD_N; ++i) {
			if (!words[i].data)
				continue;
			int ret = hash_search_data(ht,
				words[i].buf, words[i].cur_s, &words[i].data);
			assert(ret == 1);
			assert(*words[i].data == words[i].data_old);
		}
	}

	FILE *out_log = fopen(STRESS_DISTRIB_LOG, "w");
	assert(out_log);
	hash_table_dump_distrib(ht, out_log);
	fclose(out_log);

	hash_table_delete(ht);
	return 0;
}


int main()
{
	hash_ctors_test();
	hash_rand_stress_test();
	
	return 0;
}
