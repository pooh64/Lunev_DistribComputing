#include "integrate.h"
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <float.h>

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
	get_full_cpuset(&topo, &cpuset);
	DUMP_LOG(dump_cpu_topology(stderr, &topo));
	DUMP_LOG(dump_cpu_set(stderr, &cpuset));

	long double from = 0;
	long double to = 50000;
	long double step = 1 / to;
	long double result;
	size_t n_steps = (to - from) / step;

	if (integrate_multicore_abused(n_threads, &cpuset,
		n_steps, from, step, &result) == -1) {
		perror("Error: integrate");
		exit(EXIT_FAILURE);
	}

	printf("result : %.*Lg\n", LDBL_DIG, result);
	// printf("+1/xmax: %.*Lg\n", LDBL_DIG, result + 1 / to);

	return 0;
}
