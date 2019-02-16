#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>

void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
	if (rand() % 128) {
		void *ret = __real_malloc(size);
		if (ret)
			return ret;
		fprintf(stderr, "__wrap_malloc: "
				"__real_malloc failed!!!, exit\n"); 
		exit(EXIT_FAILURE);
	}
	fprintf(stderr, "__wrap_malloc: size=%lu "
			"simulating failure\n", size);
	return NULL;
}
