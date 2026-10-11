#ifndef JOBS_H
#define JOBS_H

#include <unistd.h>
#include <sys/time.h> // for struct timeval

#define MAX_JOBS 10

typedef struct {
    pid_t pid;
    int   active;     /* 0 = free slot */
    struct timeval start_time; /* for process computation time */
    char *cmd;        /* for the listing text */
} job_t;

int init_jobs(void);
int add_job(pid_t pid, const char *cmd);
int remove_job(pid_t pid);
int list_jobs(void);
int free_jobs(void);

#endif // JOBS_H