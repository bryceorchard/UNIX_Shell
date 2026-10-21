#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "jobs.h"

#define KBLUE  "\x1B[34m"
#define KRESET "\x1b[0m"

static job_t jobs[MAX_JOBS];

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
 * @brief Records a background process in the first free slot.
 *        Call with SIGCHLD blocked so the handler cannot run mid-update.
 * @param pid Process id of the background child.
 * @param cmd Command name shown by `jobs` (truncated to JOB_CMD_MAX - 1 chars).
 * @return 0 on success, -1 if the table is full.
 */
int add_job(pid_t pid, const char *cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            jobs[i].pid = pid;
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
 * @brief Retires a finished job and immediately prints, in blue, how long it ran:
 *        " Job with PID <pid> completed in <s.cc> seconds".
 *        Async-signal-safe (no malloc/free/stdio), so the SIGCHLD handler calls it.
 * @param pid Process id that finished.
 * @return 0 on success, -1 if pid was not tracked.
 */
int remove_job(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active && jobs[i].pid == pid) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now); // async-signal-safe, unlike gettimeofday
            long long elapsed_ns = (long long) (now.tv_sec - jobs[i].start_time.tv_sec) * 1000000000LL
                                 + (now.tv_nsec - jobs[i].start_time.tv_nsec);
            if (elapsed_ns < 0) elapsed_ns = 0;
            unsigned long centis = (unsigned long) ((elapsed_ns + 5000000LL) / 10000000LL); // round to 0.01 s

            char msg[128];
            size_t len = 0;
            append_str(msg, sizeof(msg), &len, KBLUE " Job with PID ");
            append_uint(msg, sizeof(msg), &len, (unsigned long) pid, 1);
            append_str(msg, sizeof(msg), &len, " completed in ");
            append_uint(msg, sizeof(msg), &len, centis / 100, 1);
            append_str(msg, sizeof(msg), &len, ".");
            append_uint(msg, sizeof(msg), &len, centis % 100, 2);
            append_str(msg, sizeof(msg), &len, " seconds" KRESET "\n");

            ssize_t ignored = write(STDOUT_FILENO, msg, len);
            (void) ignored; // nothing useful to do on a failed write in a handler

            jobs[i].active = 0; // free the slot; the cmd buffer is simply reused
            return 0; // Successfully removed
        }
    }
    return -1; // Job not found
}

/**
 * @brief Prints the active background jobs (the `jobs` builtin). Runs in the
 *        main program with SIGCHLD blocked, so stdio is fine here.
 * @return 0
 */
int list_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            printf("[%d] %s (pid: %d)\n", i, jobs[i].cmd, jobs[i].pid);
        }
    }
    return 0;
}
