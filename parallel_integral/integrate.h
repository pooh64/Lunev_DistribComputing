#ifndef INTEGRATE_H_
#define INTEGRATE_H_

/* Tired looking for error, 128 is enough here */
#define CACHE_LINE_ALIGN 1024
typedef double worker_tmp_t;

#define INTEGRATE_FUNC(x) (2 / ((x) * (x) + 1))
// #define INTEGRATE_FUNC(x) ((x) * (x))
#define INTEGRATE_FROM 0.
#define INTEGRATE_TO   20000.
#define INTEGRATE_STEP 1 / (INTEGRATE_TO - INTEGRATE_FROM)

#define INTEGRATE_UDP_PORT  4020
#define INTEGRATE_TCP_PORT  4021
#define INTEGRATE_UDP_MAGIC 0xdead
#define INTEGRATE_MAX_WORKERS 255

#define ENABLE_DUMP_LOG

#ifdef  ENABLE_DUMP_LOG
#define DUMP_LOG(...) (fprintf(stderr, __VA_ARGS__))
#define DUMP_LOG_DO(arg) arg
#else
#define DUMP_LOG(...)
#define DUMP_LOG_DO(arg)
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
