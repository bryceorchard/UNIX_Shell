#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include "shell.h"
#include "executor.h"
#include "jobs.h"
#include "utils.h"

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
    execute_command("sh", args, STDIN_FILENO, STDOUT_FILENO, 0); // foreground

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
    execute_command("sleep", args, STDIN_FILENO, STDOUT_FILENO, 0); // foreground
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
    execute_command("sleep", args, STDIN_FILENO, STDOUT_FILENO, 1); // background
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
    execute_command("true", args, STDIN_FILENO, STDOUT_FILENO, 1); // background -> should add_job(pid, ...)

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
 * @brief Tests command execution with a simple pipe. Added by Paula Lago
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
 * @brief Runs a command line through the shell's parser and executor and
 *        captures what the commands write to stdout.
 * @param line   Command line to run, e.g. "echo hello | cat".
 * @param buffer Destination for the captured output (always NUL-terminated).
 * @param size   Size of `buffer` in bytes.
 */
static void capture_output(const char *line, char *buffer, size_t size) {
    buffer[0] = '\0';

    // Temp file to receive the output. Forked commands inherit fd 1, so
    // pointing fd 1 at this file captures everything the pipeline prints.
    char path[] = "/tmp/coen346_pipe_XXXXXX";
    int tmp_fd = mkstemp(path);
    if (tmp_fd < 0) return;

    // Save the real stdout, then redirect fd 1 to the temp file.
    fflush(stdout);
    int saved_fd = dup(STDOUT_FILENO);
    dup2(tmp_fd, STDOUT_FILENO);

    // execute() prints "seq[i]: ..." debug lines through the stdout FILE.
    // Point that FILE at /dev/null so only the commands' output is captured.
    FILE *orig = stdout;
    stdout = fopen("/dev/null", "w");

    // parsecmd() frees the line it is given, so pass a heap copy.
    char *copy = strdup(line);
    struct cmdline *l = parsecmd(&copy);
    if (l != NULL && l->err == NULL) {
        execute(l); // foreground: returns once every command has been waited on
    }

    // Restore the real stdout (both the FILE and fd 1).
    fclose(stdout);
    stdout = orig;
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    // Read the captured output back from the start of the file.
    lseek(tmp_fd, 0, SEEK_SET);
    ssize_t n = read(tmp_fd, buffer, size - 1);
    buffer[n > 0 ? n : 0] = '\0';

    close(tmp_fd);
    unlink(path);
}

void test_simple_pipes() {
    struct commands {
        char *line;
        char *expected;
    };

    struct commands test_cases[] = {
        { .line = "echo hello | cat", .expected = "hello\n" },
        { .line = "echo hello | grep h", .expected = "hello\n" },
        { .line = "echo hello world | wc -w", .expected = "2\n" },
        { .line = "echo hello world | wc -c", .expected = "12\n" },
        { .line = "echo hello world | wc -l", .expected = "1\n" },
        { .line = "echo abc | tr a-z A-Z", .expected = "ABC\n" },
        { .line = "echo \"This is a test stream\" | sed 's/stream/text/'", .expected = "This is a test text\n" },
        // contacts.txt: no trailing newline, so wc -l counts 8 lines, not 9
        { .line = "cat resources/contacts.txt | wc -l", .expected = "8\n" },
        { .line = "cat resources/contacts.txt | sort", .expected = "Bob Jones\nColin Doe\nDana David\nJenny Case\nLeonard Schmidt\nLeslie Smith\nLinda Gray\nSusan Gee\nTerry Jones\n" },
        { .line = "cat resources/contacts.txt | sort -k2", .expected = "Jenny Case\nDana David\nColin Doe\nSusan Gee\nLinda Gray\nBob Jones\nTerry Jones\nLeonard Schmidt\nLeslie Smith\n" },
        { .line = "cat resources/contacts.txt | grep Jones", .expected = "Bob Jones\nTerry Jones\n" },
        { .line = "cat resources/contacts.txt | head -n 3", .expected = "Bob Jones\nLeslie Smith\nDana David\n" },
        // last line has no newline, so the output doesn't end with one either
        { .line = "cat resources/contacts.txt | tail -n 2", .expected = "Colin Doe\nJenny Case" },
        // '$2' is in single quotes, so the shell must pass it to awk untouched
        { .line = "cat resources/contacts.txt | awk '{ print $2 }'", .expected = "Jones\nSmith\nDavid\nGee\nSchmidt\nGray\nJones\nDoe\nCase\n" },
        // contents.txt: also no trailing newline
        { .line = "cat resources/contents.txt | wc -l", .expected = "8\n" },
        { .line = "cat resources/contents.txt | grep error", .expected = "This line contains an error.\nHere is another line with an error.\n" },
        { .line = "cat resources/contents.txt | grep -i error", .expected = "This line contains an error.\nThis line contains a case sensitive ERROR\nAnd this one is just an Error.\nHere is another line with an error.\n" },
        { .line = "cat resources/contents.txt | grep -ic warning", .expected = "3\n" },
        // trailing space after "line." must be preserved
        { .line = "cat resources/contents.txt | grep another", .expected = "This is another line. \nHere is another line with an error.\n" },
        // cat fails (error goes to stderr, not captured), wc still prints 0
        { .line = "cat resources/nope.txt | wc -l", .expected = "0\n" }
    };

    // Run every case, even after a failure, and report each result.
    size_t count = sizeof(test_cases) / sizeof(test_cases[0]);
    char output[1024];

    for (size_t i = 0; i < count; i++) {
        tests_run++;
        capture_output(test_cases[i].line, output, sizeof(output));

        if (strcmp(test_cases[i].expected, output + strspn(output, " \t")) == 0) {
            printf(ANSI_COLOR_GREEN "PASS: %s - \"%s\"\n" ANSI_COLOR_RESET,
                   __func__, test_cases[i].line);
        } else {
            tests_failed++;
            printf(ANSI_COLOR_RED "FAIL: %s:%d - %s - \"%s\"\n" ANSI_COLOR_RESET,
                   __FILE__, __LINE__, __func__, test_cases[i].line);
            printf("  expected: \"%s\"\n  actual:   \"%s\"\n",
                   test_cases[i].expected, output);
        }
    }
}

/* ================================================================
 * Process computation time (SIGCHLD -> elapsed-time reporting)
 * ================================================================ */

/* Small helper: capture everything remove_job/list_jobs/the handler print
 * to stdout into `buf`, so we can assert on the completion message. Because
 * the SIGCHLD handler runs on this same thread (it interrupts us during the
 * usleep loop), writing to the redirected stdout from it is not a data race
 * here - it is ordinary same-thread I/O. */

/**
 * @brief Removing a tracked job emits the completion message: its PID, the
 *        "completed in ... seconds" wording, and the blue colour the spec
 *        asks for.
 */
void test_completion_message_format() {
    free_jobs();
    add_job(8080, "sleep 1");

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    FILE *orig = stdout;
    FILE *cap = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT(cap != NULL);
    stdout = cap;
    remove_job(8080);
    fflush(cap);
    stdout = orig;
    fclose(cap);

    TEST_ASSERT(strstr(buf, "8080")         != NULL); // the pid
    TEST_ASSERT(strstr(buf, "completed in") != NULL); // the wording
    TEST_ASSERT(strstr(buf, "seconds")      != NULL); // the unit
    TEST_ASSERT(strstr(buf, "\x1b[34m")     != NULL); // printed in blue

    free_jobs();
}

/**
 * @brief The reported time reflects real elapsed wall-clock time, measured
 *        from add_job's start_time to remove_job: a job left alive ~0.3s
 *        should report at least that.
 */
void test_completion_time_tracks_elapsed() {
    free_jobs();
    add_job(8081, "work");
    usleep(300000); // 0.3 s

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    FILE *orig = stdout;
    FILE *cap = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT(cap != NULL);
    stdout = cap;
    remove_job(8081);
    fflush(cap);
    stdout = orig;
    fclose(cap);

    char *p = strstr(buf, "completed in ");
    TEST_ASSERT(p != NULL);
    double secs = -1.0;
    TEST_ASSERT(sscanf(p, "completed in %lf", &secs) == 1);
    TEST_ASSERT(secs >= 0.25); // at least ~0.3s (generous lower bound)
    TEST_ASSERT(secs <  2.0);  // and not a garbage value

    free_jobs();
}

/**
 * @brief A job removed immediately reports a near-zero time - a sanity check
 *        that start_time is set in add_job rather than left uninitialised.
 */
void test_completion_time_near_zero_when_instant() {
    free_jobs();
    add_job(8082, "quick");

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    FILE *orig = stdout;
    FILE *cap = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT(cap != NULL);
    stdout = cap;
    remove_job(8082);
    fflush(cap);
    stdout = orig;
    fclose(cap);

    char *p = strstr(buf, "completed in ");
    TEST_ASSERT(p != NULL);
    double secs = -1.0;
    TEST_ASSERT(sscanf(p, "completed in %lf", &secs) == 1);
    TEST_ASSERT(secs >= 0.0);
    TEST_ASSERT(secs <  0.2); // effectively instant

    free_jobs();
}

/**
 * @brief End-to-end: with the real SIGCHLD handler installed, a background
 *        job that finishes is reaped by the handler, which removes it from
 *        the table and prints its computation time - the main code never
 *        calls wait() itself.
 */
void test_sigchld_handler_reports_background_completion() {
    free_jobs();

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGCHLD);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    TEST_ASSERT(sigaction(SIGCHLD, &sa, NULL) == 0);

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    FILE *orig = stdout;
    FILE *cap = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT(cap != NULL);
    stdout = cap;

    char *args[] = {"true", NULL};
    execute_command("true", args, STDIN_FILENO, STDOUT_FILENO, 1); // bg: add_job(pid); child exits at once

    // Let the child die and the handler run. Do NOT wait() here - that would
    // steal the child from the handler we are testing.
    for (int i = 0; i < 100; i++) usleep(10000); // up to ~1 s

    fflush(cap);
    stdout = orig;
    fclose(cap);

    // The handler printed the completion message...
    TEST_ASSERT(strstr(buf, "completed in") != NULL);

    // ...and cleared the slot: the table is empty, so MAX_JOBS adds all fit.
    int added = 0;
    for (int i = 0; i < MAX_JOBS; i++)
        if (add_job(9000 + i, "probe") == 0) added++;
    TEST_ASSERT(added == MAX_JOBS); // 0 slots were still occupied

    free_jobs();
    signal(SIGCHLD, SIG_DFL); // restore default for later tests
}

/**
 * @brief The handler drains ALL finished children on a single delivery:
 *        SIGCHLD is not queued, so several jobs ending at once must still be
 *        reaped via the waitpid(-1, WNOHANG) loop. Launch three, confirm
 *        none linger in the table.
 */
void test_sigchld_handler_reaps_multiple() {
    free_jobs();

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGCHLD);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    TEST_ASSERT(sigaction(SIGCHLD, &sa, NULL) == 0);

    // Redirect stdout to swallow the completion messages (the handler still
    // runs; we just do not want its output interleaved with the test log).
    char buf[2048];
    memset(buf, 0, sizeof(buf));
    FILE *orig = stdout;
    FILE *cap = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT(cap != NULL);
    stdout = cap;

    char *args[] = {"true", NULL};
    execute_command("true", args, STDIN_FILENO, STDOUT_FILENO, 1);
    execute_command("true", args, STDIN_FILENO, STDOUT_FILENO, 1);
    execute_command("true", args, STDIN_FILENO, STDOUT_FILENO, 1);

    for (int i = 0; i < 100; i++) usleep(10000); // up to ~1 s

    fflush(cap);
    stdout = orig;
    fclose(cap);

    int added = 0;
    for (int i = 0; i < MAX_JOBS; i++)
        if (add_job(9100 + i, "probe") == 0) added++;
    TEST_ASSERT(added == MAX_JOBS); // all three reaped => table empty

    free_jobs();
    signal(SIGCHLD, SIG_DFL);
}

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

    // process computation time (SIGCHLD elapsed-time reporting)
    test_completion_message_format();
    test_completion_time_tracks_elapsed();
    test_completion_time_near_zero_when_instant();
    test_sigchld_handler_reports_background_completion();
    test_sigchld_handler_reaps_multiple();

    // simple pipes (x | y)
    test_simple_pipes();

    // Added by Paula Lago
    test_pipe_execution();

    printf("\nRan %d tests, %d failed.\n", tests_run, tests_failed);
    if (tests_failed == 0) {
        printf(ANSI_COLOR_GREEN "All tests passed!\n" ANSI_COLOR_RESET);
        return 0;
    } else {
        return 1;
    }
}
