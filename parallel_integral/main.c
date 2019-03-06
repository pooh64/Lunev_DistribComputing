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

/* Turbo boost:
 * https://www.kernel.org/doc/Documentation/cpu-freq/boost.txt
 * /sys/devices/system/cpu/cpufreq/boost */

/* Set one cpu per core in cpuset, returns number of cores */
int set_single_cpus(cpu_set_t *cpuset)
{
	DIR *sysfs_cpudir = opendir("/sys/bus/cpu/devices");
	if (!sysfs_cpudir)
		return -1;

	/* assoc_cpu_id = assoc_cpus[core_id] */
	int *assoc_cpus = malloc(sizeof(int) * CPU_SETSIZE);
	if (!assoc_cpus) {
		closedir(sysfs_cpudir);
		return -1;
	}
	int *ptr = assoc_cpus;
	for (size_t i = CPU_SETSIZE; i != 0; i--, ptr++)
		*ptr = -1;

	errno = 0;
	int n_cores = 0;
	struct dirent *entry;
	char buf[512];

	/* Find one cpu per one core */
	while ((entry = readdir(sysfs_cpudir)) != NULL) {
		if (!memcmp(entry->d_name, "cpu", 3)) {
			int cpu_id = atoi(entry->d_name + 3);
			sprintf(buf, "/sys/bus/cpu/devices/%s/topology/core_id\0", entry->d_name);
			int fd = open(buf, O_RDONLY);
			if (fd == -1)
				goto handle_err;
			ssize_t ret = read(fd, buf, sizeof(buf) - 1);
			if (close(fd) == -1)
				goto handle_err;
			if (ret == -1)
				goto handle_err;
			buf[ret] = '\0';
			int core_id = atoi(buf);
			/* printf("cpu: %d core: %d\n", cpu_id, core_id); */
			assoc_cpus[core_id] = cpu_id;
			n_cores++;
		}
	}

	if (errno)
		goto handle_err;

	/* Translate it to cpu_set */
	CPU_ZERO(cpuset);
	int id = 0;
	for (int n = n_cores; n != 0; id++) {
		if (assoc_cpus[id] != -1) {
			CPU_SET(assoc_cpus[id], cpuset);
			n--;
		}
	}

	closedir(sysfs_cpudir);
	return n_cores;

handle_err:
	free(assoc_cpus);
	closedir(sysfs_cpudir);
	return -1;
}

int cpu_set_search_next(int cpu, cpu_set_t *set)
{
	for (int i = cpu + 1; i < CPU_SETSIZE; i++) {
		if (CPU_ISSET(cpu, set))
			return i;
	}
	return 0;
}

#define CACHE_CELL_ALIGN 128

struct task_container {
	double base;
	double step_wdth;
	double accum;
	size_t start_step;
	size_t n_steps;
	int cpu;
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
	size_t cur_step = pack->start_step;
	double sum = 0;
	double base = pack->base;
	double step_wdth = pack->step_wdth;

	for (size_t i = pack->n_steps; i != 0; i--, cur_step++) {
		sum += func_to_integrate(base + cur_step * step_wdth) * step_wdth;
	}

	pack->accum = sum;

	return NULL;
}

void integrate_split_task(int n_threads, cpu_set_t *cpuset, double from, 
	double to, double step, struct task_container_align *tasks)
{
	int n_cpus = CPU_COUNT(cpuset);
	if (n_threads < n_cpus)
		n_cpus = n_threads;

	size_t n_steps = (to - from) / step;
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
			struct task_container *ptr = (struct task_container*) (tasks + cur_thread);
			ptr->base = from;
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

/* Handle memleaks!!! */

int integrate(int n_threads, cpu_set_t *cpuset, 
	      double from, double to, double step, double *result)
{
	/* Allocate cache-aligned task containers */
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

	/* Split task btw cpus and threads */
	integrate_split_task(n_threads, cpuset, from, to, step, tasks);

	/* Move main thread to other cpu before load start */
	struct task_container *main_task = (struct task_container*) (tasks + n_threads - 1);
	cpu_set_t cpuset_tmp;
	CPU_ZERO(&cpuset_tmp);
	CPU_SET(main_task->cpu, &cpuset_tmp);
	if (sched_setaffinity(getpid(), sizeof(cpuset_tmp), &cpuset_tmp) == -1)
		return -1;

	pthread_attr_t attr;
	pthread_attr_init(&attr);

	/* Load n - 1 threads */
	for (int i = 0; i < n_threads - 1; i++) {
		struct task_container *ptr = (struct task_container*) (tasks + i);
		CPU_ZERO(&cpuset_tmp);
		CPU_SET(ptr->cpu, &cpuset_tmp);
		pthread_attr_setaffinity_np(&attr, sizeof(cpuset_tmp), &cpuset_tmp);
		int ret = pthread_create(threads + i, &attr, task_worker, ptr);
		if (ret)
			return -1;
	}

	/* Load main thread */
	task_worker(main_task);
	double accum = main_task->accum;

	/* Accumulate result */
	for (int i = 0; i < n_threads - 1; i++) {
		struct task_container *ptr = (struct task_container*) (tasks + i);
		int ret = pthread_join(*(threads + i), NULL);
		if (ret)
			return -1;
		accum += ptr->accum;
	}

	*result = accum;

	if (threads)
		free(threads);
	free(tasks);
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Error: wrong argv\n");
		exit(EXIT_FAILURE);
	}

	int n_threads = atoi(argv[1]);

	cpu_set_t cpuset;
	int n_cores = set_single_cpus(&cpuset);
	if (n_cores == -1) {
		perror("Error: set_single_cores");
		exit(EXIT_FAILURE);
	}

	double from = 0;
	double to = 10000;
	double step = 0.0001;
	double result;
	if (integrate(n_threads, &cpuset, from, to, step, &result) == -1) {
		perror("Error: integrate");
		exit(EXIT_FAILURE);
	}

	printf("result: %.*lg\n", DBL_DIG, result);

	return 0;
}
