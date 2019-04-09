#include "integrate.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <float.h>

int main(int argc, char *argv[])
{
	long double from = INTEGRATE_FROM;
	long double to = INTEGRATE_TO;
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

	return 0;
}
