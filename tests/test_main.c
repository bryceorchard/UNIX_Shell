#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
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

// On failure, prints the location and the condition that failed, e.g.
//   FAIL: tests/test_main.c:152 - test_foreground_blocks - dt >= 0.9
#define TEST_ASSERT(condition) do { \
    tests_run++; \
    if (!(condition)) { \
        printf(ANSI_COLOR_RED "FAIL: %s:%d - %s - %s\n" ANSI_COLOR_RESET, \
               __FILE__, __LINE__, __func__, #condition); \
        tests_failed++; \
        return; \
    } \
} while(0)

// On failure, also prints both strings so the difference is visible.
#define TEST_ASSERT_EQUAL_STRING(expected, actual) do { \
    const char *exp_ = (expected); \
    const char *act_ = (actual); \
    tests_run++; \
    if (act_ == NULL || strcmp(exp_, act_) != 0) { \
        printf(ANSI_COLOR_RED "FAIL: %s:%d - %s - %s == %s\n" ANSI_COLOR_RESET, \
               __FILE__, __LINE__, __func__, #expected, #actual); \
        printf("  expected: \"%s\"\n  actual:   \"%s\"\n", \
               exp_, act_ != NULL ? act_ : "(null)"); \
        tests_failed++; \
        return; \
    } \
} while(0)

// Small helper: seconds elapsed since `start`.
/**
 * @brief Records a single-process job, as the job tests need.
 * @param pid Process id to record.
 * @param cmd Command line shown by `jobs`.
 * @return add_job()'s result: 0 on success, -1 if the table is full.
 */
static int add_test_job(pid_t pid, const char *cmd) {
    return add_job(&pid, 1, cmd);
}

/**
 * @brief Runs a command line through the shell's parser and executor,
 *        without capturing anything.
 * @param line Command line to run, e.g. "sleep 1 | sleep 1 &".
 */
static void run_line(const char *line) {
    char *copy = strdup(line); // parsecmd() frees the line it is given
    struct cmdline *l = parsecmd(&copy);
    if (l != NULL && l->err == NULL) execute(l);
}

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
    execute_command("sh", args, STDIN_FILENO, STDOUT_FILENO, 0, 1); // foreground, wait

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
    execute_command("sleep", args, STDIN_FILENO, STDOUT_FILENO, 0, 1); // foreground, wait
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
    execute_command("sleep", args, STDIN_FILENO, STDOUT_FILENO, 1, 0); // background
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
    init_jobs(); // clean slate
    TEST_ASSERT(add_test_job(1001, "sleep 100") == 0);
    TEST_ASSERT(add_test_job(1002, "sleep 200") == 0);
    init_jobs();
}

/**
 * @brief Removing a tracked pid succeeds once; removing it again, or
 *        removing a pid that was never added, fails (-1).
 */
void test_job_remove_semantics() {
    init_jobs();
    add_test_job(4242, "sleep 100");

    TEST_ASSERT(remove_job(4242) == 0);   // present -> removed
    TEST_ASSERT(remove_job(4242) == -1);  // already gone
    TEST_ASSERT(remove_job(9999) == -1);  // never tracked
    init_jobs();
}

/**
 * @brief The table holds at most MAX_JOBS jobs; the first overflow add is
 *        rejected with -1.
 */
void test_job_table_capacity() {
    init_jobs();
    for (int i = 0; i < MAX_JOBS; i++) {
        TEST_ASSERT(add_test_job(2000 + i, "bg") == 0);
    }
    TEST_ASSERT(add_test_job(3000, "overflow") == -1); // table full
    init_jobs();
}

/**
 * @brief A removed job frees its slot for reuse: fill the table, remove
 *        one entry, and a new add succeeds (the hole is reused - no shift,
 *        no renumber).
 */
void test_job_slot_reuse_after_remove() {
    init_jobs();
    for (int i = 0; i < MAX_JOBS; i++) {
        add_test_job(100 + i, "bg");
    }
    TEST_ASSERT(add_test_job(777, "no room") == -1); // full
    TEST_ASSERT(remove_job(105) == 0);          // open one slot
    TEST_ASSERT(add_test_job(777, "reused") == 0);   // slot reclaimed
    init_jobs();
}

/**
 * @brief list_jobs() prints each active job's command and pid, and omits
 *        jobs that have been removed. Captures stdout to inspect output.
 */
void test_jobs_listing_output() {
    init_jobs();
    add_test_job(5151, "sleep 5");
    add_test_job(6262, "find /");
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

    init_jobs();
}

/**
 * @brief End-to-end: running a command line ending in '&' registers it in
 *        the job table under the child's pid. Reap the child to learn its pid,
 *        then confirm that pid was tracked (remove_job succeeds once).
 */
void test_background_job_is_registered() {
    init_jobs();

    run_line("true &"); // background -> execute() should add_job(...)

    pid_t reaped = waitpid(-1, NULL, 0); // reap the child, obtain its pid
    TEST_ASSERT(reaped > 0);
    TEST_ASSERT(remove_job(reaped) == 0);   // it was in the table
    TEST_ASSERT(remove_job(reaped) == -1);  // and only once

    init_jobs();
}

/* ================================================================
 * Simple pipe and multi-pipe excecution
 * ================================================================ */

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

    // execute_command() prints "child background process pid" lines through
    // the stdout FILE. Point that FILE at /dev/null so only the commands'
    // output is captured.
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

/**
 * @brief Tests command execution with a simple pipe. Added by Paula Lago,
 *        adapted to use capture_output() instead of redirecting to a file.
 */
void test_pipe_execution() {
    char output[256];

    // printf expands the \n escapes itself, so grep receives three lines.
    capture_output("printf 'a\\nb\\nc\\n' | grep b", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("b\n", output);
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

    size_t count = sizeof(test_cases) / sizeof(test_cases[0]);
    char output[1024];

    for (size_t i = 0; i < count; i++) {
        capture_output(test_cases[i].line, output, sizeof(output));
        // skip leading padding (e.g. macOS wc right-aligns its counts)
        TEST_ASSERT_EQUAL_STRING(test_cases[i].expected, output + strspn(output, " \t"));
    }
}

/**
 * @brief Pipelines with three or more commands. Each stage's output must
 *        flow through every pipe in order.
 */
void test_multiple_pipes() {
    struct commands {
        char *line;
        char *expected;
    };

    struct commands test_cases[] = {
        { .line = "echo a | cat | cat | cat", .expected = "a\n" },
        { .line = "echo hello world | tr a-z A-Z | cat", .expected = "HELLO WORLD\n" },
        { .line = "cat resources/contacts.txt | grep Jones | wc -l", .expected = "2\n" },
        { .line = "cat resources/contacts.txt | sort | uniq", .expected = "Bob Jones\nColin Doe\nDana David\nJenny Case\nLeonard Schmidt\nLeslie Smith\nLinda Gray\nSusan Gee\nTerry Jones\n" },
        { .line = "cat resources/contacts.txt | sort | head -n 3", .expected = "Bob Jones\nColin Doe\nDana David\n" },
        // "Jones" appears twice, uniq keeps one
        { .line = "cat resources/contacts.txt | awk '{ print $2 }' | sort | uniq", .expected = "Case\nDavid\nDoe\nGee\nGray\nJones\nSchmidt\nSmith\n" },
        { .line = "cat resources/contacts.txt | sort -k2 | head -n 1 | tr a-z A-Z", .expected = "JENNY CASE\n" },
        { .line = "cat resources/contents.txt | grep -i error | wc -l", .expected = "4\n" }
    };

    size_t count = sizeof(test_cases) / sizeof(test_cases[0]);
    char output[1024];

    for (size_t i = 0; i < count; i++) {
        capture_output(test_cases[i].line, output, sizeof(output));
        // skip leading padding (e.g. macOS wc right-aligns its counts)
        TEST_ASSERT_EQUAL_STRING(test_cases[i].expected, output + strspn(output, " \t"));
    }
}

/**
 * @brief Input (<) and output (>) redirection, alone and combined with pipes.
 *        For '>', nothing should reach stdout; the file is read back with cat.
 */
void test_redirection() {
    struct commands {
        char *line;
        char *expected;
    };

    // '<' only: output still goes to stdout
    struct commands in_cases[] = {
        { .line = "sort < resources/contacts.txt", .expected = "Bob Jones\nColin Doe\nDana David\nJenny Case\nLeonard Schmidt\nLeslie Smith\nLinda Gray\nSusan Gee\nTerry Jones\n" },
        { .line = "wc -l < resources/contacts.txt", .expected = "8\n" },
        { .line = "grep Jones < resources/contacts.txt", .expected = "Bob Jones\nTerry Jones\n" },
        { .line = "tr a-z A-Z < resources/contacts.txt | head -n 1", .expected = "BOB JONES\n" },
        // input file can't be opened: nothing runs, nothing printed
        { .line = "cat < resources/nope.txt", .expected = "" }
    };

    // '>': expected is the content of the output file
    struct commands out_cases[] = {
        { .line = "echo hello world > /tmp/coen346_redir.txt", .expected = "hello world\n" },
        // shorter write over the previous file: must be truncated, not "hi\nlo world\n"
        { .line = "echo hi > /tmp/coen346_redir.txt", .expected = "hi\n" },
        { .line = "cat resources/contacts.txt | grep Jones > /tmp/coen346_redir.txt", .expected = "Bob Jones\nTerry Jones\n" },
        { .line = "sort < resources/contacts.txt > /tmp/coen346_redir.txt", .expected = "Bob Jones\nColin Doe\nDana David\nJenny Case\nLeonard Schmidt\nLeslie Smith\nLinda Gray\nSusan Gee\nTerry Jones\n" },
        { .line = "cat < resources/contacts.txt | sort | uniq > /tmp/coen346_redir.txt", .expected = "Bob Jones\nColin Doe\nDana David\nJenny Case\nLeonard Schmidt\nLeslie Smith\nLinda Gray\nSusan Gee\nTerry Jones\n" }
    };

    char output[1024];

    size_t in_count = sizeof(in_cases) / sizeof(in_cases[0]);
    for (size_t i = 0; i < in_count; i++) {
        capture_output(in_cases[i].line, output, sizeof(output));
        TEST_ASSERT_EQUAL_STRING(in_cases[i].expected, output + strspn(output, " \t"));
    }

    unlink("/tmp/coen346_redir.txt"); // start clean so O_CREAT is exercised
    size_t out_count = sizeof(out_cases) / sizeof(out_cases[0]);
    for (size_t i = 0; i < out_count; i++) {
        capture_output(out_cases[i].line, output, sizeof(output));
        TEST_ASSERT_EQUAL_STRING("", output); // went to the file, not stdout

        capture_output("cat /tmp/coen346_redir.txt", output, sizeof(output));
        TEST_ASSERT_EQUAL_STRING(out_cases[i].expected, output);
    }
    unlink("/tmp/coen346_redir.txt");
}

/**
 * @brief A pipeline ending with '&' must return immediately, and its
 *        commands must still be connected and run to completion.
 */
void test_background_pipes() {
    init_jobs();
    char output[256];

    // Two 1 s sleeps in the background: execute() should not wait for either.
    struct timeval start;
    gettimeofday(&start, NULL);
    capture_output("sleep 1 | sleep 1 &", output, sizeof(output));
    double dt = elapsed_since(start);
    TEST_ASSERT(dt < 0.5);
    while (waitpid(-1, NULL, 0) > 0); // reap both sleeps

    // Output goes to a file since capture_output returns before bg children finish.
    unlink("/tmp/coen346_bg.txt");
    capture_output("echo hello | tr a-z A-Z > /tmp/coen346_bg.txt &", output, sizeof(output));
    while (waitpid(-1, NULL, 0) > 0); // wait for the pipeline to finish

    capture_output("cat /tmp/coen346_bg.txt", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("HELLO\n", output);

    unlink("/tmp/coen346_bg.txt");
    init_jobs();
}

/**
 * @brief A writer that produces more than the pipe buffer holds (~64 KB) must
 *        not deadlock: every command of the pipeline has to be started before
 *        the shell waits on any of them. Before that fix this test hung, so an
 *        alarm aborts the test run instead of letting it stall forever.
 */
void test_pipeline_large_output() {
    char output[256];

    alarm(10); // a regression shows up as "Alarm clock" instead of a hang
    capture_output("seq 1 100000 | wc -l", output, sizeof(output));
    alarm(0);
    TEST_ASSERT_EQUAL_STRING("100000\n", output + strspn(output, " \t"));

    alarm(10);
    capture_output("seq 1 200000 | sort -n | tail -n 1", output, sizeof(output));
    alarm(0);
    TEST_ASSERT_EQUAL_STRING("200000\n", output);
}

/**
 * @brief When the reader exits early, the writer must get SIGPIPE and stop.
 *        That only happens if no process (including the writer itself) still
 *        holds the pipe's read end, i.e. each child closes the unused read end.
 *        `yes` never stops on its own, so a leaked read end hangs forever.
 */
void test_pipeline_reader_exits_early() {
    char output[256];

    alarm(10);
    capture_output("yes | head -n 1", output, sizeof(output));
    alarm(0);
    TEST_ASSERT_EQUAL_STRING("y\n", output);

    alarm(10);
    capture_output("seq 1 100000 | head -n 1", output, sizeof(output));
    alarm(0);
    TEST_ASSERT_EQUAL_STRING("1\n", output);

    alarm(10);
    capture_output("yes coen346 | head -n 3 | tr a-z A-Z", output, sizeof(output));
    alarm(0);
    TEST_ASSERT_EQUAL_STRING("COEN346\nCOEN346\nCOEN346\n", output);
}

/**
 * @brief Job commands live in a fixed-size slot (no strdup/free, which the
 *        SIGCHLD handler may not call). An over-long name is truncated to
 *        JOB_CMD_MAX - 1 characters rather than overflowing the slot.
 */
void test_job_long_command_truncated() {
    init_jobs();

    char long_cmd[JOB_CMD_MAX * 2];
    memset(long_cmd, 'x', sizeof(long_cmd) - 1);
    long_cmd[sizeof(long_cmd) - 1] = '\0';
    TEST_ASSERT(add_test_job(7070, long_cmd) == 0);

    char buf[2048];
    memset(buf, 0, sizeof(buf));
    FILE *orig = stdout;
    FILE *cap = fmemopen(buf, sizeof(buf), "w");
    TEST_ASSERT(cap != NULL);
    stdout = cap;
    list_jobs();
    fflush(cap);
    stdout = orig;
    fclose(cap);

    // Count the run of x's in the listing: exactly JOB_CMD_MAX - 1 survive.
    char *start = strchr(buf, 'x');
    TEST_ASSERT(start != NULL);
    TEST_ASSERT(strspn(start, "x") == JOB_CMD_MAX - 1);
    TEST_ASSERT(strstr(buf, "7070") != NULL); // the pid still follows

    init_jobs();
}

/* ================================================================
 * Additional tests (issue #3): the example commands from the spec
 * ================================================================ */

/**
 * @brief Runs a command line through /bin/sh and captures its stdout, to use
 *        as the expected output when it depends on the machine (file owners,
 *        network interfaces). stderr is discarded.
 * @param line   Command line to run.
 * @param buffer Destination for the captured output (always NUL-terminated).
 * @param size   Size of `buffer` in bytes.
 */
static void reference_output(const char *line, char *buffer, size_t size) {
    buffer[0] = '\0';

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s 2>/dev/null", line);
    FILE *sh = popen(cmd, "r");
    if (sh == NULL) return;

    size_t n = fread(buffer, 1, size - 1, sh);
    buffer[n] = '\0';
    pclose(sh);
}

/**
 * @brief Creates a temporary directory holding a few small known files
 *        (a.txt, b.txt, c.txt) for the ls-based tests.
 * @param dir Template buffer, e.g. "/tmp/coen346_ls_XXXXXX"; filled with the
 *            created directory's path.
 * @return 0 on success, -1 on failure.
 */
static int make_small_dir(char *dir) {
    if (mkdtemp(dir) == NULL) return -1;

    const char *names[]    = {"a.txt", "b.txt", "c.txt"};
    const char *contents[] = {"alpha\n", "beta\n", "gamma\n"};
    char path[512];
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        FILE *f = fopen(path, "w");
        if (f == NULL) return -1;
        fputs(contents[i], f);
        fclose(f);
    }
    return 0;
}

/**
 * @brief Removes the directory made by make_small_dir() and its files.
 */
static void remove_small_dir(const char *dir) {
    const char *names[] = {"a.txt", "b.txt", "c.txt"};
    char path[512];
    for (int i = 0; i < 3; i++) {
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        unlink(path);
    }
    rmdir(dir);
}

/**
 * @brief ls -l | awk '{print $9,$3,$4}' prints each file's name, owner and
 *        group. Owners differ between machines, so the expected output comes
 *        from /bin/sh running the same line in the same directory.
 */
void test_additional_ls_awk() {
    char cwd[1024];
    TEST_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);

    char dir[] = "/tmp/coen346_ls_XXXXXX";
    TEST_ASSERT(make_small_dir(dir) == 0);
    TEST_ASSERT(chdir(dir) == 0);

    const char *line = "ls -l | awk '{print $9,$3,$4}'";
    char output[1024], expected[1024];
    capture_output(line, output, sizeof(output));
    reference_output(line, expected, sizeof(expected));

    TEST_ASSERT(chdir(cwd) == 0);
    remove_small_dir(dir);

    TEST_ASSERT(strstr(output, "a.txt ") != NULL); // file name came first
    TEST_ASSERT(strstr(output, "c.txt ") != NULL);
    TEST_ASSERT_EQUAL_STRING(expected, output);
}

/**
 * @brief ls | xargs cat prints every file in a directory of small files, in
 *        the order ls lists them.
 */
void test_additional_ls_xargs_cat() {
    char cwd[1024];
    TEST_ASSERT(getcwd(cwd, sizeof(cwd)) != NULL);

    char dir[] = "/tmp/coen346_xargs_XXXXXX";
    TEST_ASSERT(make_small_dir(dir) == 0);
    TEST_ASSERT(chdir(dir) == 0);

    char output[1024];
    capture_output("ls | xargs cat", output, sizeof(output));

    TEST_ASSERT(chdir(cwd) == 0);
    remove_small_dir(dir);

    TEST_ASSERT_EQUAL_STRING("alpha\nbeta\ngamma\n", output);
}

/**
 * @brief The fixed-output example commands: grep on a file, a line count, a
 *        sed substitution.
 */
void test_additional_fixed_output() {
    struct commands {
        char *line;
        char *expected;
    };

    struct commands test_cases[] = {
        // grep with a file argument (no pipe); case-sensitive, so ERROR/Error are skipped
        { .line = "grep error resources/contents.txt", .expected = "This line contains an error.\nHere is another line with an error.\n" },
        // contents.txt has no trailing newline, so wc -l counts 8, not 9
        { .line = "cat resources/contents.txt | wc -l", .expected = "8\n" },
        { .line = "echo \"This is a test stream\" | sed 's/stream/text/'", .expected = "This is a test text\n" }
    };

    size_t count = sizeof(test_cases) / sizeof(test_cases[0]);
    char output[1024];

    for (size_t i = 0; i < count; i++) {
        capture_output(test_cases[i].line, output, sizeof(output));
        // skip leading padding (e.g. macOS wc right-aligns its counts)
        TEST_ASSERT_EQUAL_STRING(test_cases[i].expected, output + strspn(output, " \t"));
    }
}

/**
 * @brief ifconfig | grep 192 depends on the machine's interfaces (ifconfig may
 *        not even be installed), so compare against /bin/sh running the same
 *        line. Either way the two must agree.
 */
void test_additional_ifconfig_grep() {
    const char *line = "ifconfig | grep 192";
    char output[4096], expected[4096];

    capture_output(line, output, sizeof(output));
    reference_output(line, expected, sizeof(expected));
    TEST_ASSERT_EQUAL_STRING(expected, output);
}

/**
 * @brief cat contacts.txt | sort -k2 | uniq > contact_list.txt sorts on the
 *        second column, drops duplicates and writes the result to a new file
 *        instead of stdout.
 */
void test_additional_sort_uniq_to_file() {
    const char *list = "/tmp/coen346_contact_list.txt";
    unlink(list); // must be created by the redirection

    char output[1024];
    capture_output("cat resources/contacts.txt | sort -k2 | uniq > /tmp/coen346_contact_list.txt",
                   output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("", output); // went to the file, not stdout

    capture_output("cat /tmp/coen346_contact_list.txt", output, sizeof(output));
    TEST_ASSERT_EQUAL_STRING("Jenny Case\nDana David\nColin Doe\nSusan Gee\nLinda Gray\nBob Jones\nTerry Jones\nLeonard Schmidt\nLeslie Smith\n", output);

    unlink(list);
}

/* ================================================================
 * Wildcards and environment variables (*, [], ~, $VAR)
 * ================================================================ */

// Fixture directory with known files, so glob results match on every machine.
#define WILD_DIR "/tmp/coen346_wild"

/**
 * @brief Expansion of '*', '[]', '~' and environment variables, alone and
 *        combined, plus patterns that match nothing and undefined variables.
 *        Globs run against WILD_DIR; '~' and $HOME are compared against this
 *        machine's HOME since it differs between machines.
 */
void test_wildcards() {
    struct commands {
        char *line;
        char *expected;
    };

    // Fixture: three empty files (glob sorts matches: apple, banana, cherry)
    const char *files[] = { WILD_DIR "/apple.txt", WILD_DIR "/banana.txt", WILD_DIR "/cherry.log" };
    size_t file_count = sizeof(files) / sizeof(files[0]);
    mkdir(WILD_DIR, 0755); // may already exist from an aborted run, that's fine
    for (size_t i = 0; i < file_count; i++) {
        FILE *f = fopen(files[i], "w");
        TEST_ASSERT(f != NULL);
        fclose(f);
    }

    // wordexp runs in this process, so it sees these variables
    setenv("COEN346_VAR", "hello", 1);
    setenv("COEN346_DIR", WILD_DIR, 1);
    unsetenv("COEN346_UNDEF"); // guaranteed undefined

    struct commands test_cases[] = {
        // '*'
        { .line = "echo " WILD_DIR "/*", .expected = WILD_DIR "/apple.txt " WILD_DIR "/banana.txt " WILD_DIR "/cherry.log\n" },
        { .line = "echo " WILD_DIR "/*.txt", .expected = WILD_DIR "/apple.txt " WILD_DIR "/banana.txt\n" },
        { .line = "echo " WILD_DIR "/*an*", .expected = WILD_DIR "/banana.txt\n" },
        // ls only finds the file if it received the expanded name, not the pattern
        { .line = "ls " WILD_DIR "/*.log", .expected = WILD_DIR "/cherry.log\n" },
        // '[]': set, range, negation
        { .line = "echo " WILD_DIR "/[ab]*", .expected = WILD_DIR "/apple.txt " WILD_DIR "/banana.txt\n" },
        { .line = "echo " WILD_DIR "/[a-c]*.log", .expected = WILD_DIR "/cherry.log\n" },
        { .line = "echo " WILD_DIR "/[!a]*", .expected = WILD_DIR "/banana.txt " WILD_DIR "/cherry.log\n" },
        // no match: the pattern is passed through literally
        { .line = "echo " WILD_DIR "/*.pdf", .expected = WILD_DIR "/*.pdf\n" },
        { .line = "echo " WILD_DIR "/[xyz]*", .expected = WILD_DIR "/[xyz]*\n" },
        // '~' is only expanded at the start of a word
        { .line = "echo a~", .expected = "a~\n" },
        // environment variables
        { .line = "echo $COEN346_VAR", .expected = "hello\n" },
        { .line = "echo $COEN346_VAR world", .expected = "hello world\n" },
        { .line = "echo pre$COEN346_VAR", .expected = "prehello\n" },
        // undefined variables expand to nothing (the word disappears, no empty arg)
        { .line = "echo $COEN346_UNDEF", .expected = "\n" },
        { .line = "echo a $COEN346_UNDEF b", .expected = "a b\n" },
        // expands to zero words: nothing runs (needs the we_wordc == 0 check)
        { .line = "$COEN346_UNDEF", .expected = "" },
        // combinations
        { .line = "echo $COEN346_DIR/*.txt", .expected = WILD_DIR "/apple.txt " WILD_DIR "/banana.txt\n" },
        { .line = "echo $COEN346_DIR/[c]*", .expected = WILD_DIR "/cherry.log\n" },
        { .line = "echo $COEN346_DIR/*.pdf", .expected = WILD_DIR "/*.pdf\n" },
        { .line = "echo $COEN346_VAR " WILD_DIR "/[ab]*", .expected = "hello " WILD_DIR "/apple.txt " WILD_DIR "/banana.txt\n" },
        { .line = "echo $COEN346_UNDEF " WILD_DIR "/*.log", .expected = WILD_DIR "/cherry.log\n" },
        { .line = "ls " WILD_DIR "/*.txt | wc -l", .expected = "2\n" },
        { .line = "echo $COEN346_VAR | tr a-z A-Z", .expected = "HELLO\n" }
    };

    // HOME differs per machine: each %s in expected is replaced by $HOME
    struct commands home_cases[] = {
        { .line = "echo ~", .expected = "%s\n" },
        { .line = "echo $HOME", .expected = "%s\n" },
        { .line = "echo ~ $HOME", .expected = "%s %s\n" },
        // tilde expanded, then the glob matches nothing
        { .line = "echo ~/coen346_nomatch_*", .expected = "%s/coen346_nomatch_*\n" },
        { .line = "echo $HOME/coen346_nomatch_[xyz]", .expected = "%s/coen346_nomatch_[xyz]\n" }
    };

    size_t count = sizeof(test_cases) / sizeof(test_cases[0]);
    char output[1024];

    for (size_t i = 0; i < count; i++) {
        capture_output(test_cases[i].line, output, sizeof(output));
        // skip leading padding (e.g. macOS wc right-aligns its counts)
        TEST_ASSERT_EQUAL_STRING(test_cases[i].expected, output + strspn(output, " \t"));
    }

    const char *home = getenv("HOME");
    TEST_ASSERT(home != NULL);
    size_t home_count = sizeof(home_cases) / sizeof(home_cases[0]);
    char expected[1024];

    for (size_t i = 0; i < home_count; i++) {
        // home is passed twice for the "%s %s" case; unused extra args are ignored
        snprintf(expected, sizeof(expected), home_cases[i].expected, home, home);
        capture_output(home_cases[i].line, output, sizeof(output));
        TEST_ASSERT_EQUAL_STRING(expected, output);
    }

    // Cleanup
    for (size_t i = 0; i < file_count; i++) unlink(files[i]);
    rmdir(WILD_DIR);
    unsetenv("COEN346_VAR");
    unsetenv("COEN346_DIR");
}

/* ================================================================
 * Process computation time (SIGCHLD -> elapsed-time reporting)
 * ================================================================ */

/**
 * @brief Starts capturing everything written to file descriptor 1. The
 *        completion message is emitted with write() (async-signal-safe), which
 *        bypasses the stdout FILE, so swapping `stdout` would not see it.
 * @param saved_fd Receives a copy of the real stdout, for fd_capture_stop().
 * @return The capture file's descriptor, or -1 on failure.
 */
static int fd_capture_start(int *saved_fd) {
    char path[] = "/tmp/coen346_fd_XXXXXX";
    int tmp_fd = mkstemp(path);
    if (tmp_fd < 0) return -1;
    unlink(path); // removed once closed

    fflush(stdout);
    *saved_fd = dup(STDOUT_FILENO);
    dup2(tmp_fd, STDOUT_FILENO);
    return tmp_fd;
}

/**
 * @brief Restores the real stdout and copies what was captured into buffer.
 * @param tmp_fd   Descriptor returned by fd_capture_start().
 * @param saved_fd Real stdout saved by fd_capture_start().
 * @param buffer   Destination (always NUL-terminated).
 * @param size     Size of buffer in bytes.
 */
static void fd_capture_stop(int tmp_fd, int saved_fd, char *buffer, size_t size) {
    fflush(stdout);
    dup2(saved_fd, STDOUT_FILENO);
    close(saved_fd);

    lseek(tmp_fd, 0, SEEK_SET);
    ssize_t n = read(tmp_fd, buffer, size - 1);
    buffer[n > 0 ? n : 0] = '\0';
    close(tmp_fd);
}

/**
 * @brief Removing a tracked job emits the completion message: its command, the
 *        "completed in ... seconds" wording, and the blue colour the spec
 *        asks for.
 */
void test_completion_message_format() {
    init_jobs();
    add_test_job(8080, "sleep 1");

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int saved_fd;
    int cap = fd_capture_start(&saved_fd);
    TEST_ASSERT(cap >= 0);
    remove_job(8080);
    fd_capture_stop(cap, saved_fd, buf, sizeof(buf));

    TEST_ASSERT(strstr(buf, "] sleep 1 ")   != NULL); // the job's command line
    TEST_ASSERT(strstr(buf, "completed in") != NULL); // the wording
    TEST_ASSERT(strstr(buf, "seconds")      != NULL); // the unit
    TEST_ASSERT(strstr(buf, "\x1b[34m")     != NULL); // printed in blue

    init_jobs();
}

/**
 * @brief While the shell waits at its prompt, a completion message first
 *        erases the prompt line and then redraws the prompt after the
 *        message. When it is not waiting, the message is printed as is.
 */
void test_completion_message_redraws_prompt() {
    init_jobs();
    char buf[1024];
    int saved_fd;

    // At the prompt: "\r\033[K" ... message ... prompt
    set_waiting_prompt("PROMPT> ");
    add_test_job(8383, "sleep 1");
    memset(buf, 0, sizeof(buf));
    int cap = fd_capture_start(&saved_fd);
    TEST_ASSERT(cap >= 0);
    remove_job(8383);
    fd_capture_stop(cap, saved_fd, buf, sizeof(buf));
    TEST_ASSERT(strncmp(buf, "\r\x1b[K", 4) == 0);            // prompt line erased first
    TEST_ASSERT(strstr(buf, "sleep 1 completed in") != NULL);
    size_t n = strlen(buf);
    TEST_ASSERT(n >= 8 && strcmp(buf + n - 8, "PROMPT> ") == 0); // prompt redrawn last

    // Not at the prompt (e.g. a foreground command is running): no erase, no prompt
    set_waiting_prompt(NULL);
    add_test_job(8384, "sleep 1");
    memset(buf, 0, sizeof(buf));
    cap = fd_capture_start(&saved_fd);
    TEST_ASSERT(cap >= 0);
    remove_job(8384);
    fd_capture_stop(cap, saved_fd, buf, sizeof(buf));
    TEST_ASSERT(buf[0] != '\r');
    TEST_ASSERT(strstr(buf, "PROMPT> ") == NULL);

    init_jobs();
}

/**
 * @brief The reported time reflects real elapsed wall-clock time, measured
 *        from add_job's start_time to remove_job: a job left alive ~0.3s
 *        should report at least that.
 */
void test_completion_time_tracks_elapsed() {
    init_jobs();
    add_test_job(8081, "work");
    usleep(300000); // 0.3 s

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int saved_fd;
    int cap = fd_capture_start(&saved_fd);
    TEST_ASSERT(cap >= 0);
    remove_job(8081);
    fd_capture_stop(cap, saved_fd, buf, sizeof(buf));

    char *p = strstr(buf, "completed in ");
    TEST_ASSERT(p != NULL);
    double secs = -1.0;
    TEST_ASSERT(sscanf(p, "completed in %lf", &secs) == 1);
    TEST_ASSERT(secs >= 0.25); // at least ~0.3s (generous lower bound)
    TEST_ASSERT(secs <  2.0);  // and not a garbage value

    init_jobs();
}

/**
 * @brief A job removed immediately reports a near-zero time - a sanity check
 *        that start_time is set in add_job rather than left uninitialised.
 */
void test_completion_time_near_zero_when_instant() {
    init_jobs();
    add_test_job(8082, "quick");

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int saved_fd;
    int cap = fd_capture_start(&saved_fd);
    TEST_ASSERT(cap >= 0);
    remove_job(8082);
    fd_capture_stop(cap, saved_fd, buf, sizeof(buf));

    char *p = strstr(buf, "completed in ");
    TEST_ASSERT(p != NULL);
    double secs = -1.0;
    TEST_ASSERT(sscanf(p, "completed in %lf", &secs) == 1);
    TEST_ASSERT(secs >= 0.0);
    TEST_ASSERT(secs <  0.2); // effectively instant

    init_jobs();
}

/**
 * @brief End-to-end: with the real SIGCHLD handler installed, a background
 *        job that finishes is reaped by the handler, which removes it from
 *        the table and prints its computation time - the main code never
 *        calls wait() itself.
 */
void test_sigchld_handler_reports_background_completion() {
    init_jobs();

    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGCHLD);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    TEST_ASSERT(sigaction(SIGCHLD, &sa, NULL) == 0);

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int saved_fd;
    int cap = fd_capture_start(&saved_fd);
    TEST_ASSERT(cap >= 0);

    run_line("true &"); // bg: execute() records the job; the child exits at once

    // Let the child die and the handler run. Do NOT wait() here - that would
    // steal the child from the handler we are testing.
    for (int i = 0; i < 100; i++) usleep(10000); // up to ~1 s

    fd_capture_stop(cap, saved_fd, buf, sizeof(buf));

    // The handler printed the completion message...
    TEST_ASSERT(strstr(buf, "completed in") != NULL);

    // ...and cleared the slot: the table is empty, so MAX_JOBS adds all fit.
    int added = 0;
    for (int i = 0; i < MAX_JOBS; i++)
        if (add_test_job(9000 + i, "probe") == 0) added++;
    TEST_ASSERT(added == MAX_JOBS); // 0 slots were still occupied

    init_jobs();
    signal(SIGCHLD, SIG_DFL); // restore default for later tests
}

/**
 * @brief The handler drains ALL finished children on a single delivery:
 *        SIGCHLD is not queued, so several jobs ending at once must still be
 *        reaped via the waitpid(-1, WNOHANG) loop. Launch three, confirm
 *        none linger in the table.
 */
void test_sigchld_handler_reaps_multiple() {
    init_jobs();

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
    int saved_fd;
    int cap = fd_capture_start(&saved_fd);
    TEST_ASSERT(cap >= 0);

    run_line("true &");
    run_line("true &");
    run_line("true &");

    for (int i = 0; i < 100; i++) usleep(10000); // up to ~1 s

    fd_capture_stop(cap, saved_fd, buf, sizeof(buf));

    int added = 0;
    for (int i = 0; i < MAX_JOBS; i++)
        if (add_test_job(9100 + i, "probe") == 0) added++;
    TEST_ASSERT(added == MAX_JOBS); // all three reaped => table empty

    init_jobs();
    signal(SIGCHLD, SIG_DFL);
}

/**
 * @brief A background pipeline is ONE job: `jobs` lists it once, with the
 *        full command line and every pid, and the completion message is
 *        printed once, when its last process finishes.
 */
void test_background_pipeline_is_one_job() {
    init_jobs();
    run_line("sleep 1 | sleep 1 &");

    // Listed once, with both commands and their arguments.
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
    TEST_ASSERT(strstr(buf, "[0] sleep 1 | sleep 1 (pid: ") != NULL);
    TEST_ASSERT(strstr(buf, "[1]") == NULL); // not one job per process

    // The first process to finish does not end the job...
    char msg[1024];
    memset(msg, 0, sizeof(msg));
    int saved_fd;
    int fd = fd_capture_start(&saved_fd);
    TEST_ASSERT(fd >= 0);
    pid_t first = waitpid(-1, NULL, 0);
    TEST_ASSERT(remove_job(first) == 0);
    fd_capture_stop(fd, saved_fd, msg, sizeof(msg));
    TEST_ASSERT(strstr(msg, "completed in") == NULL);

    // ...the last one does, with a single message for the whole pipeline.
    memset(msg, 0, sizeof(msg));
    fd = fd_capture_start(&saved_fd);
    TEST_ASSERT(fd >= 0);
    pid_t second = waitpid(-1, NULL, 0);
    TEST_ASSERT(remove_job(second) == 0);
    fd_capture_stop(fd, saved_fd, msg, sizeof(msg));
    TEST_ASSERT(strstr(msg, "sleep 1 | sleep 1 completed in") != NULL);
    TEST_ASSERT(remove_job(second) == -1); // the job is gone

    init_jobs();
}

/**
 * @brief Arguments containing quotes reach the program unchanged: a ' or "
 *        left in an argument after parsing must not break wordexp().
 */
void test_quoted_arguments() {
    struct commands {
        char *line;
        char *expected;
    };
    struct commands test_cases[] = {
        { .line = "echo \"it's\"",          .expected = "it's\n" },
        { .line = "echo it\\'s",            .expected = "it's\n" },
        { .line = "echo \"don't | stop\"",  .expected = "don't | stop\n" },
        { .line = "echo 'say \"hi\"'",       .expected = "say \"hi\"\n" },
    };
    char output[256];
    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        capture_output(test_cases[i].line, output, sizeof(output));
        TEST_ASSERT_EQUAL_STRING(test_cases[i].expected, output);
    }
}

/**
 * @brief A command that fails to exec must not print the shell's buffered
 *        output a second time (the child would flush its copy of the buffer).
 */
void test_failed_exec_does_not_duplicate_output() {
    char path[] = "/tmp/coen346_dup_XXXXXX";
    int fd = mkstemp(path);
    TEST_ASSERT(fd >= 0);

    FILE *orig = stdout;
    stdout = fdopen(fd, "w"); // a file stream is fully buffered
    printf("pending\n");      // still sitting in stdout's buffer

    char *args[] = {"coen346_no_such_command", NULL};
    execute_command(args[0], args, STDIN_FILENO, STDOUT_FILENO, 0, 1);

    fclose(stdout); // flushes and closes fd
    stdout = orig;

    char buf[64] = {0};
    FILE *f = fopen(path, "r");
    TEST_ASSERT(f != NULL);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    unlink(path);

    TEST_ASSERT_EQUAL_STRING("pending\n", buf);
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
    test_failed_exec_does_not_duplicate_output();
    test_background_execution();

    // listing background processes (jobs)
    test_job_add_returns_success();
    test_job_remove_semantics();
    test_job_table_capacity();
    test_job_slot_reuse_after_remove();
    test_jobs_listing_output();
    test_job_long_command_truncated();
    test_background_job_is_registered();

    // process computation time (SIGCHLD elapsed-time reporting)
    test_completion_message_format();
    test_completion_message_redraws_prompt();
    test_completion_time_tracks_elapsed();
    test_completion_time_near_zero_when_instant();
    test_sigchld_handler_reports_background_completion();
    test_sigchld_handler_reaps_multiple();
    test_background_pipeline_is_one_job();

    // simple pipes (x | y)
    test_simple_pipes();

    // multiple pipes, redirection, background pipelines
    test_multiple_pipes();
    test_redirection();
    test_background_pipes();
    test_pipeline_large_output();
    test_pipeline_reader_exits_early();

    // additional tests from the spec (issue #3)
    test_additional_ls_awk();
    test_additional_ls_xargs_cat();
    test_additional_fixed_output();
    test_additional_ifconfig_grep();
    test_additional_sort_uniq_to_file();

    // wildcards and environment variables (*, [], ~, $VAR)
    test_wildcards();
    test_quoted_arguments();

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
