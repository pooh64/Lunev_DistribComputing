#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <pthread.h>

/* Turbo boost:
 * https://www.kernel.org/doc/Documentation/cpu-freq/boost.txt
 * /sys/devices/system/cpu/cpufreq/boost (0/1) */

#define CACHE_CELL_ALIGN 128

struct task_container {
	double base;
	double step;
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
}

void *task_worker(void *arg)
{
	struct task_container *pack = arg;
	size_t cur_step = pack->cur_step;
	double sum = 0;
	double base = pack->base;
	double step = pack->step;

	for (size_t i = pack->n_steps; i != 0; i--, cur_step++) {
		sum += func_to_integrate(base + cur_step * step) * step;
	}

	pack->accm = sum;
	return NULL;
}


/* Handle memleaks!!!
 * Correct n_threads == 1 behaviour!!
*/

int integrate(int n_threads, double from, double to, double step, double *result)
{
	assert(sizeof(struct task_container) <= CACHE_CELL_ALIGN);

	struct task_container_align *tasks = 
		aligned_alloc(sizeof(*tasks), sizeof(*tasks) * n_threads);
	if (!tasks)
		return -1;

	pthread_t *threads;
	if (n_threads != 1) {
		threads = malloc(sizeof(*threads) * (n_threads - 1));
		if (!threads)
			return -1;
	}

	size_t n_steps = (to - from) / step;
	size_t cur_step = 0;

	for (int i = 0; i < n_threads; i++) {
		struct task_container *ptr = (struct task_container*) (tasks + i);
		ptr->base = from;
		ptr->step = step;

		size_t this_steps = n_steps / (n_threads - i);
		ptr->cur_step = cur_step;
		ptr->n_steps = this_steps;
		cur_step += this_steps;
		n_steps -= this_steps;
	}

	double accm;

	for (int i = 0; i < n_threads; i++) {
		struct task_container *ptr = (struct task_container*) (tasks + i);
		int ret = pthread_create(threads + i, NULL, task_worker, ptr);
		if (ret)
			return -1;
		if (i == n_threads - 1) {
			task_worker(ptr);
			accm = ptr->accm;
		}
	}

	for (int i = 0; i < n_threads - 1; i++) {
		struct task_container *ptr = (struct task_container*) (tasks + i);
		int ret = pthread_join(*(threads + i), NULL);
		if (ret)
			return -1;
		accm += ptr->accm;
	}

	*result = accm;

	free(tasks);
	free(threads);
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc != 2)
		return EXIT_FAILURE;

	int n_threads = atoi(argv[1]);

	double from = 0;
	double to = 10000;
	double step = 0.0001;
	double result;
	integrate(n_threads, from, to, step, &result);
	printf("result: %lg\n", result);
	return 0;
}
