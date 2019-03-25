#ifndef INTEGRATE_H_
#define INTEGRATE_H_

#define ENABLE_DUMP_LOG

/* Tired looking for error, 128 is enough here */
#define CACHE_LINE_ALIGN 1024

/* Switching to long double makes 3x faster calculations, but some L1 bounds */
typedef double worker_tmp_t;

/* #define INTEGRATE_FUNC(x) (2 / (x * x + 1)) */
#define INTEGRATE_FUNC(x) (x)

#define INTEGRATE_UDP_PORT 4010
#define INTEGRATE_TCP_PORT 4011


#ifdef ENABLE_DUMP_LOG
#define DUMP_LOG(arg) arg
#else
#define DUMP_LOG(arg)
#endif

#include "cpu_topology.h"
#include <stdio.h>

#define TRACE_LINE (fprintf(stderr, "line: %d\n", __LINE__))

int integrate_multicore(cpu_set_t *cpuset, size_t n_steps,
	long double base, long double step, long double *result);

int integrate_multicore_abused(int n_threads, cpu_set_t *cpuset, size_t n_steps,
	long double base, long double step, long double *result);


int integrate_network_starter(size_t n_steps, long double base,
	long double step, long double *result);

int integrate_network_worker(cpu_set_t *cpuset);

#endif /* INTEGRATE_H_ */
