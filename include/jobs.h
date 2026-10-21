#ifndef JOBS_H
#define JOBS_H

#include <signal.h>   // for sig_atomic_t
#include <time.h>     // for struct timespec
#include <unistd.h>

#define MAX_JOBS 10
#define JOB_CMD_MAX 256 /* longer command names are truncated in the listing */

/* Everything uses fixed-size storage so the SIGCHLD handler can remove a
 * job without malloc/free. */
typedef struct {
    pid_t pid;
    volatile sig_atomic_t active;   // 0 = free slot
    struct timespec start_time;     // CLOCK_MONOTONIC, for process computation time
    char cmd[JOB_CMD_MAX];          // for the listing text
} job_t;

int init_jobs(void);
int add_job(pid_t pid, const char *cmd);
int remove_job(pid_t pid);
int list_jobs(void);

#endif // JOBS_H
