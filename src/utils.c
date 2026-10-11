//
// Created by Paula on 2024-07-27.
//

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "utils.h"
#include "jobs.h"

void sigchld_handler(int sig) {
    // compilation warning: unused parameter 'sig'
    int saved_errno = errno; // waitpid may clobber errno
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        remove_job(pid); // frees cmd *char and resets values
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
