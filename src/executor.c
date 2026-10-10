#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/wait.h>

#include "parser.h"

/**
 * @brief Executes a single, simple command in another process
 * @param cmd
 * @param args
 * @param in
 * @param out
 * @param bg
 * @return
 */
int execute_command(char* cmd, char** args, int bg) {
    // TODO: Implement the function to execute a command with given arguments, input/output redirection, and background execution.
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork failed");
        return EXIT_FAILURE;
    }
    else if (pid == 0) { // child
        execvp(cmd, args);
        perror(cmd); // only reached if execvp fails
        exit(EXIT_FAILURE);
    } else {
        if (bg == 0) waitpid(pid, NULL, 0); // wait(NULL) reaps the previous zombie child process, but we want to wait for 
                                            // the specific child process we just created. Using waitpid with the child's pid 
                                            // ensures that we wait for the correct process.
        else printf("child background process pid: %d\n", (int) pid);
    }
    return EXIT_SUCCESS;
}

/**
 * @brief Executes a command pipeline.
 * @param l A pointer to a cmdline structure containing the parsed command.
 * @return 0 on success, or a non-zero value on error.
 */
int execute(struct cmdline *l){
    printf("TODO: Execute the command\n");
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
        fflush(stdout); 
        execute_command(cmd[0], cmd, l->bg);
    }
    return EXIT_SUCCESS;
}
