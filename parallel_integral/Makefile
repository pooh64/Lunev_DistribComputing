CFLAGS := -c -g -O0 -Wall -std=c99 -MD
LDFLAGS := -pthread

BUILD_DIR := build

all: multicore_integrate netw_starter netw_worker

-include $(BUILD_DIR)/*.d

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

MULTICORE_INTEGRATE_SRC := multicore_integrate.c integrate.c cpu_topology.c signal_except.c
MULTICORE_INTEGRATE_OBJ := $(addprefix $(BUILD_DIR)/,$(MULTICORE_INTEGRATE_SRC:.c=.o))

.PHONY: multicore_integrate
multicore_integrate: $(BUILD_DIR)/multicore_integrate
$(BUILD_DIR)/multicore_integrate: $(MULTICORE_INTEGRATE_OBJ)
	$(CC) $(LDFLAGS) $(MULTICORE_INTEGRATE_OBJ) -o $@


NETW_STARTER_SRC := netw_starter.c netw_integrate.c integrate.c cpu_topology.c signal_except.c
NETW_STARTER_OBJ := $(addprefix $(BUILD_DIR)/,$(NETW_STARTER_SRC:.c=.o))

.PHONY: netw_starter
netw_starter: $(BUILD_DIR)/netw_starter
$(BUILD_DIR)/netw_starter: $(NETW_STARTER_OBJ)
	$(CC) $(LDFLAGS) $(NETW_STARTER_OBJ) -o $@


NETW_WORKER_SRC := netw_worker.c netw_integrate.c integrate.c cpu_topology.c signal_except.c
NETW_WORKER_OBJ := $(addprefix $(BUILD_DIR)/,$(NETW_WORKER_SRC:.c=.o))

.PHONY: netw_worker
netw_worker: $(BUILD_DIR)/netw_worker
$(BUILD_DIR)/netw_worker: $(NETW_WORKER_OBJ)
	$(CC) $(LDFLAGS) $(NETW_WORKER_OBJ) -o $@
