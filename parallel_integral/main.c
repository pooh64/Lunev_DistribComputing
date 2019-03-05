#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <float.h>
#include <pthread.h>
#include <math.h>

/* Turbo boost:
 * https://www.kernel.org/doc/Documentation/cpu-freq/boost.txt
 * /sys/devices/system/cpu/cpufreq/boost (0/1) */

#define CACHE_CELL_ALIGN 128

struct task_container {
	double base;
	double step_wdth;
	double accm;
	size_t cur_step;
	size_t n_steps;
};

struct task_container_align {
	struct task_container task;
	uint8_t padding[128 - sizeof(struct task_container)];
};

static inline double func_to_integrate(double x)
{
	return 2 / (1 + x * x);
	/* return 1; */
}

void *task_worker(void *arg)
{
	struct task_container *pack = arg;
	size_t cur_step = pack->cur_step;
	double sum = 0;
	double base = pack->base;
	double step_wdth = pack->step_wdth;

	/* size_t cur_step_beg = cur_step; */

	for (size_t i = pack->n_steps; i != 0; i--, cur_step++) {
		sum += func_to_integrate(base + cur_step * step_wdth) * step_wdth;
	}

	pack->accm = sum;

/*
	printf("task_worker: from %lg to %lg sum: %lg\n", 
			base + ((double) cur_step_beg) * step_wdth,
			base + ((double) cur_step_beg + pack->n_steps) * step_wdth,
			sum);
*/
	return NULL;
}


/* Handle memleaks!!! */

int integrate(int n_threads, double from, double to, double step, double *result)
{
	assert(sizeof(struct task_container) <= CACHE_CELL_ALIGN);

	struct task_container_align *tasks = 
		aligned_alloc(sizeof(*tasks), sizeof(*tasks) * n_threads);
	if (!tasks)
		return -1;

	pthread_t *threads = NULL;
	if (n_threads != 1) {
		threads = malloc(sizeof(*threads) * (n_threads - 1));
		if (!threads)
			return -1;
	}

	size_t n_steps = (to - from) / step;
	size_t cur_step = 0;
	struct task_container *ptr;

	/* Prepare tasks */
	for (int i = 0; i < n_threads; i++) {
		ptr = (struct task_container*) (tasks + i);
		ptr->base = from;
		ptr->step_wdth = step;

		size_t this_steps = n_steps / (n_threads - i);
		ptr->cur_step = cur_step;
		ptr->n_steps = this_steps;
		cur_step += this_steps;
		n_steps -= this_steps;
	}

	/* Load n - 1 threads */
	for (int i = 0; i < n_threads - 1; i++) {
		ptr = (struct task_container*) (tasks + i);
		int ret = pthread_create(threads + i, NULL, task_worker, ptr);
		if (ret)
			return -1;
	}

	/* Load main thread */
	ptr = (struct task_container*) (tasks + n_threads - 1);
	task_worker(ptr);
	double accm = ptr->accm;

	/* Accumulate */
	for (int i = 0; i < n_threads - 1; i++) {
		struct task_container *ptr = (struct task_container*) (tasks + i);
		int ret = pthread_join(*(threads + i), NULL);
		if (ret)
			return -1;
		accm += ptr->accm;
	}

	*result = accm;

	if (threads)
		free(threads);
	free(tasks);
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc != 2)
		return EXIT_FAILURE;

	int n_threads = atoi(argv[1]);

	double from = 0;
	double to = 100000;
	double step = 0.0001;
	double result;
	integrate(n_threads, from, to, step, &result);
	printf("result: %.*lg\n", DBL_DIG, result);
	printf("origin: %.*lg\n", DBL_DIG, M_PI);

	return 0;
}
