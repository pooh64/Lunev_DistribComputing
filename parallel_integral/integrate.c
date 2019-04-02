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
#include <setjmp.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

/* Signal handler exception */
jmp_buf sig_exc_buf;

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

/* It's error-cleanup func, no err handler */
int integrate_cancel_tasks(pthread_t *threads, int n_threads)
{
	for (; n_threads != 0; n_threads--, threads++)
		pthread_cancel(*threads);
	
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
	if (setjmp(sig_exc_buf)) {
		fprintf(stderr, "Error: signal-exception caught\n");
		goto handle_err;
	}

	int n_threads = CPU_COUNT(cpuset);

	/* Allocate cache-aligned task containers */
	struct task_container_align *tasks = 
		aligned_alloc(sizeof(*tasks), sizeof(*tasks) * n_threads);
	if (!tasks) {
		perror("Error: aligned_alloc");
		goto handle_err;
	}
	
	pthread_t *threads = malloc(sizeof(*threads) * n_threads);
	if (!threads) {
		perror("Error: malloc");
		goto handle_err;
	}

	/* Split task btw cpus and threads */
	integrate_split_tasks(tasks, n_threads, cpuset, n_steps, base, step);
	
	/* Move main thread to other cpu */
	if (set_this_thread_cpu(tasks[0].task.cpu))
		goto handle_err;
	
	/* Run non-main tasks */
	if (integrate_run_tasks(tasks + 1, threads + 1, n_threads - 1))
		goto handle_err;

	/* Run main task */
	integrate_task_worker(&tasks[0].task);
	
	/* Finish non-main tasks */
	if (integrate_join_tasks(threads + 1, n_threads - 1))
		goto handle_err;
	
	/* Sumary */
	*result = integrate_accumulate_result(tasks, n_threads);

	free(tasks);
	return 0;

handle_err:
	if (threads) {
		integrate_cancel_tasks(threads + 1, n_threads - 1);
		free(threads);
	}

	if (tasks)
		free(tasks);
	return -1;
}

/* Baaaad, fighting with TurboBoost */
int integrate_multicore_scalable(int n_threads, cpu_set_t *cpuset, size_t n_steps,
	long double base, long double step, long double *result)
{
	if (setjmp(sig_exc_buf)) {
		fprintf(stderr, "Error: signal-exception caught\n");
		goto handle_err;
	}

	/* Allocate cache-aligned task containers */
	struct task_container_align *tasks = NULL;
	tasks = aligned_alloc(sizeof(*tasks), sizeof(*tasks) * n_threads);
	if (!tasks) {
		perror("Error: aligned_alloc");
		goto handle_err;
	}
	
	/* The same with overloading threads */
	int n_bad_threads = 0;
	struct task_container_align *bad_tasks = NULL;
	if (CPU_COUNT(cpuset) > n_threads) {
		n_bad_threads = CPU_COUNT(cpuset) - n_threads;
		bad_tasks = aligned_alloc(sizeof(*bad_tasks),
			sizeof(*bad_tasks) * n_bad_threads);
		if (!bad_tasks) {
			perror("Error: aligned_alloc");
			goto handle_err;
		}
	}

	pthread_t *threads = NULL;
	threads = malloc(sizeof(*threads) * n_threads);
	if (!threads) {
		perror("Error: malloc");
		goto handle_err;
	}

	pthread_t *bad_threads = NULL;
	if (n_bad_threads) {
		bad_threads = malloc(sizeof(*bad_threads) * n_bad_threads);
		if (!bad_threads) {
			perror("Error: malloc");
			goto handle_err;
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
	if (n_bad_threads && integrate_run_tasks(bad_tasks,
	    bad_threads, n_bad_threads))
		goto handle_err;
	
	/* Run non-main tasks */
	if (integrate_run_tasks(tasks + 1, threads + 1, n_threads - 1))
		goto handle_err;

	/* Run main task */
	integrate_task_worker(&tasks[0].task);
	
	/* Finish bad tasks */
	if (n_bad_threads && integrate_join_tasks(bad_threads, n_bad_threads))
		goto handle_err;
	
	/* Finish non-main tasks */
	if (integrate_join_tasks(threads + 1, n_threads - 1))
		goto handle_err;
	
	/* Sumary */
	*result = integrate_accumulate_result(tasks, n_threads);

	if (n_bad_threads) {
		free(bad_tasks);
		free(bad_threads);
	}
	free(tasks);
	free(threads);
	return 0;

handle_err:
	if (bad_threads) {
		integrate_cancel_tasks(bad_threads, n_bad_threads);
		free(bad_threads);
	}

	if (threads) {
		integrate_cancel_tasks(threads + 1, n_threads - 1);
		free(threads);
	}

	if (bad_tasks)
		free(bad_tasks);

	if (tasks)
		free(tasks);
	return -1;
}


/********************** Network Worker *************************/

struct task_netw {
	long double base;
	long double step_wdth;
	size_t start_step;
	size_t n_steps;
};

int netw_sigio_handler_socket = -1;

void netw_sigio_handler(int sig)
{
	DUMP_LOG("Signal caught: %d\n", sig);

	if (netw_sigio_handler_socket < 0)
	       return;	

	/* No more async */
	fcntl(netw_sigio_handler_socket, F_SETFL, 0);
	longjmp(sig_exc_buf, sig);
}

/* Read and write same size blocks from TCP socket */
ssize_t netw_tcp_read(int sock, void *buf, size_t buf_s)
{
	ssize_t ret = read(sock, buf, buf_s);
	if (ret < 0) {
		perror("Error: read");
		return -1;
	}
	if (ret == 0) {
		fprintf(stderr, "Error: connection lost\n");
		return -1;
	}
	if (ret != buf_s) {
		fprintf(stderr, "Error: nonfull read\n");
		return -1;
	}
	return ret;
}

ssize_t netw_tcp_write(int sock, void *buf, size_t buf_s)
{
	ssize_t ret = write(sock, buf, buf_s);
	if (ret < 0) {
		perror("Error: write");
		return -1;
	}
	if (ret == 0) {
		fprintf(stderr, "Error: connection lost\n");
		return -1;
	}
	if (ret != buf_s) {
		fprintf(stderr, "Error: nonfull write\n");
		return -1;
	}
	return ret;
}

int netw_socket_keepalive(int sock)
{
	int yes = 1;
	int idle = 1;
	int interval = 1;
	int maxpkt = 1;
	
	if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int)) < 0) {
		perror("Error: setsockopt");
		return -1;
	}
	
	if (setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, &idle, sizeof(int)) < 0) {
		perror("Error: setsockopt");
		return -1;
	}
	
	if (setsockopt(sock, SOL_TCP, TCP_KEEPINTVL, &interval, sizeof(int)) < 0) {
		perror("Error: setsockopt");
		return -1;
	}
	
	if (setsockopt(sock, SOL_TCP, TCP_KEEPCNT, &maxpkt, sizeof(int)) < 0) {
		perror("Error: setsockopt");
		return -1;
	}
	
	return 0;
}

int worker_udp_prepare_socket(in_port_t port)
{
	int udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (udp_sock == -1) {
		perror("Error: socket");
		goto handle_err_0;
	}
	
	int val = 1;
	if (setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val)) < 0) {
		perror("Error: setsockopt");
		goto handle_err_1;
	}
	
	val = 1;
	if (setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
		perror("Error: setsockopt");
		goto handle_err_1;
	}
	
	struct sockaddr_in addr;
	addr.sin_family      = AF_INET;
	addr.sin_port        = port;
	addr.sin_addr.s_addr = INADDR_BROADCAST;
	
	if (bind(udp_sock, &addr, sizeof(addr))) {
		perror("Error: bind");
		goto handle_err_1;
	}
	
	return udp_sock;
	
handle_err_1:
	close(udp_sock);
handle_err_0:
	return -1;
}

int worker_wait_udp_msg(int udp_sock, int udp_msg, struct sockaddr_in *src)
{
	while (1) {
		DUMP_LOG("Waiting for udp_msg\n");
		socklen_t addr_len = sizeof(*src);
		
		int received;
		if (recvfrom(udp_sock, &received, sizeof(received), 0, src, &addr_len) < 0) {
			perror("Error: recvfrom");
			return -1;
		}
		
		DUMP_LOG("Received: %d\n", received);
		if (received != udp_msg) {
			DUMP_LOG("Not equal to %d\n", udp_msg);
		} else
			break;
	}
	
	return 0;
}

int worker_tcp_connect(struct sockaddr_in *addr, struct timeval *timeout)
{
	int tcp_sock = socket(PF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (tcp_sock == -1) {
		perror("Error: socket");
		goto handle_err_0;
	}
	
	if (netw_socket_keepalive(tcp_sock) < 0) {
		fprintf(stderr, "Error: netw_socket_keepalive");
		goto handle_err_1;
	}
	
	fd_set set;
	FD_ZERO(&set);
	FD_SET(tcp_sock, &set);
	
	int ret = connect(tcp_sock, addr, sizeof(*addr));
	if (ret < 0 && errno == EINPROGRESS) {
		DUMP_LOG("Connecting to starter...\n");
		ret = select(tcp_sock + 1, NULL, &set, NULL, timeout);
		if (ret == 0) {
			fprintf(stderr, "Error: connect timed out\n");
			close(tcp_sock);
			return 1;	// Note this
		}
		if (ret < 0) {
			perror("Error: select");
			goto handle_err_1;
		}
		
		int val;
		socklen_t val_len = sizeof(val);
		ret = getsockopt(tcp_sock, SOL_SOCKET, SO_ERROR, &val, &val_len);
		if (ret == -1) {
			perror("Error: getsockopt");
			goto handle_err_1;
		}
		if (val != 0) {
			errno = val;
			perror("Error: connect");
			goto handle_err_1;
		}
	} else if (ret < 0) {
		perror("Error: connect");
		goto handle_err_1;
	}
		
	DUMP_LOG("Successfully connected\n");
	DUMP_LOG("\t(%ld usec remaining)\n", timeout->tv_usec);
	
	if (fcntl(tcp_sock, F_SETFL, 0) < 0) {
		perror("Error: fcntl");
		goto handle_err_1;
	}

	if (fcntl(tcp_sock, F_SETOWN, getpid()) < 0) {
		perror("Error: fcntl");
		goto handle_err_1;
	}
	
	return tcp_sock;

handle_err_1:
	close(tcp_sock);
handle_err_0:
	return -1;
}

int integrate_network_worker(int n_threads, cpu_set_t *cpuset)
{
	fprintf(stderr, "-------- Starting worker (%d threads) --------\n", n_threads);

	/* Set SIGPIPE here */
	struct sigaction act = { };
	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) < 0) {
		perror("Error: sigaction");
		goto handle_err_0;
	}

	act.sa_handler = netw_sigio_handler;
	if (sigaction(SIGIO, &act, NULL) < 0) {
		perror("Error: sigaction");
		goto handle_err_0;
	}
	
	/* Prepare UDP socket */
	int udp_sock = worker_udp_prepare_socket(htons(INTEGRATE_UDP_PORT));
	if (udp_sock < 0) {
		goto handle_err_0;
	}
	
	/* Process requests */
	int tcp_sock;
	
	while (1) {
		/* Wait for broadcast */
		struct sockaddr_in starter_addr;
		if (worker_wait_udp_msg(udp_sock, INTEGRATE_UDP_MAGIC, &starter_addr) < 0) {
			goto handle_err_1;
		}
		
		/* Connect to starter */
		struct timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = INTEGRATE_NETW_TIMEOUT_USEC;
		starter_addr.sin_port = htons(INTEGRATE_TCP_PORT);
		
		tcp_sock = worker_tcp_connect(&starter_addr, &timeout);
		if (tcp_sock < 0) {
			goto handle_err_1;
		}

		/* Prepare exception handler */
		if (setjmp(sig_exc_buf)) {
			fprintf(stderr, "Error: connection lost\n");
			goto handle_err_2;
		}

		/* Enable async */
		netw_sigio_handler_socket = tcp_sock;
		if (fcntl(tcp_sock, F_SETFL, O_ASYNC) < 0) {
			perror("Error: fcntl");
			goto handle_err_2;
		}
		
		/* Send ncpus */
		DUMP_LOG("n_threads: %d, sending...\n", n_threads);
		
		if (netw_tcp_write(tcp_sock, &n_threads, sizeof(n_threads)) < 0) {
			fprintf(stderr, "Error: write n_threads to starter\n");
			goto handle_err_2;
		}
		
		/* Receive task */
		struct task_netw task;
		if (netw_tcp_read(tcp_sock, &task, sizeof(task)) < 0) {
			fprintf(stderr, "Error: read task from starter\n");
			goto handle_err_2;
		}

		/* *********************************** */

		/* Process task */
		DUMP_LOG("task:\n\tfrom = %Lg\n\tto = %Lg\n\tstep = %Lg\n",
			 task.base + task.step_wdth * task.start_step,
			 task.base + task.step_wdth * (task.start_step + task.n_steps),
			 task.step_wdth);
		long double result;
		
		if (integrate_multicore_scalable(n_threads, cpuset,
			task.n_steps, task.base + task.step_wdth * task.start_step,
			task.step_wdth, &result) < 0) {
			fprintf(stderr, "Error: integrate failed\n");
			goto handle_err_2;
		}

		/* Disable async */
		if (fcntl(tcp_sock, F_SETFL, 0) < 0) {
			perror("Error: fcntl");
			goto handle_err_2;
		}
		
		/* Send result */
		DUMP_LOG("Result: %Lg, sending...\n", result);
		
		if (netw_tcp_write(tcp_sock, &result, sizeof(result)) < 0) {
			fprintf(stderr, "Error: write result to starter\n");
			goto handle_err_2;
		}
		
		DUMP_LOG("-------- Result sent, request done --------\n");
		close(tcp_sock);
	}
	
	return 0;

handle_err_2:
	close(tcp_sock);
handle_err_1:
	close(udp_sock);
handle_err_0:
	return -1;
}


/********************** Network Starter *************************/


int starter_tcp_listen_socket(in_port_t port, int max_listen)
{	
	int tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (tcp_sock == -1) {
		perror("Error: socket");
		goto handle_err_0;
	}
	
	int val = 1;
	if (setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
		perror("Error: setsockopt");
		goto handle_err_1;
	}
	
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = port;
	addr.sin_addr.s_addr = INADDR_ANY;
	
	if (bind(tcp_sock, &addr, sizeof(addr))) {
		perror("Error: bind");
		goto handle_err_1;
	}
	
	if (listen(tcp_sock, max_listen)) {
		perror("Error: listen");
		goto handle_err_1;
	}
	
	return tcp_sock;

handle_err_1:
	close(tcp_sock);
handle_err_0:
	return -1;
}

int starter_udp_broadcast_msg(in_port_t port, int udp_msg)
{
	int udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (udp_sock == -1) {
		perror("Error: socket");
		goto handle_err_0;
	}
	
	int val = 1;
	if (setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val)) < 0) {
		perror("Error: setsockopt");
		goto handle_err_1;
	}
	
	DUMP_LOG("Broadcasting udp_msg to workers\n");
	struct sockaddr_in addr;
	addr.sin_family      = AF_INET;
	addr.sin_port        = port;
	addr.sin_addr.s_addr = INADDR_BROADCAST;
	
	if (sendto(udp_sock, &udp_msg, sizeof(udp_msg), 0, &addr, sizeof(addr)) < 0) {
		perror("Error: sendto");
		goto handle_err_1;
	}
	
	close(udp_sock);
	return 0;
	
handle_err_1:
	close(udp_sock);
handle_err_0:
	return -1;
}

int starter_accept_connections(int tcp_sock, int *sockets, int max_sockets, struct timeval *timeout)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(tcp_sock, &set);
	
	int n_workers = 0;
	
	for (; n_workers < max_sockets; n_workers++) {
		/* Unportable, Linux-only */
		int ret = select(tcp_sock + 1, &set, NULL, NULL, timeout);
		if (ret == 0) {
			DUMP_LOG("Accept timed out, %d workers accepted\n", n_workers);
			break;
		}
		if (ret < 0) {
			perror("Error: select");
			goto handle_err;
		}
		
		ret = accept(tcp_sock, NULL, NULL);
		if (ret == -1) {
			perror("Error: accept");
			goto handle_err;
		}
		sockets[n_workers] = ret;
		
		DUMP_LOG("Accepted connection №%d\n", n_workers + 1);
		DUMP_LOG("\t(%ld usec remaining)\n", timeout->tv_usec);
		
		if (netw_socket_keepalive(sockets[n_workers]) < 0) {
			fprintf(stderr, "Error: netw_socket_keepalive");
			goto handle_err;
		}
	}
	
	return n_workers;
	
handle_err:
	while (n_workers--)
		close(sockets[n_workers]);

	return -1; 
}

int starter_send_tasks(int *sockets, int *cpus, int n_sockets, struct task_netw *full_task)
{
	int sum_cpus = 0;
	for (int i = 0; i < n_sockets; i++) 
		sum_cpus += cpus[i];

	struct task_netw task;
	task.base	= full_task->base;
	task.step_wdth  = full_task->step_wdth;
	size_t cur_step = full_task->start_step;
	size_t n_steps  = full_task->n_steps;
	
	for (; n_sockets != 0; n_sockets--) {
	
		task.start_step = cur_step;
		task.n_steps    = (n_steps * cpus[n_sockets - 1]) / sum_cpus;
		
		cur_step       += task.n_steps;
		n_steps        -= task.n_steps;
		sum_cpus       -= cpus[n_sockets - 1];
		
		DUMP_LOG("Sending task to worker[%d]\n", n_sockets - 1);
		
		ssize_t ret = write(sockets[n_sockets - 1], &task, sizeof(task));
		if (ret < 0) {
			if (errno == EPIPE)
				fprintf(stderr, "Error: connection to worker[%d] lost (EPIPE)\n", n_sockets - 1);
			else
				perror("Error: write");
			return -1;
		}
		if (ret != sizeof(task)) {
			fprintf(stderr, "Error: nonfull write\n");
			return -1;
		}
	}
	
	return 0;
}

int starter_accumulate_result(int *sockets, int n_sockets, long double *result)
{
	DUMP_LOG("Receiving sum...\n");
	long double accum = 0;
	for (; n_sockets != 0; n_sockets--) {
		long double sum;
		if (netw_tcp_read(sockets[n_sockets - 1], &sum, sizeof(sum)) < 0) {
			fprintf(stderr, "Error: connection with worker[%d] lost\n", n_sockets - 1);
			return -1;
		}
		
		DUMP_LOG("worker[%d] sum = %Lg\n", n_sockets - 1, sum);
		accum += sum;
	}
	
	*result = accum;
	return 0;
}

int starter_get_workers_ncpus(int *sockets, int *ncpu, int n_sockets)
{
	DUMP_LOG("Receiving ncpus...\n");
	for (; n_sockets != 0; n_sockets--) {
		if (netw_tcp_read(sockets[n_sockets - 1], &ncpu[n_sockets - 1], sizeof(*ncpu)) < 0) {
			fprintf(stderr, "Error: connection with worker[%d] lost\n", n_sockets - 1);
			return -1;
		}
		
		DUMP_LOG("worker[%d] ncpus = %d\n", n_sockets - 1, ncpu[n_sockets - 1]);
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
		goto handle_err_0;
	}
	
	/* Prepare TCP socket */
	int tcp_sock = starter_tcp_listen_socket(htons(INTEGRATE_TCP_PORT), INTEGRATE_MAX_WORKERS);
	if (tcp_sock < 0) {
		fprintf(stderr, "Error: starter_tcp_listen_socket failed\n");
		goto handle_err_0;
	}

	/* UDP Broadcast */
	if (starter_udp_broadcast_msg(htons(INTEGRATE_UDP_PORT), INTEGRATE_UDP_MAGIC) < 0) {
		fprintf(stderr, "Error: starter_udp_broadcast_msg failed\n");
		goto handle_err_1;
	}
	
	/* Accept TCP connections */
	int worker_sock[INTEGRATE_MAX_WORKERS];
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = INTEGRATE_NETW_TIMEOUT_USEC;
	
	int n_workers = starter_accept_connections(tcp_sock, worker_sock, INTEGRATE_MAX_WORKERS, &timeout);
	if (n_workers < 0) {
		fprintf(stderr, "Error: starter_accept_connections failed\n");
		goto handle_err_1;
	}
	if (n_workers == 0) {
		DUMP_LOG("No workers aviable\n");
		goto handle_err_1;
	}
	
	/* Get numbers of cpus */
	int worker_ncpu[INTEGRATE_MAX_WORKERS];
	
	if (starter_get_workers_ncpus(worker_sock, worker_ncpu, n_workers) < 0) {
		fprintf(stderr, "Error: starter_get_workers_ncpus failed\n");
		goto handle_err_2;
	}
	
	/* Split task and send requests */
	struct task_netw task;
	task.base = base;
	task.step_wdth = step;
	task.start_step = 0;
	task.n_steps = n_steps;
	
	if (starter_send_tasks(worker_sock, worker_ncpu, n_workers, &task) < 0) {
		fprintf(stderr, "Error: starter_send_tasks failed\n");
		goto handle_err_2;
	}
	
	/* Accumulate result */
	if (starter_accumulate_result(worker_sock, n_workers, result) < 0) {
		fprintf(stderr, "Error: starter_accumulate_result failed\n");
		goto handle_err_2;
	}

	/* Close sockets */
	while (n_workers--)
		close(worker_sock[n_workers]);
	close(tcp_sock);
	
	return 0;
	
handle_err_2:
	while (n_workers--)
		close(worker_sock[n_workers]);
handle_err_1:
	close(tcp_sock);
handle_err_0:
	return -1;
}
