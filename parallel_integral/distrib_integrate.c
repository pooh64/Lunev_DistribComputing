#include "integrate.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <float.h>

int process_args(int argc, char *argv[], int *mode)
{
	if (argc != 2) {
		fprintf(stderr, "Error: one arg required\n");
		return -1;
	}
	
	char *endptr;
	errno = 0;
	long tmp = strtol(argv[1], &endptr, 10);
	if (errno || *endptr != '\0' || tmp < 0 || tmp > 1) {
		fprintf(stderr, "Error: wrong mode\n");
		return -1;
	}
	if (tmp == 0)
		DUMP_LOG("argv = worker\n");
	else
		DUMP_LOG("argv = starter\n");
	
	*mode = tmp;
	
	return 0;
}

int main(int argc, char *argv[])
{
	int mode;
	if (process_args(argc, argv, &mode))
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
		
		int ret = integrate_network_worker(&cpuset);
		if (ret == -1) {
			fprintf(stderr, "Error: worker failed\n");
			exit(EXIT_FAILURE);
		} if (ret == 1) {
			// maybe run it again?
			assert(0);
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
