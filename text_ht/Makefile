CFLAGS := -c -g -O0 -Wall -fprofile-arcs -ftest-coverage -MD
LDFLAGS := -fprofile-arcs
LDFLAGS_FAULT_INJECTOR := -Wl,-wrap,malloc,-wrap,calloc

BUILD_DIR := build
-include *.d

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -rf *.o *.d *.gcov

HASH_UTEST_SRC := hash_table_utest.c hash_table.c
HASH_UTEST_OBJ := $(addprefix $(BUILD_DIR)/,$(HASH_UTEST_SRC:.c=.o))
.PHONY: build_utest
build_utest: $(BUILD_DIR)/hash_utest
$(BUILD_DIR)/hash_utest: $(HASH_UTEST_OBJ)
	$(CC) $(LDFLAGS) $(HASH_UTEST_OBJ) -o $@

FAULT_TEST_SRC := hash_fault_test.c hash_table.c fault_injector.c
FAULT_TEST_OBJ := $(addprefix $(BUILD_DIR)/,$(FAULT_TEST_SRC:.c=.o))
.PHONY: build_fault_test
build_fault_test: $(BUILD_DIR)/fault_test
$(BUILD_DIR)/fault_test: $(FAULT_TEST_OBJ)
	$(CC) $(LDFLAGS) $(LDFLAGS_FAULT_INJECTOR) $(FAULT_TEST_OBJ) -o $@

.PHONY: run_tests
run_tests: build_utest build_fault_test
	valgrind ./$(BUILD_DIR)/hash_utest
	valgrind ./$(BUILD_DIR)/fault_test
	gcov $(BUILD_DIR)/hash_table.c
