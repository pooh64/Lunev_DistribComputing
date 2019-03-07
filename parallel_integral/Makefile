CFLAGS := -c -g -O2 -Wall -std=c99
LDFLAGS := -pthread

BUILD_DIR := build
-include *.d

.DEFAULT_GOAL := integral

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -rf *.o *.log

INTEGRAL_SRC := main.c cpu_topology.c
INTEGRAL_OBJ := $(addprefix $(BUILD_DIR)/,$(INTEGRAL_SRC:.c=.o))

.PHONY: integral
integral: $(BUILD_DIR)/integral
$(BUILD_DIR)/integral: $(INTEGRAL_OBJ)
	$(CC) $(LDFLAGS) $(INTEGRAL_OBJ) -o $@
