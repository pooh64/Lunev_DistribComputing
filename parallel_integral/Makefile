CFLAGS := -c -g -O2 -Wall -std=c99 -MD
LDFLAGS := -pthread

BUILD_DIR := build
-include $(BUILD_DIR)/*.d

all: multicore_integrate distrib_integrate

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

MULTICORE_INTEGRATE_SRC := multicore_integrate.c integrate.c cpu_topology.c
MULTICORE_INTEGRATE_OBJ := $(addprefix $(BUILD_DIR)/,$(MULTICORE_INTEGRATE_SRC:.c=.o))

.PHONY: multicore_integrate
multicore_integrate: $(BUILD_DIR)/multicore_integrate
$(BUILD_DIR)/multicore_integrate: $(MULTICORE_INTEGRATE_OBJ)
	$(CC) $(LDFLAGS) $(MULTICORE_INTEGRATE_OBJ) -o $@


DISTRIB_INTEGRATE_SRC := distrib_integrate.c integrate.c cpu_topology.c
DISTRIB_INTEGRATE_OBJ := $(addprefix $(BUILD_DIR)/,$(DISTRIB_INTEGRATE_SRC:.c=.o))

.PHONY: distrib_integrate
distrib_integrate: $(BUILD_DIR)/distrib_integrate
$(BUILD_DIR)/distrib_integrate: $(DISTRIB_INTEGRATE_OBJ)
	$(CC) $(LDFLAGS) $(DISTRIB_INTEGRATE_OBJ) -o $@
