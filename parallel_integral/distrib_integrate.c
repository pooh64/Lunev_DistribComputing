#include "integrate.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

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
		fprintf(stderr, "Starting as worker\n");
	else
		fprintf(stderr, "Starting as starter\n");
	
	*mode = tmp;
	
	return 0;
}

int main(int argc, char *argv[])
{
	int mode;
	if (!process_args(argc, argv, &mode))
		exit(EXIT_FAILURE);
	
	if (mode == 0)
		integrate_network_worker(NULL);
	else
		integrate_network_starter(0, 0, 0, NULL);
		
	return 0;
}