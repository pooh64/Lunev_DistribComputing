#ifndef INTEGRATE_H_
#define INTEGRATE_H_

#define ENABLE_DUMP_LOG

#ifdef ENABLE_DUMP_LOG
#define DUMP_LOG(arg) arg
#else
#define DUMP_LOG(arg)
#endif

#include "cpu_topology.h"
#include <stdio.h>

#define TRACE_LINE (fprintf(stderr, "line: %d\n", __LINE__))

#define CACHE_LINE_ALIGN 128

int integrate_multicore(cpu_set_t *cpuset, size_t n_steps,
	long double base, long double step, long double *result);

int integrate_multicore_abused(int n_threads, cpu_set_t *cpuset, size_t n_steps,
	long double base, long double step, long double *result);


int integrate_network_starter(size_t n_steps, long double base,
	long double step, long double *result);

int integrate_network_worker(cpu_set_t *cpuset);

#endif /* INTEGRATE_H_ */
