#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>

/* #define FAULT_INJECT_LOG_ON */

#ifdef FAULT_INJECT_LOG_ON
#define FAULT_LOG(arg) arg
#else
#define FAULT_LOG(arg)
#endif

void *__real_malloc(size_t size);
void *__real_calloc(size_t nmemb, size_t size);

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
	FAULT_LOG(fprintf(stderr,
			  "__wrap_malloc: size=%lu "
			  "simulating failure\n",
			  size));
	return NULL;
}

void *__wrap_calloc(size_t nmemb, size_t size)
{
	if (rand() % 128) {
		void *ret = __real_calloc(nmemb, size);
		if (ret)
			return ret;
		fprintf(stderr, "__wrap_calloc: "
				"__real_calloc failed!!!, exit\n");
		exit(EXIT_FAILURE);
	}
	FAULT_LOG(fprintf(stderr,
			  "__wrap_calloc: nmemb=%lu size=%lu "
			  "simulating failure\n",
			  nmemb, size));
	return NULL;
}
