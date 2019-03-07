#ifndef CPU_TOPOLOGY_H_
#define CPU_TOPOLOGY_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>
#include <stdio.h>

struct cpu_topology_elem {
	int package_id;
	int core_id;
	int cpu_id;
};

struct cpu_topology {
	struct cpu_topology_elem cpu[CPU_SETSIZE - 1];
	int max_package_id;
	int max_core_id;
	int max_cpu_id;
};

int  get_cpu_topology(struct cpu_topology *topo);
int dump_cpu_topology(FILE *stream, struct cpu_topology *topo);

int one_cpu_per_core_cpu_topology(struct cpu_topology *topo, cpu_set_t *cpuset);

int dump_cpu_set(FILE *stream, cpu_set_t *cpuset);
int cpu_set_search_next(int cpu, cpu_set_t *set);

#endif /* CPU_TOPOLOGY_H_ */
