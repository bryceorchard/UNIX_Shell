#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "jobs.h"

#define KBLUE  "\x1B[34m"
#define KRESET "\x1b[0m"

static job_t jobs[MAX_JOBS];

int init_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        jobs[i].active = 0;
    }
    return 0;
}

int add_job(pid_t pid, const char *cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            jobs[i].pid = pid;
            jobs[i].active = 1;
            gettimeofday(&jobs[i].start_time, NULL); // Record the start time
            jobs[i].cmd = strdup(cmd); // Duplicate the command string

            // NOTE: allocating and freeing memory for the command string can interfere with the main loop.
            // The parser can malloc for the command string buffer, then get interrupted by a SIGCHLD signal halfway through.
            // Then, the signal handler frees the cmd string - writing to memory at the same time as the main loop. (mutex deadlock)
            // TODO: use a fixed-size buffer for the command string to avoid this issue.

            return 0; // Successfully added
        }
    }
    return -1; // No free slot available
}

// Future suggestions:
// Do the minimum in the handler and defer the rest: 
// in the handler, just waitpid + record which PIDs finished 
// (write into a volatile sig_atomic_t flag or a small preallocated array — no malloc, no free, no stdio), 
// then let the main loop notice the flag and do the printing and freeing where no lock is at risk. 
// Or, if you want to keep it self-contained in the handler, 
// emit with write(STDOUT_FILENO, ...) after formatting the number by hand, and skip the free (reuse fixed-size slots instead of strdup/free).
int remove_job(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active && jobs[i].pid == pid) {
            struct timeval now;
            gettimeofday(&now, NULL);
            double elapsed = (now.tv_sec - jobs[i].start_time.tv_sec) + (now.tv_usec - jobs[i].start_time.tv_usec) / 1e6;
            
            printf("%s Job with PID %d completed in %.2f seconds%s\n", KBLUE, pid, elapsed, KRESET); // unsafe - would rather use write() but printf is easier for now

            jobs[i].active = 0;
            free(jobs[i].cmd); // Free the command string

            return 0; // Successfully removed
        }
    }
    return -1; // Job not found
}

// Same thing as above: printing in a signal handler is not safe. Use write() instead of printf() to avoid potential issues with stdio.
int list_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            printf("[%d] %s (pid: %d)\n", i, jobs[i].cmd, jobs[i].pid);
        }
    }
    return 0;
}

int free_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            free(jobs[i].cmd);
            jobs[i].active = 0;
        }
    }
    return 0;
}