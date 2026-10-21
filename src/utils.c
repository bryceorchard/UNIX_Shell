//
// Created by Paula on 2024-07-27.
//

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "utils.h"
#include "jobs.h"

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

void memory_error(void)
{
    errno = ENOMEM;
    perror(0);
    exit(EXIT_FAILURE);
}

void *xmalloc(size_t size)
{
    void *p = malloc(size);
    if (!p) memory_error();
    return p;
}

void *xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (!p) memory_error();
    return p;
}
