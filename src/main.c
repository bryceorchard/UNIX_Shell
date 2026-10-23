/**
 * @file main.c
 * @brief Entry point: installs the SIGCHLD handler and runs the
 *        read, parse and execute loop.
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "shell.h"
#include "executor.h"
#include "jobs.h"
#include "utils.h"

//example pipes https://www.redhat.com/sysadmin/pipes-command-line-linux#:~:text=One%20of%20the%20most%20powerful,of%20output%20or%20file%20descriptors.


/**
 * Start of shell program
 * @return
 */
int main(void) {
    init_jobs(); // initialize the job list

    // Set up the SIGCHLD handler to reap background processes
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGCHLD);          // block SIGCHLD while handler runs
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;  // restart fgets and waitpid; ignore stop/cont
    sigaction(SIGCHLD, &sa, NULL);

    while (1) {
        struct cmdline *l;
        char *line=0;
        char *prompt = "myshell>";

        /* Readline use some internal memory structure that
           can not be cleaned at the end of the program. Thus
           one memory leak per command seems unavoidable yet */
        line = readline(prompt); // line is a pointer to char (string)
        if (line == 0 || ! strncmp(line,"exit", 4)) {
            terminate(line);
        }
        else {
            /* parsecmd, free line, and set it up to 0 */
            l = parsecmd( & line);

            /* If input stream closed, normal termination */
            if (l == 0) {

                terminate(0);
            }
            else if (l->err != 0) {
                /* Syntax error, read another command */
                printf("error: %s\n", l->err);
                continue;
            }
            else {
                execute(l);
            }
        }
    }
}
