/**
 * @file jobs.h
 * @brief Background job table: job_t and the functions that manage it.
 */

#ifndef JOBS_H
#define JOBS_H

#include <signal.h>   // for sig_atomic_t
#include <time.h>     // for struct timespec
#include <unistd.h>

#define MAX_JOBS 10
#define JOB_PIDS_MAX 16 // most processes (pipeline commands) one job can track
#define JOB_CMD_MAX 256 // longer command lines are truncated in the listing

/* A job is one command line run in the background: a single command or a
 * whole pipeline, whose processes are all part of the same job.
 * Everything uses fixed-size storage so the SIGCHLD handler can update a
 * job without malloc/free. */
typedef struct {
    volatile sig_atomic_t active;   // 0 = free slot
    pid_t pids[JOB_PIDS_MAX];       // one per process; 0 once that process has finished
    int remaining;                  // processes still running; the job ends at 0
    struct timespec start_time;     // CLOCK_MONOTONIC, for process computation time
    char cmd[JOB_CMD_MAX];          // full command line, for the listing
} job_t;

void sigchld_handler(int sig);
int init_jobs(void);
int add_job(const pid_t *pids, int count, const char *cmd);
int remove_job(pid_t pid);
int list_jobs(void);
void set_waiting_prompt(const char *line);

#endif // JOBS_H
