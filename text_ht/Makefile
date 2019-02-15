CFLAGS = -c -g -O0 -Wall -fprofile-arcs -ftest-coverage -MD
LDFLAGS = -fprofile-arcs
BUILD_DIR := build
-include *.d

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -R $(BUILD_DIR)
	rm -rf *.o *.d *.gcov

HASH_UTEST_SRC := hash_table_utest.c hash_table.c
HASH_UTEST_OBJ := $(addprefix $(BUILD_DIR)/,$(HASH_UTEST_SRC:.c=.o))
.PHONY: build_utest
build_utest: $(BUILD_DIR)/hash_utest
$(BUILD_DIR)/hash_utest: $(HASH_UTEST_OBJ)
	$(CC) $(LDFLAGS) $(HASH_UTEST_OBJ) -o $@

.PHONY: test_cov
test_cov: build_utest
	./$(BUILD_DIR)/hash_utest
	gcov $(addprefix $(BUILD_DIR)/,$(HASH_UTEST_SRC))
