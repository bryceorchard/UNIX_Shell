#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <wordexp.h>

#include "parser.h"
#include "jobs.h"

#define MAX_ARG_LENGTH 1024

/**
 * @brief Executes a single, simple command in another process
 * @param cmd
 * @param args
 * @param in File descriptior the command reads from (STDIN_FILENO if none)
 * @param out File descriptor the command writes to (STDOUT_FILENO if none)
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
    int cmd_in = STDIN_FILENO;
    int file_out = STDOUT_FILENO;

    if (l->in) printf("in: %s\n", l->in);
    if (l->out) printf("out: %s\n", l->out);
    if (l->bg) printf("background (&)\n");

    if (l->in) {
        cmd_in = open(l->in, O_RDONLY);
        if (cmd_in == -1) {
            perror("Input file open failed");
            return EXIT_FAILURE;
        }
    }
    if (l->out) {
        file_out = open(l->out, O_WRONLY | O_CREAT, 0644);
        if (file_out == -1) {
            perror("Output file open failed");
            if (cmd_in != STDIN_FILENO) close(cmd_in);
            return EXIT_FAILURE;
        }
        if (ftruncate(file_out, 0) == -1) {
            perror("Output file truncate failed");
            if (cmd_in != STDIN_FILENO) close(cmd_in);
            close(file_out);
            return EXIT_FAILURE;
        }
    }
    
    /* Run each command of the pipe */
    for (i=0; l->seq[i]!=0; i++) {
        char **cmd = l->seq[i];
        int cmd_out = STDOUT_FILENO;
        if (l->seq[i+1] == 0) cmd_out = file_out;
        int fds[2];

        size_t cmd_length = 0;
        for (j = 0; cmd[j] != NULL; j++) {
            cmd_length += strnlen(cmd[j], MAX_ARG_LENGTH) + 3; // space (1) + quotes (2)
        }
        cmd_length++; // + \0 (1)
        char *cmd_buffer = malloc(cmd_length);
        if (cmd_buffer == NULL) {
            perror("malloc failed");
            if (cmd_in != STDIN_FILENO) close(cmd_in);
            if (file_out != STDOUT_FILENO) close(file_out);
            return EXIT_FAILURE;
        }
        strlcpy(cmd_buffer, cmd[0], cmd_length);
        for (j = 1; cmd[j] != NULL; j++) {
            strlcat(cmd_buffer, " ", cmd_length);
            if (strpbrk(cmd[j], " \t\\{}<>()|&;") != NULL) {
                strlcat(cmd_buffer, "'", cmd_length);
                strlcat(cmd_buffer, cmd[j], cmd_length);    
                strlcat(cmd_buffer, "'", cmd_length);
            }
            else {
                strlcat(cmd_buffer, cmd[j], cmd_length);
            }
        }
        wordexp_t output;
        int status = wordexp(cmd_buffer, &output, WRDE_NOCMD);
        free(cmd_buffer);
        if (status != 0) {
            perror("wordexp failed");
            if (cmd_in != STDIN_FILENO) close(cmd_in);
            if (file_out != STDOUT_FILENO) close(file_out);
            return EXIT_FAILURE;
        }

        if (l->seq[i+1] != 0) {
            if (pipe(fds) == -1) {
                perror("Pipe failed");
                if (cmd_in != STDIN_FILENO) close(cmd_in);
                if (file_out != STDOUT_FILENO) close(file_out);
                wordfree(&output);
                return EXIT_FAILURE;
            }
            cmd_out = fds[1];
        }
        printf("seq[%d]: ", i);
        for (j=0; cmd[j]!=0; j++) {
            printf("'%s' ", cmd[j]);
        }
        printf("\n");
        // fflush(stdout); // Not needed since "\n" already flushes the buffer, but it likely will be needed for writing to files.
        if (output.we_wordc > 0) {
            execute_command(output.we_wordv[0], output.we_wordv, cmd_in, cmd_out, l->bg);
        }
        wordfree(&output);
        if (cmd_in != STDIN_FILENO) close(cmd_in);
        if (cmd_out != STDOUT_FILENO) close(cmd_out);
        // Check for a next command
        if (l->seq[i+1] != 0) {
            cmd_in = fds[0];
        }
    }
    return EXIT_SUCCESS;
}
