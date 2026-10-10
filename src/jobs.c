#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jobs.h"

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
            jobs[i].cmd = strdup(cmd); // Duplicate the command string
            return 0; // Successfully added
        }
    }
    return -1; // No free slot available
}
int remove_job(pid_t pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active && jobs[i].pid == pid) {
            free(jobs[i].cmd); // Free the command string
            jobs[i].active = 0;
            return 0; // Successfully removed
        }
    }
    return -1; // Job not found
}

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