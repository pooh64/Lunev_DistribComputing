#include "integrate.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <float.h>

int process_args(int argc, char *argv[], int *mode, int *n_threads)
{
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Error: wrong argv\n");
		return -1;
	}
	
	char *endptr;
	errno = 0;
	long tmp = strtol(argv[1], &endptr, 10);
	if (errno || *endptr != '\0' || tmp < 0 || tmp > 1) {
		fprintf(stderr, "Error: wrong mode\n");
		return -1;
	}
	*mode = tmp;
	
	if (tmp == 1) {
		DUMP_LOG("argv = starter\n");
		return 0;
	}
	
	DUMP_LOG("argv = worker\n");
	errno = 0;
	tmp = strtol(argv[2], &endptr, 10);
	if (errno || *endptr != '\0' || tmp < 1) {
		fprintf(stderr, "Error: wrong n_threads\n");
		return -1;
	}
	
	*n_threads = tmp;
	
	return 0;
}

int main(int argc, char *argv[])
{
	int mode, n_threads;
	if (process_args(argc, argv, &mode, &n_threads))
		exit(EXIT_FAILURE);
	
	if (mode == 0) {
		/* Prepare usable cpuset */
	
		struct cpu_topology topo;
		cpu_set_t cpuset;
		if (get_cpu_topology(&topo)) {
			fprintf(stderr, "Error: get_cpu_topology\n");
			exit(EXIT_FAILURE);
		}
		get_full_cpuset(&topo, &cpuset);
		DUMP_LOG_DO(dump_cpu_topology(stderr, &topo));
		DUMP_LOG_DO(dump_cpu_set(stderr, &cpuset));	
		
		while (1) {
			int ret = integrate_network_worker(n_threads, &cpuset);
			if (ret == -1)
				fprintf(stderr, "Error: worker failed, restarting...\n");
		}
	}
	else {
		long double from = INTEGRATE_FROM;
		long double to   = INTEGRATE_TO;
		long double step = INTEGRATE_STEP;
		long double result;
		size_t n_steps = (to - from) / step;
		
		int ret = integrate_network_starter(n_steps, from, step, &result);
		if (ret == -1) {
			fprintf(stderr, "Error: starter failed\n");
			exit(EXIT_FAILURE);
		}
		
		printf("result: %.*Lg\n", LDBL_DIG, result);
		printf("+1/to : %.*Lg\n", LDBL_DIG, result + 1 / to);
	}
		
	return 0;
}
