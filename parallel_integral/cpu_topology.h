#ifndef CPU_TOPOLOGY_H_
#define CPU_TOPOLOGY_H_

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sched.h>

struct cpu_topology_elem {
	int package_id;
	int core_id;
	int cpu_id;
};

struct cpu_topology {
	struct cpu_topology_elem cpu[CPU_SETSIZE];
	int max_package_id;
	int max_core_id;
	int max_cpu_id;
};

int  get_cpu_topology(struct cpu_topology *topo);
int dump_cpu_topology(FILE *stream, struct cpu_topology *topo);

/* One cpu per one core */
int get_single_cpus_cpu_topology(struct cpu_topology *topo, cpu_set_t *cpuset);

#endif /* CPU_TOPOLOGY_H_
