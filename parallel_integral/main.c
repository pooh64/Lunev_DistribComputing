#include "cpu_topology.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <float.h>
#include <pthread.h>
#include <dirent.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <limits.h>

/* #define DUMP_LOG_ENABLED */
#ifdef DUMP_LOG_ENABLED
#define DUMP_LOG(arg) arg
#else
#define DUMP_LOG(arg)
#endif

#define TRACE_LINE fprintf(stderr, "line: %d\n", __LINE__)

/* Todo:
 *
 * Turbo boost:
 * https://www.kernel.org/doc/Documentation/cpu-freq/boost.txt
 * /sys/devices/system/cpu/cpufreq/boost */

struct task_container {
	double base;
	double step_wdth;
	double accum;

	size_t start_step;
	size_t n_steps;
	int cpu;
};

#define CACHE_CELL_ALIGN 128
struct task_container_align {
	struct task_container task;
	uint8_t padding[CACHE_CELL_ALIGN -
		sizeof(struct task_container)];
};

static inline double func_to_integrate(double x)
{
	return 2 / (1 + x * x);
	/* return 1; */
}

void *task_worker(void *arg)
{
	struct task_container *pack = arg;
	size_t cur_step = pack->start_step;
	double sum = 0;
	double base = pack->base;
	double step_wdth = pack->step_wdth;

	DUMP_LOG(double dump_from = base + cur_step * step_wdth);
	DUMP_LOG(double dump_to   = base + (cur_step + pack->n_steps) * 
		 		    step_wdth);

	for (size_t i = pack->n_steps; i != 0; i--, cur_step++) {
		sum += func_to_integrate(base + cur_step * step_wdth) * 
		       step_wdth;
	}

	pack->accum = sum;

	DUMP_LOG(fprintf(stderr, "worker: from: %9.9lg "
				 "to: %9.9lg sum: %9.9lg\n",
			 dump_from, dump_to, sum));

	return NULL;
}

void integrate_split_task(struct task_container_align *tasks, int n_threads,
	cpu_set_t *cpuset, size_t n_steps, double base, double step)
{
	int n_cpus = CPU_COUNT(cpuset);
	if (n_threads < n_cpus)
		n_cpus = n_threads;

	size_t cur_step = 0;
	int    cur_thread  = 0;

	int cpu = cpu_set_search_next(-1, cpuset);
	for (; n_cpus != 0; n_cpus--, cpu = cpu_set_search_next(cpu, cpuset)) {

		/* Take ~1/n steps and threads per one cpu */
		size_t cpu_steps   = n_steps   / n_cpus;
		int    cpu_threads = n_threads / n_cpus;
		       n_steps    -= cpu_steps;
		       n_threads  -= cpu_threads;

		for (; cpu_threads != 0; cpu_threads--, cur_thread++) {
			struct task_container *ptr =
				(struct task_container*) (tasks + cur_thread);
			ptr->base = base;
			ptr->step_wdth = step;
			ptr->cpu = cpu;

			size_t thr_steps = cpu_steps / cpu_threads;

			ptr->start_step = cur_step;
			ptr->n_steps    = thr_steps;

			cur_step  += thr_steps;
			cpu_steps -= thr_steps;
		}
	}
}

int integrate(int n_threads, cpu_set_t *cpuset, size_t n_steps,
	      double base, double step, double *result)
{
	/* Allocate cache-aligned task containers */
	struct task_container_align *tasks = 
		aligned_alloc(sizeof(*tasks), sizeof(*tasks) * n_threads);
	if (!tasks) {
		perror("Error: aligned_alloc");
		return -1;
	}

	pthread_t *threads = NULL;
	if (n_threads != 1) {
		threads = malloc(sizeof(*threads) * (n_threads - 1));
		if (!threads) {
			perror("Error: malloc");
			free(tasks);
			return -1;
		}
	}

	/* Split task btw cpus and threads */
	integrate_split_task(tasks, n_threads, cpuset, n_steps, base, step);

	/* Move main thread to other cpu before load start */
	struct task_container *main_task = 
		(struct task_container*) (tasks + n_threads - 1);
	cpu_set_t cpuset_tmp;
	CPU_ZERO(&cpuset_tmp);
	CPU_SET(main_task->cpu, &cpuset_tmp);
	DUMP_LOG(fprintf(stderr, "setting worker to cpu = %d\n",
			 main_task->cpu));
	if (sched_setaffinity(getpid(), 
	    sizeof(cpuset_tmp), &cpuset_tmp) == -1) {
		perror("Error: sched_setaffinity");
		goto handle_err;
	}

	/* Load n - 1 threads */
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	for (int i = 0; i < n_threads - 1; i++) {
		struct task_container *ptr = 
			(struct task_container*) (tasks + i);
		CPU_ZERO(&cpuset_tmp);
		CPU_SET(ptr->cpu, &cpuset_tmp);
		DUMP_LOG(fprintf(stderr, "setting worker to cpu = %d\n",
				 ptr->cpu));
		pthread_attr_setaffinity_np(&attr,
					    sizeof(cpuset_tmp), &cpuset_tmp);
		int ret = pthread_create(threads + i, &attr, task_worker, ptr);
		if (ret) {
			perror("Error: pthread_create");
			goto handle_err;
		}
	}

	/* Load main thread */
	task_worker(main_task);
	double accum = main_task->accum;

	/* Accumulate result */
	for (int i = 0; i < n_threads - 1; i++) {
		struct task_container *ptr =
			(struct task_container*) (tasks + i);
		int ret = pthread_join(*(threads + i), NULL);
		if (ret) {
			perror("Error: pthread_join");
			goto handle_err;
		}
		accum += ptr->accum;
	}

	*result = accum;

	if (threads)
		free(threads);
	free(tasks);
	return 0;

handle_err:
	if (threads)
		free(threads);
	free(tasks);
	return -1;
}

int process_args(int argc, char *argv[], int *n_threads)
{
	if (argc != 2) {
		fprintf(stderr, "Error: only 1 arg required\n");
		return -1;
	}
	
	char *endptr;
	errno = 0;
	long tmp = strtol(argv[1], &endptr, 10);
	if (errno || *endptr != '\0' || tmp < 1 || tmp > INT_MAX) {
		fprintf(stderr, "Error: wrong number of threads\n");
		return -1;
	}
	*n_threads = tmp;
	
	return 0;
}

int main(int argc, char *argv[])
{
	int n_threads;
	if (process_args(argc, argv, &n_threads)) {
		fprintf(stderr, "Error: wrong argv\n");
		exit(EXIT_FAILURE);
	}

	struct cpu_topology topo;
	cpu_set_t cpuset;
	if (get_cpu_topology(&topo)) {
		fprintf(stderr, "Error: get_cpu_topology\n");
		exit(EXIT_FAILURE);
	}
	DUMP_LOG(dump_cpu_topology(stderr, &topo));
	if (one_cpu_per_core_cpu_topology(&topo, &cpuset)) {
		fprintf(stderr, "Error: one_cpu_per_core_cpu_topology\n");
		exit(EXIT_FAILURE);
	}
	DUMP_LOG(dump_cpu_set(stderr, &cpuset));

	double from = 0;
	double to = 500000;
	double step = 0.00005;
	double result;
	size_t n_steps = (to - from) / step;
	if (integrate(n_threads, &cpuset, n_steps, from, step, &result) == -1) {
		perror("Error: integrate");
		exit(EXIT_FAILURE);
	}

	printf("result: %.*lg\n", DBL_DIG, result);

	return 0;
}
