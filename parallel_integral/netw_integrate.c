#include "integrate.h"
#include "signal_except.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

/* Network messages format */
struct task_netw {
	long double base;
	long double step_wdth;
	size_t start_step;
	size_t n_steps;
};

typedef int netw_msg_t;

/* Current socket watched by sigio_handler */
int netw_sigio_handler_socket = -1;

void netw_sigio_handler(int sig)
{
	DUMP_LOG("Signal caught: %d\n", sig);

	if (netw_sigio_handler_socket < 0)
		return;

	/* Disable async */
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

int netw_tcp_set_keepalive(int sock)
{
	int on = 1;
	int idle = 1;
	int inter = 1;
	int maxpkt = 1;

	if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(int)) < 0) {
		perror("Error: setsockopt");
		return -1;
	}

	if (setsockopt(sock, SOL_TCP, TCP_KEEPIDLE, &idle, sizeof(int)) < 0) {
		perror("Error: setsockopt");
		return -1;
	}

	if (setsockopt(sock, SOL_TCP, TCP_KEEPINTVL, &inter, sizeof(int)) < 0) {
		perror("Error: setsockopt");
		return -1;
	}

	if (setsockopt(sock, SOL_TCP, TCP_KEEPCNT, &maxpkt, sizeof(int)) < 0) {
		perror("Error: setsockopt");
		return -1;
	}

	return 0;
}

int netw_udp_brcast_rec_socket(in_port_t port)
{
	int sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock == -1) {
		perror("Error: socket");
		goto handle_err_0;
	}

	int val = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val)) < 0) {
		perror("Error: setsockopt");
		goto handle_err_1;
	}

	val = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
		perror("Error: setsockopt");
		goto handle_err_1;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = port;
	addr.sin_addr.s_addr = INADDR_BROADCAST;

	if (bind(sock, &addr, sizeof(addr))) {
		perror("Error: bind");
		goto handle_err_1;
	}

	return sock;

handle_err_1:
	close(sock);
handle_err_0:
	return -1;
}

int netw_udp_wait_msg(int sock, netw_msg_t msg, struct sockaddr_in *src)
{
	while (1) {
		DUMP_LOG("Waiting for udp_msg\n");
		socklen_t addr_len = sizeof(*src);

		int received;
		if (recvfrom(sock, &received, sizeof(received), 0, src,
			     &addr_len) < 0) {
			perror("Error: recvfrom");
			return -1;
		}

		DUMP_LOG("Received: %d\n", received);
		if (received == msg)
			break;

		DUMP_LOG("Not equal to %d\n", msg);
	}

	return 0;
}

int netw_tcp_connect(struct sockaddr_in *addr, struct timeval *timeout)
{
	int tcp_sock = socket(PF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (tcp_sock == -1) {
		perror("Error: socket");
		goto handle_err_0;
	}

	if (netw_tcp_set_keepalive(tcp_sock) < 0) {
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
			return 1; // Note this
		}
		if (ret < 0) {
			perror("Error: select");
			goto handle_err_1;
		}

		int val;
		socklen_t val_len = sizeof(val);
		ret = getsockopt(tcp_sock, SOL_SOCKET, SO_ERROR, &val,
				 &val_len);
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

int netw_tcp_listen_socket(in_port_t port, int max_listen)
{
	int tcp_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (tcp_sock == -1) {
		perror("Error: socket");
		goto handle_err_0;
	}

	int val = 1;
	if (setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) <
	    0) {
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

int netw_udp_broadcast_msg(in_port_t port, int udp_msg)
{
	int udp_sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (udp_sock == -1) {
		perror("Error: socket");
		goto handle_err_0;
	}

	int val = 1;
	if (setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &val, sizeof(val)) <
	    0) {
		perror("Error: setsockopt");
		goto handle_err_1;
	}

	DUMP_LOG("Broadcasting udp_msg to workers\n");
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = port;
	addr.sin_addr.s_addr = INADDR_BROADCAST;

	if (sendto(udp_sock, &udp_msg, sizeof(udp_msg), 0, &addr,
		   sizeof(addr)) < 0) {
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

int netw_tcp_accept_connections(int tcp_sock, int *sockets, int max_sockets,
				struct timeval *timeout)
{
	fd_set set;
	FD_ZERO(&set);
	FD_SET(tcp_sock, &set);

	int n_sockets = 0;

	for (; n_sockets < max_sockets; n_sockets++) {
		/* Unportable, Linux-only */
		int ret = select(tcp_sock + 1, &set, NULL, NULL, timeout);
		if (ret == 0) {
			DUMP_LOG("Accept timed out, %d connections accepted\n",
				 n_sockets);
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
		sockets[n_sockets] = ret;

		DUMP_LOG("Accepted connection №%d\n", n_sockets + 1);
		DUMP_LOG("\t(%ld usec remaining)\n", timeout->tv_usec);

		if (netw_tcp_set_keepalive(sockets[n_sockets]) < 0) {
			fprintf(stderr, "Error: netw_socket_keepalive");
			goto handle_err;
		}

		if (fcntl(sockets[n_sockets], F_SETOWN, getpid()) < 0) {
			perror("Error: fcntl");
			goto handle_err;
		}
	}

	return n_sockets;

handle_err:
	while (n_sockets--)
		close(sockets[n_sockets]);

	return -1;
}

/********************** Network Worker *************************/

/* Calc speed here is synonym for n_threads, but not in general */
int integrate_network_worker(int calc_speed, cpu_set_t *cpuset, int n_threads)
{
	fprintf(stderr, "-------- Starting worker (%d threads) --------\n",
		calc_speed);

	/* Ignore SIGPIPE */
	struct sigaction act = {};
	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) < 0) {
		perror("Error: sigaction");
		goto handle_err_0;
	}

	/* Prepare SIGIO handler */
	act.sa_handler = netw_sigio_handler;
	if (sigaction(SIGIO, &act, NULL) < 0) {
		perror("Error: sigaction");
		goto handle_err_0;
	}

	/* Prepare UDP socket to receive broadcast */
	int udp_sock = netw_udp_brcast_rec_socket(htons(INTEGRATE_UDP_PORT));
	if (udp_sock < 0) {
		goto handle_err_0;
	}

	int tcp_sock;

	/* Process requests */
	while (1) {
		/* Wait for broadcast */
		struct sockaddr_in starter_addr;
		if (netw_udp_wait_msg(udp_sock, INTEGRATE_UDP_MAGIC,
				      &starter_addr) < 0) {
			goto handle_err_1;
		}

		/* Connect to starter */
		struct timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = INTEGRATE_NETW_TIMEOUT_USEC;
		starter_addr.sin_port = htons(INTEGRATE_TCP_PORT);

		tcp_sock = netw_tcp_connect(&starter_addr, &timeout);
		if (tcp_sock < 0) {
			goto handle_err_1;
		}

		/* Send relative calc speed value (here it's n_threads) */
		DUMP_LOG("n_threads: %d, sending...\n", calc_speed);

		if (netw_tcp_write(tcp_sock, &calc_speed, sizeof(int)) < 0) {
			fprintf(stderr, "Error: write n_threads to starter\n");
			goto handle_err_2;
		}

		/* Receive task */
		struct task_netw task;
		if (netw_tcp_read(tcp_sock, &task, sizeof(task)) < 0) {
			fprintf(stderr, "Error: read task from starter\n");
			goto handle_err_2;
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

		/* Process task */
		DUMP_LOG("task:\n\tfrom = %Lg\n\tto = %Lg\n\tstep = %Lg\n",
			 task.base + task.step_wdth * task.start_step,
			 task.base + task.step_wdth *
					     (task.start_step + task.n_steps),
			 task.step_wdth);
		long double result;

		if (integrate_multicore_scalable(
			    n_threads, cpuset, task.n_steps,
			    task.base + task.step_wdth * task.start_step,
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

int starter_send_tasks(int *sockets, int *speeds, int n_sockets,
		       struct task_netw *full_task)
{
	int sum_speeds = 0;
	for (int i = 0; i < n_sockets; i++)
		sum_speeds += speeds[i];

	struct task_netw task;
	task.base = full_task->base;
	task.step_wdth = full_task->step_wdth;
	size_t cur_step = full_task->start_step;
	size_t n_steps = full_task->n_steps;

	for (; n_sockets != 0; n_sockets--) {
		task.start_step = cur_step;
		task.n_steps = (n_steps * speeds[n_sockets - 1]) / sum_speeds;

		cur_step += task.n_steps;
		n_steps -= task.n_steps;
		sum_speeds -= speeds[n_sockets - 1];

		DUMP_LOG("Sending task to worker[%d]\n", n_sockets - 1);

		ssize_t ret =
			write(sockets[n_sockets - 1], &task, sizeof(task));
		if (ret < 0) {
			if (errno == EPIPE)
				fprintf(stderr,
					"Error: connection to "
					"worker[%d] lost (EPIPE)\n",
					n_sockets - 1);
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

	fd_set set;
	fd_set tmp_set;
	FD_ZERO(&set);
	int max_fd = 0;
	for (int i = 0; i < n_sockets; i++) {
		if (sockets[i] > max_fd)
			max_fd = sockets[i];
		FD_SET(sockets[i], &set);
	}

	long double accum = 0;
	while (n_sockets) {
		tmp_set = set;
		int n_ready = select(max_fd + 1, &tmp_set, NULL, NULL, NULL);
		if (n_ready < 0) {
			perror("Error: select");
			return -1;
		}

		for (int n = 0; n_ready; n++) {
			if (!FD_ISSET(sockets[n], &tmp_set))
				continue;
			long double sum;
			if (netw_tcp_read(sockets[n], &sum, sizeof(sum)) < 0) {
				fprintf(stderr, "Error: connection[%d] lost\n",
					n_sockets - 1);
				return -1;
			}
			DUMP_LOG("worker[%d] sum = %Lg\n", n, sum);
			accum += sum;
			n_ready--;
			n_sockets--;
			FD_CLR(sockets[n], &set);
		}
	}

	*result = accum;
	return 0;
}

int starter_get_workers_speeds(int *sockets, int *speeds, int n_sockets)
{
	DUMP_LOG("Receiving ncpus...\n");
	for (; n_sockets != 0; n_sockets--) {
		if (netw_tcp_read(sockets[n_sockets - 1],
				  &speeds[n_sockets - 1],
				  sizeof(*speeds)) < 0) {
			fprintf(stderr,
				"Error: connection with worker[%d] lost\n",
				n_sockets - 1);
			return -1;
		}

		DUMP_LOG("worker[%d] ncpus = %d\n", n_sockets - 1,
			 speeds[n_sockets - 1]);
	}

	return 0;
}

int integrate_network_starter(size_t n_steps, long double base,
			      long double step, long double *result)
{
	fprintf(stderr, "Starting starter\n");

	/* Set SIGPIPE here */
	struct sigaction act = {};
	act.sa_handler = SIG_IGN;
	if (sigaction(SIGPIPE, &act, NULL) < 0) {
		perror("Error: sigaction");
		goto handle_err_0;
	}

	/* Prepare TCP socket */
	int tcp_sock = netw_tcp_listen_socket(htons(INTEGRATE_TCP_PORT),
					      INTEGRATE_MAX_WORKERS);
	if (tcp_sock < 0) {
		fprintf(stderr, "Error: starter_tcp_listen_socket failed\n");
		goto handle_err_0;
	}

	/* UDP Broadcast */
	if (netw_udp_broadcast_msg(htons(INTEGRATE_UDP_PORT),
				   INTEGRATE_UDP_MAGIC) < 0) {
		fprintf(stderr, "Error: starter_udp_broadcast_msg failed\n");
		goto handle_err_1;
	}

	/* Accept TCP connections */
	int worker_sock[INTEGRATE_MAX_WORKERS];
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = INTEGRATE_NETW_TIMEOUT_USEC;

	int n_workers = netw_tcp_accept_connections(
		tcp_sock, worker_sock, INTEGRATE_MAX_WORKERS, &timeout);
	if (n_workers < 0) {
		fprintf(stderr, "Error: starter_accept_connections failed\n");
		goto handle_err_1;
	}
	if (n_workers == 0) {
		DUMP_LOG("No workers aviable\n");
		goto handle_err_1;
	}

	/* Get relative speeds (number of threads here) */
	int worker_speed[INTEGRATE_MAX_WORKERS];

	if (starter_get_workers_speeds(worker_sock, worker_speed, n_workers) <
	    0) {
		fprintf(stderr, "Error: starter_get_workers_ncpus failed\n");
		goto handle_err_2;
	}

	/* Split task and send requests */
	struct task_netw task;
	task.base = base;
	task.step_wdth = step;
	task.start_step = 0;
	task.n_steps = n_steps;

	if (starter_send_tasks(worker_sock, worker_speed, n_workers, &task) <
	    0) {
		fprintf(stderr, "Error: starter_send_tasks failed\n");
		goto handle_err_2;
	}

	/* Accumulate result */
	if (starter_accumulate_result(worker_sock, n_workers, result) < 0) {
		fprintf(stderr, "Error: starter_accumulate_result failed\n");
		goto handle_err_2;
	}

	/* Close connections */
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
