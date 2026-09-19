#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "shell.h"
#include "executor.h"

// ====================================================================
// A very simple testing framework with a few helper macros.
// ====================================================================

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RESET   "\x1b[0m"

static int tests_run = 0;
static int tests_failed = 0;

#define TEST_ASSERT(condition) do { \
    tests_run++; \
    if (!(condition)) { \
        printf(ANSI_COLOR_RED "FAIL: %s:%d - %s\n" ANSI_COLOR_RESET, __FILE__, __LINE__, __func__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_ASSERT_EQUAL_STRING(expected, actual) do { \
    TEST_ASSERT(strcmp(expected, actual) == 0); \
} while(0)

// ====================================================================
// Test cases for the shell's functionality.
// ====================================================================

/**
 * @brief Tests the readline function's ability to handle long input.
 */
void test_long_input_readline() {
    // Note: This is a bit tricky to automate without a mock stdin.
    // For now, we'll assume a basic functionality and would manually test this.
    // In a real scenario, you'd use a mocking library or redirect stdin.
    // For simplicity, we'll just test that it returns a non-null pointer.
    char *input = "hello world\n";
    FILE *mock_stdin = fmemopen(input, strlen(input), "r");
    FILE *original_stdin = stdin;
    stdin = mock_stdin;

    char *line = readline("test>");
    TEST_ASSERT_EQUAL_STRING("hello world", line);

    free(line);
    fclose(mock_stdin);
    stdin = original_stdin; // Restore stdin
}

/**
 * @brief Tests a simple command execution without pipes or redirection.
 */
void test_simple_command_execution() {
    struct cmdline cmd;
    cmd.in = NULL;
    cmd.out = NULL;
    cmd.bg = 0;

    char *args[] = {"echo", "Hello", "World", NULL};
    cmd.seq = (char***)malloc(sizeof(char**) * 2);
    cmd.seq[0] = args;
    cmd.seq[1] = NULL;

    int result = execute(&cmd);
    TEST_ASSERT(result == EXIT_SUCCESS);

    free(cmd.seq);
}

/**
 * @brief Tests command execution with a simple pipe.
 */
void test_pipe_execution() {
    // This requires a way to capture the output of the final command.
    // We'll redirect the output to a file and check the file's contents.

    // Create a temporary file for output redirection.
    char *temp_file = "test_output.txt";

    struct cmdline cmd;
    cmd.in = NULL;
    cmd.out = temp_file;
    cmd.bg = 0;

    char *cmd1_args[] = {"echo", "a\nb\nc\n", NULL};
    char *cmd2_args[] = {"grep", "b", NULL};

    cmd.seq = (char***)malloc(sizeof(char**) * 3);
    cmd.seq[0] = cmd1_args;
    cmd.seq[1] = cmd2_args;
    cmd.seq[2] = NULL;

    int result = execute(&cmd);
    TEST_ASSERT(result == EXIT_SUCCESS);

    // Read the content of the temporary file.
    FILE *f = fopen(temp_file, "r");
    TEST_ASSERT(f != NULL);

    char buffer[256];
    fgets(buffer, sizeof(buffer), f);
    TEST_ASSERT_EQUAL_STRING("b\n", buffer);

    fclose(f);
    remove(temp_file); // Clean up the temporary file
    free(cmd.seq);
}

/**
 * @brief Runs all the tests.
 */
int main() {
    test_long_input_readline();
    test_simple_command_execution();
    test_pipe_execution();

    printf("\nRan %d tests, %d failed.\n", tests_run, tests_failed);
    if (tests_failed == 0) {
        printf(ANSI_COLOR_GREEN "All tests passed!\n" ANSI_COLOR_RESET);
        return 0;
    } else {
        return 1;
    }
}

