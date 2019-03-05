CFLAGS := -c -g -Wall

.DEFAULT_GOAL := integral

integral: main.c
	$(CC) -lpthread main.c -o $@

.PHONY: test_scaling
test_scaling: integral
	./test_scaling.sh	
