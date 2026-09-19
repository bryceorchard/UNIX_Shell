#include <stdio.h>
#include <string.h>

#include "shell.h"
#include "executor.h"

//example pipes https://www.redhat.com/sysadmin/pipes-command-line-linux#:~:text=One%20of%20the%20most%20powerful,of%20output%20or%20file%20descriptors.


/**
 * Start of shell program
 * @return
 */
int main(void) {
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
