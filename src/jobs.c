/**
 * @file jobs.c
 * @brief Background job table: records jobs, reaps finished children
 *        (SIGCHLD handler), reports their computation time, redraws the
 *        prompt, and implements the `jobs` builtin.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>

#include "jobs.h"

#define KBLUE  "\x1B[34m"
#define KRESET "\x1b[0m"

static job_t jobs[MAX_JOBS];

static volatile sig_atomic_t at_prompt = 0; // 1 while the shell waits for input at its prompt
static const char *prompt_line = "";        // the prompt as printed, redrawn after a message

/**
 * @brief SIGCHLD handler: reaps every finished child (signals are not queued,
 *        so one delivery may stand for several exits) and reports finished
 *        background jobs. Only async-signal-safe calls happen here: waitpid()
 *        and remove_job(), which uses clock_gettime() and write().
 * @param sig Signal number (always SIGCHLD; unused).
 */
void sigchld_handler(int sig) {
    (void) sig;
    int saved_errno = errno; // waitpid may clobber errno
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        remove_job(pid); // prints the computation time and frees the jobs slot
    }
    errno = saved_errno;
}

/**
 * @brief Marks every slot of the job table free.
 * @return 0
 */
int init_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        jobs[i].active = 0;
    }
    return 0;
}

/**
 * @brief Records a background job (a command or a whole pipeline) in the
 *        first free slot. Call with SIGCHLD blocked, from before the first
 *        process is forked, so the handler cannot see a process of the job
 *        finish before the job is recorded.
 * @param pids  Process ids of the job's processes; entries <= 0 (commands
 *              that were not started) are skipped.
 * @param count Number of entries in pids.
 * @param cmd   Command line shown by `jobs` (truncated to JOB_CMD_MAX - 1 chars).
 * @return 0 on success (or if no process was started, so there is nothing to
 *         record), -1 if the table is full or the job has more than
 *         JOB_PIDS_MAX processes.
 */
int add_job(const pid_t *pids, int count, const char *cmd) {
    int started = 0;
    for (int k = 0; k < count; k++) {
        if (pids[k] > 0) started++;
    }
    if (started == 0) return 0;
    if (started > JOB_PIDS_MAX) return -1;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            int n = 0;
            for (int k = 0; k < count; k++) {
                if (pids[k] > 0) jobs[i].pids[n++] = pids[k];
            }
            while (n < JOB_PIDS_MAX) jobs[i].pids[n++] = 0;
            jobs[i].remaining = started;
            clock_gettime(CLOCK_MONOTONIC, &jobs[i].start_time); // Record the start time
            snprintf(jobs[i].cmd, sizeof(jobs[i].cmd), "%s", cmd); // copy into the slot, no malloc
            jobs[i].active = 1; // set last: the slot is complete once it is visible
            return 0; // Successfully added
        }
    }
    return -1; // No free slot available
}

/* 
 * Async-signal-safe formatting helpers for remove_job(). stdio is off
 * limits inside the SIGCHLD handler, so the message is built by hand
 * and emitted with a single write().
*/

/**
 * @brief Appends a NUL-terminated string to buf at *len (bounded by size).
 */
static void append_str(char *buf, size_t size, size_t *len, const char *s) {
    while (*s && *len + 1 < size) buf[(*len)++] = *s++;
}

/**
 * @brief Appends the decimal digits of a non-negative number to buf at *len.
 * @param min_digits Pad with leading zeros up to this many digits.
 */
static void append_uint(char *buf, size_t size, size_t *len, unsigned long n, int min_digits) {
    char digits[24];
    int count = 0;
    do {
        digits[count++] = (char) ('0' + n % 10);
        n /= 10;
    } while (n > 0 && count < (int) sizeof(digits));
    while (count < min_digits && count < (int) sizeof(digits)) digits[count++] = '0';
    while (count > 0 && *len + 1 < size) buf[(*len)++] = digits[--count];
}

/**
 * @brief Prints, in blue, how long job i ran:
 *        " Job [<i>] <command line> completed in <s.cc> seconds".
 *        If the shell is waiting at its prompt, the prompt line is erased
 *        first and the prompt is drawn again below the message, so it does
 *        not end up stranded above it. Anything half-typed on that line is
 *        discarded too, so input the user can no longer see is never run.
 *        Async-signal-safe: the message is built by hand and sent with one write().
 * @param i Index of the job in the table.
 */
static void report_job_completion(int i) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now); // async-signal-safe, unlike gettimeofday
    long long elapsed_ns = (long long) (now.tv_sec - jobs[i].start_time.tv_sec) * 1000000000LL
                         + (now.tv_nsec - jobs[i].start_time.tv_nsec);
    if (elapsed_ns < 0) elapsed_ns = 0;
    unsigned long centis = (unsigned long) ((elapsed_ns + 5000000LL) / 10000000LL); // round to 0.01 s

    char msg[JOB_CMD_MAX + 256];
    size_t len = 0;
    int redraw = at_prompt; 
    if (redraw) append_str(msg, sizeof(msg), &len, "\r\033[K"); // to column 0, clear the line
    append_str(msg, sizeof(msg), &len, KBLUE " Job [");
    append_uint(msg, sizeof(msg), &len, (unsigned long) i, 1);
    append_str(msg, sizeof(msg), &len, "] ");
    append_str(msg, sizeof(msg), &len, jobs[i].cmd);
    append_str(msg, sizeof(msg), &len, " completed in ");
    append_uint(msg, sizeof(msg), &len, centis / 100, 1);
    append_str(msg, sizeof(msg), &len, ".");
    append_uint(msg, sizeof(msg), &len, centis % 100, 2);
    append_str(msg, sizeof(msg), &len, " seconds" KRESET "\n");
    if (redraw) {
        append_str(msg, sizeof(msg), &len, prompt_line);
        // The erase also hid any half-typed input, still pending in the terminal
        // tcflush erases it
        tcflush(STDIN_FILENO, TCIFLUSH);
    }

    ssize_t ignored = write(STDOUT_FILENO, msg, len);
    (void) ignored; // nothing useful to do on a failed write in a handler
}

/**
 * @brief Records that a background process finished. When it was the last
 *        running process of its job, prints the job's computation time and
 *        frees the slot. Async-signal-safe (no malloc/free/stdio), so the
 *        SIGCHLD handler calls it.
 * @param pid Process id that finished.
 * @return 0 on success, -1 if pid was not tracked.
 */
int remove_job(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) continue;
        for (int k = 0; k < JOB_PIDS_MAX; k++) {
            if (jobs[i].pids[k] == pid) {
                jobs[i].pids[k] = 0; // this process is done
                jobs[i].remaining--;
                if (jobs[i].remaining == 0) { // the whole job is done
                    report_job_completion(i);
                    jobs[i].active = 0; // free the slot; its buffers are simply reused
                }
                return 0; // Successfully removed
            }
        }
    }
    return -1; // Job not found
}

/**
 * @brief Prints the active background jobs (the `jobs` builtin) with the
 *        pids of their processes still running. Runs in the main program
 *        with SIGCHLD blocked, so stdio is fine here.
 * @return 0
 */
int list_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            printf("[%d] %s (pid:", i, jobs[i].cmd);
            for (int k = 0; k < JOB_PIDS_MAX; k++) { // processes still running
                if (jobs[i].pids[k] > 0) printf(" %d", (int) jobs[i].pids[k]);
            }
            printf(")\n");
        }
    }
    return 0;
}

/**
 * @brief Tells the completion reporter whether the shell is waiting at its
 *        prompt. If it is, a completion message erases the prompt line
 *        and redraws the prompt below the message. Call with SIGCHLD
 *        blocked
 * @param line The prompt exactly as printed (colour codes included), or NULL
 *             once the shell is no longer waiting at the prompt.
 */
void set_waiting_prompt(const char *line) {
    if (line != NULL) {
        prompt_line = line;
        at_prompt = 1; // the handler only reads prompt_line once this is 1
    }
    else {
        at_prompt = 0;
    }
}
