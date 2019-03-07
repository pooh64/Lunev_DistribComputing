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

#define DUMP_LOG_ENABLED
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
	long double base;
	long double step_wdth;
	long double accum;

	size_t start_step;
	size_t n_steps;
	
	pthread_t thread;
	int cpu;
};

#define CACHE_CELL_ALIGN 128
struct task_container_align {
	struct task_container task;
	uint8_t padding[CACHE_CELL_ALIGN -
		sizeof(struct task_container)];
};

static inline long double func_to_integrate(long double x)
{
	return 2 / (1 + x * x);
	/* return 1; */
}

void *integrate_task_worker(void *arg)
{
	struct task_container *pack = arg;
	size_t cur_step = pack->start_step;
	long double sum = 0;
	long double base = pack->base;
	long double step_wdth = pack->step_wdth;

	DUMP_LOG(long double dump_from = base + cur_step * step_wdth);
	DUMP_LOG(long double dump_to   = base + (cur_step + pack->n_steps) * 
		 		       	 step_wdth);

	for (size_t i = pack->n_steps; i != 0; i--, cur_step++) {
		sum += func_to_integrate(base + cur_step * step_wdth) * 
		       step_wdth;
	}

	pack->accum = sum;

	DUMP_LOG(fprintf(stderr, "worker: from: %9.9Lg "
				 "to: %9.9Lg sum: %9.9Lg\n",
			 dump_from, dump_to, sum));

	return NULL;
}

void integrate_split_tasks(struct task_container_align *tasks, int n_tasks,
	cpu_set_t *cpuset, size_t n_steps, long double base, long double step)
{
	int n_cpus = CPU_COUNT(cpuset);
	if (n_tasks < n_cpus)
		n_cpus = n_tasks;

	size_t cur_step = 0;
	int    cur_task = 0;

	int cpu = cpu_set_search_next(-1, cpuset);
	for (; n_cpus != 0; n_cpus--, cpu = cpu_set_search_next(cpu, cpuset)) {

		/* Take ~1/n steps and threads per one cpu */
		size_t cpu_steps   = n_steps / n_cpus;
		int    cpu_tasks   = n_tasks / n_cpus;
		       n_steps    -= cpu_steps;
		       n_tasks    -= cpu_tasks;

		for (; cpu_tasks != 0; cpu_tasks--, cur_task++) {
			struct task_container *ptr = &tasks[cur_task].task;
			ptr->base = base;
			ptr->step_wdth = step;
			ptr->cpu = cpu;

			size_t task_steps = cpu_steps / cpu_tasks;

			ptr->start_step = cur_step;
			ptr->n_steps    = task_steps;

			cur_step  += task_steps;
			cpu_steps -= task_steps;
		}
	}
}

int set_this_thread_cpu(int cpu)
{
	cpu_set_t cpuset_tmp;
	CPU_ZERO(&cpuset_tmp);
	CPU_SET(cpu, &cpuset_tmp);
	DUMP_LOG(fprintf(stderr, "setting this thread to cpu = %d\n", cpu));
	if (sched_setaffinity(getpid(), 
	    sizeof(cpuset_tmp), &cpuset_tmp) == -1) {
		perror("Error: sched_setaffinity");
		return -1;
	}
	return 0;
}

int integrate_run_tasks(struct task_container_align *tasks, int n_tasks)
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	cpu_set_t cpuset_tmp;
	for (int i = 0; i < n_tasks; i++) {
		struct task_container *ptr = &tasks[i].task;
		CPU_ZERO(&cpuset_tmp);
		CPU_SET(ptr->cpu, &cpuset_tmp);
		DUMP_LOG(fprintf(stderr, "setting worker to cpu = %d\n",
				 ptr->cpu));
		int ret = pthread_attr_setaffinity_np(&attr,
			sizeof(cpuset_tmp), &cpuset_tmp);
		if (ret) {
			perror("Error: pthread_attr_setaffinity_np");
			return -1;
		}
		ret = pthread_create(&ptr->thread, &attr,
			integrate_task_worker, ptr);
		if (ret) {
			perror("Error: pthread_create");
			return -1;
		}
	}
	return 0;
}

int integrate_join_tasks(struct task_container_align *tasks, int n_tasks)
{
	for (int i = 0; i < n_tasks; i++) {
		struct task_container *ptr = &tasks[i].task;
		int ret = pthread_join(ptr->thread, NULL);
		if (ret) {
			perror("Error: pthread_join");
			return -1;
		}
	}
	return 0;
}

long double integrate_accumulate_result(struct task_container_align *tasks,
	int n_tasks)
{
	long double accum = 0;
	for (int i = 0; i < n_tasks; i++) {
		accum += tasks[i].task.accum;
	}
	return accum;
}

void integrate_tasks_unused_cpus(struct task_container_align *tasks,
	int n_tasks, cpu_set_t *cpuset, cpu_set_t *result)
{
	CPU_ZERO(result);
	for (int i = 0; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(i, cpuset))
			CPU_SET(i, result);
	}
	for (int i = 0; i < n_tasks; i++)
		CPU_CLR(tasks[i].task.cpu, result);
}

int integrate(int n_threads, cpu_set_t *cpuset, size_t n_steps,
	long double base, long double step, long double *result)
{
	/* Allocate cache-aligned task containers */
	struct task_container_align *tasks = 
		aligned_alloc(sizeof(*tasks), sizeof(*tasks) * n_threads);
	if (!tasks) {
		perror("Error: aligned_alloc");
		return -1;
	}

	/* Split task btw cpus and threads */
	integrate_split_tasks(tasks, n_threads, cpuset, n_steps, base, step);
	
	/* Move main thread to other cpu */
	if (set_this_thread_cpu(tasks[0].task.cpu))
		goto handle_err;
	
	/* Run non-main tasks */
	if (integrate_run_tasks(tasks + 1, n_threads - 1))
		goto handle_err;

	/* Run main task */
	integrate_task_worker(&tasks[0].task);
	
	/* Finish non-main tasks */
	if (integrate_join_tasks(tasks + 1, n_threads - 1))
		goto handle_err;
	
	/* Sumary */
	*result = integrate_accumulate_result(tasks, n_threads);

	free(tasks);
	return 0;

handle_err:
	free(tasks);
	return -1;
}


int integrate_abused(int n_threads, cpu_set_t *cpuset, size_t n_steps,
	long double base, long double step, long double *result)
{
	/* Allocate cache-aligned task containers */
	struct task_container_align *tasks = 
		aligned_alloc(sizeof(*tasks), sizeof(*tasks) * n_threads);
	if (!tasks) {
		perror("Error: aligned_alloc");
		return -1;
	}
	
	/* Prepare bad tasks */
	int n_bad_threads = 0;
	struct task_container_align *bad_tasks;
	if (CPU_COUNT(cpuset) > n_threads) {
		n_bad_threads = CPU_COUNT(cpuset) - n_threads;
		bad_tasks = aligned_alloc(sizeof(*bad_tasks),
			sizeof(*bad_tasks) * n_bad_threads);
		if (!bad_tasks) {
			perror("Error: aligned_alloc");
			free(tasks);
			return -1;
		}
	}

	/* Split task btw cpus and threads */
	integrate_split_tasks(tasks, n_threads, cpuset, n_steps, base, step);
	
	/* Split bad tasks */
	if (n_bad_threads) {
		cpu_set_t bad_cpuset;
		integrate_tasks_unused_cpus(tasks, n_threads,
			cpuset, &bad_cpuset);
		size_t n_bad_steps = (n_steps / n_threads) * n_bad_threads;
		integrate_split_tasks(bad_tasks, n_bad_threads, &bad_cpuset,
			n_bad_steps, base, step);
	}
	
	/* Move main thread to other cpu */
	if (set_this_thread_cpu(tasks[0].task.cpu))
		goto handle_err;
	
	/* Run bad tasks */
	if (n_bad_threads && integrate_run_tasks(bad_tasks, n_bad_threads))
		goto handle_err;
	
	/* Run non-main tasks */
	if (integrate_run_tasks(tasks + 1, n_threads - 1))
		goto handle_err;

	/* Run main task */
	integrate_task_worker(&tasks[0].task);
	
	/* Finish bad tasks */
	if (n_bad_threads && integrate_join_tasks(bad_tasks, n_bad_threads))
		goto handle_err;
	
	/* Finish non-main tasks */
	if (integrate_join_tasks(tasks + 1, n_threads - 1))
		goto handle_err;
	
	/* Sumary */
	*result = integrate_accumulate_result(tasks, n_threads);

	if (n_bad_threads)
		free(bad_tasks);
	free(tasks);
	return 0;

handle_err:
	if (n_bad_threads)
		free(bad_tasks);
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

	long double from = 0;
	long double to = 1000;
	long double step = 1 / to;
	long double result;
	size_t n_steps = (to - from) / step;
	if (integrate_abused(n_threads, &cpuset,
		n_steps, from, step, &result) == -1) {
		perror("Error: integrate");
		exit(EXIT_FAILURE);
	}

	printf("result : %.*Lg\n", LDBL_DIG, result);
	printf("+1/xmax: %.*Lg\n", LDBL_DIG, result + 1 / to);

	return 0;
}
