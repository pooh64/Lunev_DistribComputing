#include "integrate.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <float.h>

int process_args(int argc, char *argv[], int *n_threads)
{
	if (argc != 2) {
		fprintf(stderr, "Error: only n_threads required\n");
		return -1;
	}

	char *endptr;
	errno = 0;
	long tmp = strtol(argv[1], &endptr, 10);
	if (errno || *endptr != '\0' || tmp < 1) {
		fprintf(stderr, "Error: wrong n_threads\n");
		return -1;
	}
	*n_threads = tmp;

	return 0;
}

int main(int argc, char *argv[])
{
	int n_threads;
	if (process_args(argc, argv, &n_threads))
		exit(EXIT_FAILURE);

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
		if (integrate_network_worker(n_threads, &cpuset, n_threads) < 0)
			fprintf(stderr,
				"Error: worker failed, restarting...\n");
	}

	return 0;
}
