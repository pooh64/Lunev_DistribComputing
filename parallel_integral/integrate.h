#ifndef INTEGRATE_H_
#define INTEGRATE_H_

/* Calculating details */
#define CACHE_LINE_ALIGN 256
typedef double worker_tmp_t;

/* Function to integrate */
#define INTEGRATE_FUNC(x) (2 / ((x) * (x) + 1))
#define INTEGRATE_FROM 0.
#define INTEGRATE_TO 50000.
#define INTEGRATE_STEP 1 / (INTEGRATE_TO - INTEGRATE_FROM)

/* Network */
#define INTEGRATE_UDP_PORT 4020
#define INTEGRATE_TCP_PORT 4021
#define INTEGRATE_NETW_TIMEOUT_USEC 1000 * 1000
#define INTEGRATE_UDP_MAGIC 0xdead
#define INTEGRATE_MAX_WORKERS 255

/* Main log (stderr) */
#define ENABLE_DUMP_LOG

#ifdef ENABLE_DUMP_LOG
#define DUMP_LOG(...) (fprintf(stderr, "DUMP_LOG: " __VA_ARGS__))
#define DUMP_LOG_DO(arg) arg
#else
#define DUMP_LOG(...)
#define DUMP_LOG_DO(arg)
#endif

#define TRACE_LINE (fprintf(stderr, "TRACE_LINE: %d\n", __LINE__))

#include "cpu_topology.h"
#include <stdio.h>

/* Uses full cpuset */
int integrate_multicore(cpu_set_t *cpuset, size_t n_steps, long double base,
			long double step, long double *result);

/* Uses full cpuset with thrash-threads to get const cpufreq */
int integrate_multicore_scalable(int n_threads, cpu_set_t *cpuset,
				 size_t n_steps, long double base,
				 long double step, long double *result);

int integrate_network_starter(size_t n_steps, long double base,
			      long double step, long double *result);

int integrate_network_worker(int calc_speed, cpu_set_t *cpuset, int n_threads);

#endif /* INTEGRATE_H_ */
