# Define the compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Iinclude

# Define the output directories
BIN_DIR = bin
OBJ_DIR = obj
TESTS_DIR = tests

# Define the target executable names
TARGET = unix_shell
TEST_TARGET = run_tests

# Find all source files in the src directory
SRCS = $(wildcard src/*.c)
# Exclude main.c from the list of source files for the test suite
TEST_SRCS = $(filter-out src/main.c, $(SRCS))

# Create a list of all object file paths, but place them in the obj directory
OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(SRCS))
# Create a list of object file paths for the test suite
TEST_OBJS = $(patsubst src/%.c, $(OBJ_DIR)/%.o, $(TEST_SRCS))

# Phony targets that don't correspond to a file
.PHONY: all clean test system_test docs

# The default target. It depends on the bin directory and the final executable.
all: $(BIN_DIR) $(BIN_DIR)/$(TARGET)

docs:
	doxygen Doxyfile

# Rule to create the bin and obj directories if they don't exist
$(BIN_DIR):
	mkdir -p $(BIN_DIR)
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Rule to link the object files into the final executable
$(BIN_DIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# Rule to compile each source file into an object file in the obj directory
$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# New rule for the C test suite
$(TESTS_DIR)/$(TEST_TARGET): $(TESTS_DIR)/test_main.c $(TEST_OBJS)
	$(CC) $(CFLAGS) $< $(TEST_OBJS) -o $@

# Rule to compile and run the C test suite
test: $(TESTS_DIR)/$(TEST_TARGET)
	./$(TESTS_DIR)/$(TEST_TARGET)

# Rule to run the system test script
system_test: all
	./$(TESTS_DIR)/run_tests.sh

# Clean target to remove all compiled files and directories
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(TESTS_DIR)/$(TEST_TARGET)
