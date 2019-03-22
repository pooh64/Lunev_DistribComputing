#include "integrate.h"

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

#include <netinet/in.h>
#include <sys/socket.h>

struct task_container {
	long double base;
	long double step_wdth;
	long double accum;

	size_t start_step;
	size_t n_steps;
	
	pthread_t thread;
	int cpu;
};

struct task_container_align {
	struct task_container task;
	uint8_t padding[CACHE_LINE_ALIGN -
		sizeof(struct task_container)];
};

static inline long double func_to_integrate(long double x)
{
	return 2 / (1 + x * x);
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

	DUMP_LOG(fprintf(stderr, "worker: from: %Le "
				 "to: %Le sum: %Le arg: %p\n",
			 dump_from, dump_to, sum, arg));

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
	DUMP_LOG(fprintf(stderr, "setting main   to cpu = %2d\n", cpu)); 
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
		DUMP_LOG(fprintf(stderr, "setting worker to cpu = %2d\n",
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
	pthread_attr_destroy(&attr);
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

int integrate_multicore(cpu_set_t *cpuset, size_t n_steps,
	long double base, long double step, long double *result)
{
	int n_threads = CPU_COUNT(cpuset);

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

/* Baaaad */
int integrate_multicore_abused(int n_threads, cpu_set_t *cpuset, size_t n_steps,
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


int integrate_network_worker(cpu_set_t *cpuset)
{
	DUMP_LOG(fprintf(stderr, "Starting worker\n"));
	
	int sk_udp = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk_udp == -1) {
		perror("Error: socket");
		return -1;
	}
	
	int val = 1;
	ssize_t ret;
	
	ret = setsockopt(sk_udp, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val));
	if (ret == -1) {
		perror("Error: setsockopt");
		return -1;
	}
	
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(INTEGRATE_UDP_PORT);
	addr.sin_addr.s_addr = INADDR_BROADCAST;	// Broadcast/any?, htons?
	
	if (bind(sk_udp, &addr, sizeof(addr))) {
		perror("Error: bind");
		return -1;
	}
	
	while (1) {
		int udp_msg;
		socklen_t addr_len;
		DUMP_LOG(fprintf(stderr, "Waiting for udp_msg\n"));
		ret = recvfrom(sk_udp, &udp_msg, sizeof(udp_msg), 0, &addr, &addr_len);
		if (ret == -1) {
			perror("Error: recvfrom");
			return -1;
		}
		DUMP_LOG(fprintf(stderr, "Received udp_msg: %d\n", udp_msg));
		
		// check msg
		// receive task (tcp)
		// do
		// send result	
	}
	
	return 0;
}


int integrate_network_starter(size_t n_steps, long double base,
	long double step, long double *result)
{
	DUMP_LOG(fprintf(stderr, "Starting starter\n"));
	
	int sk_udp = socket(PF_INET, SOCK_DGRAM, 0);
	if (sk_udp == -1) {
		perror("Error: socket");
		return -1;
	}
	
	int val = 1;
	ssize_t ret;
	ret = setsockopt(sk_udp, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val));
	if (ret == -1) {
		perror("Error: setsockopt");
		return -1;
	}
	
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(INTEGRATE_UDP_PORT);
	addr.sin_addr.s_addr = INADDR_BROADCAST;	// Broadcast/any?, htons?
	
	int udp_msg = 1234;
	DUMP_LOG(fprintf(stderr, "Sending udp_msg: %d\n", udp_msg));
	ret = sendto(sk_udp, &udp_msg, sizeof(udp_msg), 0, &addr, sizeof(addr));
	if (ret == -1) {
		perror("Error: sendto");
		return -1;
	}
	
	close(sk_udp);
	
	/////
	
	int sk_tcp = socket(PF_INET, SOCK_STREAM, 0);
	if (sk_tcp == -1) {
		perror("Error: socket");
		return -1;
	}
	
	addr.sin_family = AF_INET;
	addr.sin_port = htons(INTEGRATE_TCP_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	// send task (tcp)
	// wait for results (with timeout)
	
	return 0;
}

