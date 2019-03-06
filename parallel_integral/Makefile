CFLAGS := -c -g -O0 -Wall
LDFLAGS := -lpthread

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

INTEGRAL_SRC := main.c
INTEGRAL_OBJ := $(addprefix $(BUILD_DIR)/,$(INTEGRAL_SRC:.c=.o))
.PHONY: integral
integral: $(BUILD_DIR)/integral
$(BUILD_DIR)/integral: $(INTEGRAL_OBJ)
	$(CC) $(LDFLAGS) $(INTEGRAL_OBJ) -o $@
	cp ./test_scaling.sh $(BUILD_DIR)
