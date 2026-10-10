#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include "shell.h"
#include "executor.h"
#include "jobs.h"

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

// Small helper: seconds elapsed since `start`.
static double elapsed_since(struct timeval start) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1e6;
}

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
 * @brief The parser should set bg=1 when a command ends with '&', and bg=0
 *        otherwise, without reporting an error for valid lines.
 */
void test_parser_background_flag() {
    // parsecmd() frees the line it is given, so hand it heap-allocated copies.
    char *bg_line = strdup("sleep 1 &");
    struct cmdline *l = parsecmd(&bg_line);
    TEST_ASSERT(l != NULL);
    TEST_ASSERT(l->err == NULL);
    TEST_ASSERT(l->bg == 1);

    char *fg_line = strdup("sleep 1");
    l = parsecmd(&fg_line);
    TEST_ASSERT(l != NULL);
    TEST_ASSERT(l->err == NULL);
    TEST_ASSERT(l->bg == 0);
}

/**
 * @brief An '&' with no command before it (or with a command after it) is a
 *        syntax error, so the parser should populate err.
 */
void test_parser_misplaced_ampersand() {
    char *lone = strdup("&");
    struct cmdline *l = parsecmd(&lone);
    TEST_ASSERT(l != NULL);
    TEST_ASSERT(l->err != NULL);

    char *mid = strdup("echo & hi");
    l = parsecmd(&mid);
    TEST_ASSERT(l != NULL);
    TEST_ASSERT(l->err != NULL);
}

/**
 * @brief A foreground command should actually run the program: after
 *        execute_command returns, the side effect (a created file) is visible.
 *        This only holds if the parent waited for the child to finish.
 */
void test_foreground_runs_command() {
    const char *marker = "/tmp/coen346_fg_marker";
    unlink(marker); // start clean

    char *args[] = {"sh", "-c", "echo ok > /tmp/coen346_fg_marker", NULL};
    execute_command("sh", args, 0); // foreground

    TEST_ASSERT(access(marker, F_OK) == 0);
    unlink(marker);
}

/**
 * @brief A foreground command must BLOCK until the child finishes. Running
 *        `sleep 1` in the foreground should take roughly a full second.
 */
void test_foreground_blocks() {
    char *args[] = {"sleep", "1", NULL};

    struct timeval start;
    gettimeofday(&start, NULL);
    execute_command("sleep", args, 0); // foreground
    double dt = elapsed_since(start);

    TEST_ASSERT(dt >= 0.9); // we waited out the sleep
}

/**
 * @brief A background command must NOT block. Launching `sleep 1` with bg=1
 *        should return almost immediately, well before the sleep completes.
 */
void test_background_execution() {
    char *args[] = {"sleep", "1", NULL};

    struct timeval start;
    gettimeofday(&start, NULL);
    execute_command("sleep", args, 1); // background
    double dt = elapsed_since(start);

    TEST_ASSERT(dt < 0.5); // returned without waiting for the sleep

    // Tidy up the child we just spawned so it does not linger as a zombie.
    waitpid(-1, NULL, 0);
}

/* ================================================================
 * Listing background processes (the `jobs` builtin)
 * ================================================================ */

/**
 * @brief add_job reports success (0) while a free slot exists.
 */
void test_job_add_returns_success() {
    free_jobs(); // clean slate
    TEST_ASSERT(add_job(1001, "sleep 100") == 0);
    TEST_ASSERT(add_job(1002, "sleep 200") == 0);
    free_jobs();
}

/**
 * @brief Removing a tracked pid succeeds once; removing it again, or
 *        removing a pid that was never added, fails (-1).
 */
void test_job_remove_semantics() {
    free_jobs();
    add_job(4242, "sleep 100");

    TEST_ASSERT(remove_job(4242) == 0);   // present -> removed
    TEST_ASSERT(remove_job(4242) == -1);  // already gone
    TEST_ASSERT(remove_job(9999) == -1);  // never tracked
    free_jobs();
}

/**
 * @brief The table holds at most MAX_JOBS jobs; the first overflow add is
 *        rejected with -1.
 */
void test_job_table_capacity() {
    free_jobs();
    for (int i = 0; i < MAX_JOBS; i++) {
        TEST_ASSERT(add_job(2000 + i, "bg") == 0);
    }
    TEST_ASSERT(add_job(3000, "overflow") == -1); // table full
    free_jobs();
}

/**
 * @brief A removed job frees its slot for reuse: fill the table, remove
 *        one entry, and a new add succeeds (the hole is reused - no shift,
 *        no renumber).
 */
void test_job_slot_reuse_after_remove() {
    free_jobs();
    for (int i = 0; i < MAX_JOBS; i++) {
        add_job(100 + i, "bg");
    }
    TEST_ASSERT(add_job(777, "no room") == -1); // full
    TEST_ASSERT(remove_job(105) == 0);          // open one slot
    TEST_ASSERT(add_job(777, "reused") == 0);   // slot reclaimed
    free_jobs();
}

/**
 * @brief list_jobs() prints each active job's command and pid, and omits
 *        jobs that have been removed. Captures stdout to inspect output.
 */
void test_jobs_listing_output() {
    free_jobs();
    add_job(5151, "sleep 5");
    add_job(6262, "find /");
    remove_job(5151); // should not appear in the listing

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    FILE *orig = stdout;
    FILE *cap = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT(cap != NULL);
    stdout = cap;
    list_jobs();
    fflush(cap);
    stdout = orig;
    fclose(cap);

    TEST_ASSERT(strstr(buf, "find /") != NULL);  // active job shown
    TEST_ASSERT(strstr(buf, "6262")   != NULL);  // ... with its pid
    TEST_ASSERT(strstr(buf, "5151")   == NULL);  // removed job absent

    free_jobs();
}

/**
 * @brief End-to-end: launching a command with bg=1 registers it in the
 *        job table under the child's pid. Reap the child to learn its pid,
 *        then confirm that pid was tracked (remove_job succeeds once).
 */
void test_background_job_is_registered() {
    free_jobs();

    char *args[] = {"true", NULL};
    execute_command("true", args, 1); // background -> should add_job(pid, ...)

    pid_t reaped = waitpid(-1, NULL, 0); // reap the child, obtain its pid
    TEST_ASSERT(reaped > 0);
    TEST_ASSERT(remove_job(reaped) == 0);   // it was in the table
    TEST_ASSERT(remove_job(reaped) == -1);  // and only once

    free_jobs();
}

/* ================================================================
 * Simple pipe and multi-pipe excecution
 * ================================================================ */

/**
 * @brief Tests command execution with a simple pipe.
 */
// void test_pipe_execution() {
//     // This requires a way to capture the output of the final command.
//     // We'll redirect the output to a file and check the file's contents.

//     // Create a temporary file for output redirection.
//     char *temp_file = "test_output.txt";

//     struct cmdline cmd;
//     cmd.in = NULL;
//     cmd.out = temp_file;
//     cmd.bg = 0;

//     char *cmd1_args[] = {"echo", "a\nb\nc\n", NULL};
//     char *cmd2_args[] = {"grep", "b", NULL};

//     cmd.seq = (char***)malloc(sizeof(char**) * 3);
//     cmd.seq[0] = cmd1_args;
//     cmd.seq[1] = cmd2_args;
//     cmd.seq[2] = NULL;

//     int result = execute(&cmd);
//     TEST_ASSERT(result == EXIT_SUCCESS);

//     // Read the content of the temporary file.
//     FILE *f = fopen(temp_file, "r");
//     TEST_ASSERT(f != NULL);

//     char buffer[256];
//     fgets(buffer, sizeof(buffer), f);
//     TEST_ASSERT_EQUAL_STRING("b\n", buffer);

//     fclose(f);
//     remove(temp_file); // Clean up the temporary file
//     free(cmd.seq);
// }

/**
 * @brief Runs all the tests.
 */
int main() {
    // other tests
    test_long_input_readline();

    // simple command and background execution
    test_simple_command_execution();
    test_parser_background_flag();
    test_parser_misplaced_ampersand();
    test_foreground_runs_command();
    test_foreground_blocks();
    test_background_execution();

    // listing background processes (jobs)
    test_job_add_returns_success();
    test_job_remove_semantics();
    test_job_table_capacity();
    test_job_slot_reuse_after_remove();
    test_jobs_listing_output();
    test_background_job_is_registered();

    // test_pipe_execution();

    printf("\nRan %d tests, %d failed.\n", tests_run, tests_failed);
    if (tests_failed == 0) {
        printf(ANSI_COLOR_GREEN "All tests passed!\n" ANSI_COLOR_RESET);
        return 0;
    } else {
        return 1;
    }
}
