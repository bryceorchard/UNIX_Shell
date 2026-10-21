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
 * @brief Marks a file descriptor close-on-exec, so a child keeps no copy of it
 *        once execvp() succeeds. The copies dup2() puts on stdin/stdout are not
 *        affected (dup2 clears the flag on the new descriptor).
 * @param fd Descriptor the shell opened (pipe end or redirection file).
 */
static void set_cloexec(int fd) {
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) perror("fcntl FD_CLOEXEC failed");
}

/**
 * @brief Executes a single, simple command in another process: handles the
 *        `jobs` builtin, forks, wires up stdin/stdout, execs the program,
 *        registers background children in the job table, and waits for
 *        foreground ones when asked to.
 * @param cmd  Program to run (looked up in PATH).
 * @param args NULL-terminated argument vector, args[0] == cmd.
 * @param in   File descriptor the command reads from (STDIN_FILENO if none).
 * @param out  File descriptor the command writes to (STDOUT_FILENO if none).
 * @param bg   Non-zero to run in the background (never waited on).
 * @param wait Non-zero to wait for a foreground command before returning.
 *             execute() passes 0 for every command of a pipeline but the last,
 *             so the whole pipeline is running before anything is waited on.
 * @return The child's pid, 0 if the command was a builtin run in the shell
 *         itself, or -1 if fork failed.
 */
pid_t execute_command(char* cmd, char** args, int in, int out, int bg, int wait) {

    sigset_t mask, prev;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

    if (strcmp(args[0], "jobs") == 0) {
        sigprocmask(SIG_BLOCK, &mask, &prev); // the handler must not edit the table mid-listing
        list_jobs();
        sigprocmask(SIG_SETMASK, &prev, NULL); // Restore previous signal mask
        return 0; // nothing to wait for
    }

    // Block SIGCHLD so a child that exits instantly cannot be reaped by the
    // handler before add_job() has recorded it.
    sigprocmask(SIG_BLOCK, &mask, &prev);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        sigprocmask(SIG_SETMASK, &prev, NULL);
        return -1;
    }
    else if (pid == 0) { // child
        sigprocmask(SIG_SETMASK, &prev, NULL); // don't give a blocked SIGCHLD to the program
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
    }

    // parent
    if (bg) {
        printf("child background process pid: %d\n", (int) pid);
        add_job(pid, cmd); // Add the background job to the job list
    }
    sigprocmask(SIG_SETMASK, &prev, NULL); // restore the previous mask
    if (!bg && wait) {
        waitpid(pid, NULL, 0); // ECHILD if the SIGCHLD handler already reaped it, which is fine
    }
    return pid;
}

/**
 * @brief Executes a command pipeline.
 *
 * Every command is started before any is waited on: waiting for each in turn
 * deadlocks as soon as a command writes more than the pipe buffer holds, since
 * its reader has not been started yet. So only the last command is waited on
 * as it is launched; the earlier ones are waited on once it has finished.
 * All descriptors the shell opens are close-on-exec, so a child keeps only its
 * own stdin/stdout; in particular a writer keeps no copy of its pipe's read
 * end, so it gets SIGPIPE when the reader exits early (e.g. `yes | head -n 1`).
 *
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
        set_cloexec(cmd_in);
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
        set_cloexec(file_out);
    }

    // conditionally allocate an array to hold the pids of the commands in the pipeline
    int count = 0;
    while (l->seq[count] != 0) count++;
    pid_t *pids = calloc(count > 0 ? count : 1, sizeof(pid_t));
    if (pids == NULL) {
        perror("calloc failed");
        if (cmd_in != STDIN_FILENO) close(cmd_in);
        if (file_out != STDOUT_FILENO) close(file_out);
        return EXIT_FAILURE;
    }
    int status = EXIT_SUCCESS;

    /* Start each command of the pipe */
    for (i=0; l->seq[i]!=0; i++) {
        char **cmd = l->seq[i];
        int cmd_out = STDOUT_FILENO;
        if (l->seq[i+1] == 0) cmd_out = file_out;
        int fds[2];

        if (l->seq[i+1] != 0) {
            if (pipe(fds) == -1) {
                perror("Pipe failed");
                if (cmd_in != STDIN_FILENO) close(cmd_in);
                if (file_out != STDOUT_FILENO) close(file_out);
                status = EXIT_FAILURE;
                break; // still wait for the commands already started
            }
            set_cloexec(fds[0]);
            set_cloexec(fds[1]);
            cmd_out = fds[1];
        }

        int is_last = (l->seq[i+1] == 0);
        pids[i] = execute_command(cmd[0], cmd, cmd_in, cmd_out, l->bg, is_last);
        if (pids[i] < 0) status = EXIT_FAILURE;
        if (is_last) pids[i] = 0; // already waited on
        // The children have their own copies now; the shell must drop its
        // copies so each reader sees EOF once its writer exits.
        if (cmd_in != STDIN_FILENO) close(cmd_in);
        if (cmd_out != STDOUT_FILENO) close(cmd_out);
        // Check for a next command
        if (l->seq[i+1] != 0) {
            cmd_in = fds[0];
        }
    }

    /* Foreground: wait for the earlier commands of the pipeline too */
    if (!l->bg) {
        for (j = 0; j < i; j++) {
            if (pids[j] > 0) waitpid(pids[j], NULL, 0); // ECHILD if the handler reaped it which is fine
        }
    }
    free(pids);
    return status;
}
