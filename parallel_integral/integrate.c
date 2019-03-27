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
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <limits.h>
#include <signal.h>

#include <netinet/in.h>
#include <sys/socket.h>

struct task_container {
	long double base;
	long double step_wdth;
	long double accum;

	size_t start_step;
	size_t n_steps;
	
	int cpu;
};

struct task_container_align {
	struct task_container task;
	uint8_t padding[CACHE_LINE_ALIGN -
		sizeof(struct task_container)];
};

void *integrate_task_worker(void *arg)
{
	struct task_container *pack = arg;
	register worker_tmp_t base      = pack->base;
	register worker_tmp_t step_wdth = pack->step_wdth;
		 size_t       n_steps   = pack->n_steps;
	register size_t       cur_step  = pack->start_step;
	register worker_tmp_t sum = 0;

	DUMP_LOG_DO(worker_tmp_t dump_from = base + cur_step * step_wdth);
	DUMP_LOG_DO(worker_tmp_t dump_to   = base + (cur_step + pack->n_steps) *
					     step_wdth);

	for (register size_t i = n_steps; i != 0; i--, cur_step++) {
		register worker_tmp_t x = base + cur_step * step_wdth;
		sum += INTEGRATE_FUNC(x) * step_wdth;
	}

	pack->accum = sum;

	DUMP_LOG("worker: from: %lg "
		 "to: %lg sum: %lg arg: %p\n",
		 (double) dump_from, (double) dump_to, (double) sum, arg);

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
	DUMP_LOG("setting main   to cpu = %2d\n", cpu); 
	if (sched_setaffinity(getpid(), 
	    sizeof(cpuset_tmp), &cpuset_tmp) == -1) {
		perror("Error: sched_setaffinity");
		return -1;
	}
	return 0;
}

int integrate_run_tasks(struct task_container_align *tasks,
	pthread_t *threads, int n_tasks)
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	cpu_set_t cpuset_tmp;
	for (int i = 0; i < n_tasks; i++) {
		struct task_container *ptr = &tasks[i].task;
		CPU_ZERO(&cpuset_tmp);
		CPU_SET(ptr->cpu, &cpuset_tmp);
		DUMP_LOG("setting worker to cpu = %2d\n",
				 ptr->cpu);
		int ret = pthread_attr_setaffinity_np(&attr,
			sizeof(cpuset_tmp), &cpuset_tmp);
		if (ret) {
			perror("Error: pthread_attr_setaffinity_np");
			return -1;
		}
		ret = pthread_create(&threads[i], &attr,
			integrate_task_worker, ptr);
		if (ret) {
			perror("Error: pthread_create");
			return -1;
		}
	}
	pthread_attr_destroy(&attr);
	return 0;
}

int integrate_join_tasks(pthread_t *threads, int n_threads)
{
	for (; n_threads != 0; n_threads--, threads++) {
		if (pthread_join(*threads, NULL)) {
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
		goto handle_err_0;
	}
	
	pthread_t *threads = malloc(sizeof(*threads) * n_threads);
	if (!threads) {
		perror("Error: malloc");
		goto handle_err_1;
	}

	/* Split task btw cpus and threads */
	integrate_split_tasks(tasks, n_threads, cpuset, n_steps, base, step);
	
	/* Move main thread to other cpu */
	if (set_this_thread_cpu(tasks[0].task.cpu))
		goto handle_err_2;
	
	/* Run non-main tasks */
	if (integrate_run_tasks(tasks + 1, threads + 1, n_threads - 1))
		goto handle_err_2;

	/* Run main task */
	integrate_task_worker(&tasks[0].task);
	
	/* Finish non-main tasks */
	if (integrate_join_tasks(threads + 1, n_threads - 1))
		goto handle_err_2;
	
	/* Sumary */
	*result = integrate_accumulate_result(tasks, n_threads);

	free(tasks);
	return 0;

handle_err_2:
	free(threads);
handle_err_1:
	free(tasks);
handle_err_0:
	return -1;
}

/* Baaaad, fighting with TurboBoost */
int integrate_multicore_abused(int n_threads, cpu_set_t *cpuset, size_t n_steps,
	long double base, long double step, long double *result)
{
	/* Allocate cache-aligned task containers */
	struct task_container_align *tasks = 
		aligned_alloc(sizeof(*tasks), sizeof(*tasks) * n_threads);
	if (!tasks) {
		perror("Error: aligned_alloc");
		goto handle_err_0;
	}
	
	/* The same with overloading threads */
	int n_bad_threads = 0;
	struct task_container_align *bad_tasks;
	if (CPU_COUNT(cpuset) > n_threads) {
		n_bad_threads = CPU_COUNT(cpuset) - n_threads;
		bad_tasks = aligned_alloc(sizeof(*bad_tasks),
			sizeof(*bad_tasks) * n_bad_threads);
		if (!bad_tasks) {
			perror("Error: aligned_alloc");
			goto handle_err_1;
		}
	}

	pthread_t *threads = malloc(sizeof(*threads) * n_threads);
	if (!threads) {
		perror("Error: malloc");
		goto handle_err_2;
	}

	pthread_t *bad_threads;
	if (n_bad_threads) {
		bad_threads = malloc(sizeof(*bad_threads) * n_bad_threads);
		if (!bad_threads) {
			perror("Error: malloc");
			goto handle_err_3;
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
		goto handle_err_4;
	
	/* Run bad tasks */
	if (n_bad_threads && integrate_run_tasks(bad_tasks,
	    bad_threads, n_bad_threads))
		goto handle_err_4;
	
	/* Run non-main tasks */
	if (integrate_run_tasks(tasks + 1, threads + 1, n_threads - 1))
		goto handle_err_4;

	/* Run main task */
	integrate_task_worker(&tasks[0].task);
	
	/* Finish bad tasks */
	if (n_bad_threads && integrate_join_tasks(bad_threads, n_bad_threads))
		goto handle_err_4;
	
	/* Finish non-main tasks */
	if (integrate_join_tasks(threads + 1, n_threads - 1))
		goto handle_err_4;
	
	/* Sumary */
	*result = integrate_accumulate_result(tasks, n_threads);

	if (n_bad_threads) {
		free(bad_tasks);
		free(bad_threads);
	}
	free(tasks);
	free(threads);
	return 0;

handle_err_4:
	if (n_bad_threads)
		free(bad_threads);
handle_err_3:
	free(threads);
handle_err_2:
	if (n_bad_threads)
		free(bad_tasks);
handle_err_1:
	free(tasks);
handle_err_0:
	return -1;
}


struct task_netw {
	long double base;
	long double step_wdth;
	size_t start_step;
	size_t n_steps;
};

int integrate_network_worker(cpu_set_t *cpuset)
{
	fprintf(stderr, "Starting worker\n");

	/* Set SIGPIPE here */
	
	struct sigaction act = { };
	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) < 0) {
		perror("Error: sigaction");
		return -1;
	}
	
	/* Prepare UDP socket */
	
	struct sockaddr_in addr;
	socklen_t addr_len;
	ssize_t ret;
	int val;
	socklen_t val_len;
	
	int udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (udp_sock == -1) {
		perror("Error: socket");
		return -1;
	}
	
	val = 1;
	ret = setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val));
	if (ret == -1) {
		perror("Error: setsockopt");
		return -1;
	}
	
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(INTEGRATE_UDP_PORT);
	addr.sin_addr.s_addr = INADDR_BROADCAST;
	
	if (bind(udp_sock, &addr, sizeof(addr))) {
		perror("Error: bind");
		return -1;
	}
	
	/* Process requests */
	
	while (1) {
		/* Wait for broadcast */
	
		DUMP_LOG("Waiting for udp_msg\n");
		int udp_msg;
		addr_len = sizeof(addr);
		
		ret = recvfrom(udp_sock, &udp_msg, sizeof(udp_msg), 0, &addr, &addr_len);
		if (ret == -1) {
			perror("Error: recvfrom");
			return -1;
		}
		
		DUMP_LOG("Received udp_msg: %d\n", udp_msg);
		if (udp_msg != INTEGRATE_UDP_MAGIC) {
			DUMP_LOG("Not equal to INTEGRATE_UDP_MAGIC\n");
			continue;
		}
		
		/* Connect to starter */
		
		addr.sin_port = htons(INTEGRATE_TCP_PORT);
		
		int tcp_sock = socket(PF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
		if (tcp_sock == -1) {
			perror("Error: socket");
			return -1;
		}
		
		fd_set set;
		FD_ZERO(&set);
		FD_SET(tcp_sock, &set);
		
		ret = connect(tcp_sock, &addr, sizeof(addr));
		if (ret < 0 && errno == EINPROGRESS) {
			DUMP_LOG("Connecting to starter...\n");
			struct timeval timeout;
			timeout.tv_sec = 1; 	// Define that
			timeout.tv_usec = 0;
			ret = select(tcp_sock + 1, NULL, &set, NULL, &timeout);
			if (ret == 0) {
				fprintf(stderr, "Error: connection timed out");
				return 1;	// Note this
			}
			if (ret < 0) {
				perror("Error: select");
				return -1;
			}
			
			val_len = sizeof(val);
			ret = getsockopt(tcp_sock, SOL_SOCKET, SO_ERROR, &val, &val_len);
			if (ret == -1) {
				perror("Error: getsockopt");
				return -1;
			}
			if (val != 0) {
				errno = val;
				perror("Error: connection failed");
				return -1;
			}
		} else if (ret < 0) {
			perror("Error: connect");
			return -1;
		}
		
		DUMP_LOG("Successfully connected\n");
		
		if (fcntl(tcp_sock, F_SETFL, 0) < 0) {
			perror("Error: fcntl");
			return -1;
		}
		
		/* Receive task */
		
		struct task_netw task;
		
		ret = read(tcp_sock, &task, sizeof(task));
		if (ret < 0) {
			perror("Error: read");
			return -1;
		}
		if (ret == 0) {
			fprintf(stderr, "Error: connection lost\n");
			return -1;
		}
		if (ret != sizeof(task)) {
			assert(!"Nonfull read");
		}
		
		/* Process task */
		
		DUMP_LOG("task:\n\tfrom = %Lg\n\tto = %Lg\n\tstep = %Lg\n",
			 task.base + task.step_wdth * task.start_step,
			 task.base + task.step_wdth * (task.start_step + task.n_steps),
			 task.step_wdth);
		
		long double result;
		
		if (integrate_multicore(cpuset,
			task.n_steps, task.base, task.step_wdth, &result) == -1) {
			perror("Error: integrate");
			return -1;
		}
		
		/* Send result */
		
		DUMP_LOG("Result: %Lg, sending...\n", result);
		
		ret = write(tcp_sock, &result, sizeof(result));	 // SIGPIPE
		if (ret < 0) {
			if (errno == EPIPE)
				perror("Error: write, connection lost");
			else
				perror("Error: write");
			return -1;
		}
		if (ret != sizeof(result)) {
			assert(!"Nonfull write");
		}
		
		DUMP_LOG("Result sent, request done\n");
	}
	
	return 0;
}


int integrate_network_starter(size_t n_steps, long double base,
	long double step, long double *result)
{	
	fprintf(stderr, "Starting starter\n");
	
	/* Set SIGPIPE here */
	
	struct sigaction act = { };
	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) < 0) {
		perror("Error: sigaction");
		return -1;
	}
	
	/* Prepare TCP socket */
	
	struct sockaddr_in addr;
	socklen_t addr_len;
	int val;
	ssize_t ret;
	
	int tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (tcp_sock == -1) {
		perror("Error: socket");
		return -1;
	}
	
	addr.sin_family = AF_INET;
	addr.sin_port = htons(INTEGRATE_TCP_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;
	
	if (bind(tcp_sock, &addr, sizeof(addr))) {
		perror("Error: bind");
		return -1;
	}
	
	if (listen(tcp_sock, INTEGRATE_MAX_WORKERS)) {
		perror("Error: listen");
		return -1;
	}

	/* UDP Broadcast */
	
	int udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (udp_sock == -1) {
		perror("Error: socket");
		return -1;
	}
	
	val = 1;
	ret = setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val));
	if (ret == -1) {
		perror("Error: setsockopt");
		return -1;
	}
	
	DUMP_LOG("Broadcasting udp_msg to workers\n");
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(INTEGRATE_UDP_PORT);
	addr.sin_addr.s_addr = INADDR_BROADCAST;
	int udp_msg = INTEGRATE_UDP_MAGIC;
	
	ret = sendto(udp_sock, &udp_msg, sizeof(udp_msg), 0, &addr, sizeof(addr));
	if (ret == -1) {
		perror("Error: sendto");
		return -1;
	}
	
	// close(udp_sock);
	
	/* Accept TCP connections */
	
	int worker_sock[INTEGRATE_MAX_WORKERS];
	int n_workers = 0;
	
	fd_set set;
	FD_ZERO(&set);
	FD_SET(tcp_sock, &set);
	struct timeval timeout;
	timeout.tv_sec = 1; // Define that
	timeout.tv_usec = 0;
	
	while (1) {
		/* Unportable, Linux-only */
		ret = select(tcp_sock + 1, &set, NULL, NULL, &timeout);
		if (ret == 0) {
			DUMP_LOG("Accept timed out\n");
			break;
		}
		if (ret < 0) {
			perror("Error: select");
			return -1;
		}
		DUMP_LOG("Accepted connection n%d\n", n_workers);
		ret = accept(tcp_sock, &addr, &addr_len);
		if (ret == -1) {
			perror("Error: accept");
			return -1;
		}
		worker_sock[n_workers++] = ret;
	}
	
	if (!n_workers) {
		DUMP_LOG("No workers aviable");
		return 1; // Note this
	}
	
	/* Split task and send requests */
	struct task_netw task;
	task.base = base;
	task.step_wdth = step;
	size_t cur_step = 0;
	
	for (int i = n_workers - 1; i != 0; i--) {
		task.start_step = cur_step;
		task.n_steps = n_steps / n_workers;
		cur_step += task.n_steps;
		n_steps -= task.n_steps;
		
		DUMP_LOG("Sending task to worker[%d]\n", i);
		
		ret = write(worker_sock[i], &task, sizeof(task));  // SIGPIPE 
		if (ret < 0) {
			if (errno == EPIPE)
				fprintf(stderr, "Error: connection to worker[%d] lost (EPIPE)\n", i);
			else
				perror("Error: write");
			return -1;
		}
		if (ret == 0) {
			assert(!"Null write");
		}
		if (ret != sizeof(task)) {
			assert(!"Nonfull write");
		}
	}
	
	/* Accumulate result */
	long double accum = 0;
	
	for (int i = n_workers - 1; i != 0; i--) {
		long double sum;
		DUMP_LOG("Receiving sum...\n");
		ret = read(worker_sock[i], &sum, sizeof(sum));
		if (ret == 0) {
			perror("Error: connection to worker lost");
			return -1;
		}
		if (ret == -1) {
			perror("Error: read");
			return -1;
		}
		if (ret != sizeof(sum)) {
			assert(!"Nonfull read");
		}
		
		DUMP_LOG("Received sum = %Lg\n", sum);
		
		accum += sum;
	}
	
	*result = accum;
	
	return 0;
}

