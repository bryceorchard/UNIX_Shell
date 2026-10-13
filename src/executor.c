#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>

#include "parser.h"
#include "jobs.h"

/**
 * @brief Executes a single, simple command in another process
 * @param cmd
 * @param args
 * @param in
 * @param out
 * @param bg
 * @return
 */
int execute_command(char* cmd, char** args, int in, int out, int bg) {

    sigset_t mask, prev;
    sigemptyset(&mask); 
    sigaddset(&mask, SIGCHLD);

    if (strcmp(args[0], "jobs") == 0) { 
        sigprocmask(SIG_BLOCK, &mask, &prev); // Block SIGCHLD to prevent race conditions when adding jobs
        list_jobs(); 
        sigprocmask(SIG_SETMASK, &prev, NULL); // Restore previous signal mask
        return EXIT_SUCCESS; 
    }

    sigprocmask(SIG_BLOCK, &mask, &prev);

    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork failed");
        return EXIT_FAILURE;
    }
    else if (pid == 0) { // child
        if (in != STDIN_FILENO) {
            dup2(in, STDIN_FILENO);
            close(in);
        }
        if (out != STDOUT_FILENO) {
            dup2(out, STDOUT_FILENO);
            close(out);
        }
        execvp(cmd, args);
        perror(cmd); // only reached if execvp fails
        exit(EXIT_FAILURE);
    } else { // parent
        if (bg == 0) {
            sigprocmask(SIG_SETMASK, &prev, NULL); // Restore previous signal mask
            waitpid(pid, NULL, 0);
        } else {
            printf("child background process pid: %d\n", (int) pid);
            add_job(pid, cmd); // Add the background job to the job list
            sigprocmask(SIG_SETMASK, &prev, NULL);
        }
    }
    return EXIT_SUCCESS;
}

/**
 * @brief Executes a command pipeline.
 * @param l A pointer to a cmdline structure containing the parsed command.
 * @return 0 on success, or a non-zero value on error.
 */
int execute(struct cmdline *l){
    int i,j;
    if (l->in) printf("in: %s\n", l->in);
    if (l->out) printf("out: %s\n", l->out);
    if (l->bg) printf("background (&)\n");

    /* Display each command of the pipe */
    for (i=0; l->seq[i]!=0; i++) {
        char **cmd = l->seq[i];
        printf("seq[%d]: ", i);
        for (j=0; cmd[j]!=0; j++) {
            printf("'%s' ", cmd[j]);
        }
        printf("\n");
        // fflush(stdout); // Not needed since "\n" already flushes the buffer, but it likely will be needed for writing to files.
        execute_command(cmd[0], cmd, STDIN_FILENO, STDOUT_FILENO, l->bg);
    }
    return EXIT_SUCCESS;
}
