#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <wordexp.h>

#include "executor.h"
#include "parser.h"
#include "jobs.h"

#define MAX_ARG_LENGTH 1024

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
 * @brief Appends arg to buf wrapped in single quotes, so wordexp() takes it
 *        literally. A ' cannot appear inside single quotes, so each one is
 *        written as '\'' (close the quotes, an escaped ', reopen the quotes).
 * @param buf  NUL-terminated string to append to.
 * @param size Total size of buf in bytes.
 * @param arg  Argument to append.
 */
static void append_single_quoted(char *buf, size_t size, const char *arg) {
    strlcat(buf, "'", size);
    for (const char *c = arg; *c != '\0'; c++) {
        if (*c == '\'') {
            strlcat(buf, "'\\''", size);
        }
        else {
            char ch[2] = { *c, '\0' };
            strlcat(buf, ch, size);
        }
    }
    strlcat(buf, "'", size);
}

/**
 * @brief Describes a wordexp() error code. wordexp() returns its error
 *        instead of setting errno, so perror() cannot be used for it.
 * @param code Non-zero value returned by wordexp().
 * @return A static, human-readable message.
 */
static const char *wordexp_error_message(int code) {
    switch (code) {
        case WRDE_BADCHAR: return "illegal character (newline, |, &, ;, <, >, (, ), {, })";
        case WRDE_CMDSUB:  return "command substitution is not supported";
        case WRDE_NOSPACE: return "out of memory";
        case WRDE_SYNTAX:  return "syntax error (e.g. unbalanced quotes)";
        default:           return "unknown error";
    }
}

/**
 * @brief Writes the command line of l as the user typed it (quotes removed),
 *        e.g. "cat < in.txt | sort > out.txt", for the job listing.
 * @param l    Parsed command line.
 * @param buf  Destination, always NUL-terminated (truncated if too small).
 * @param size Size of buf in bytes.
 */
static void format_command_line(const struct cmdline *l, char *buf, size_t size) {
    // snprintf returns the length it wanted to write, so once len reaches
    // size the text has been truncated and nothing more is appended.
    size_t len = 0;
    buf[0] = '\0';
    for (int i = 0; l->seq[i] != NULL && len < size; i++) {
        for (int j = 0; l->seq[i][j] != NULL && len < size; j++) {
            const char *separator = (j > 0) ? " " : (i > 0) ? " | " : "";
            len += (size_t) snprintf(buf + len, size - len, "%s%s", separator, l->seq[i][j]);
        }
        if (i == 0 && l->in != NULL && len < size) {
            len += (size_t) snprintf(buf + len, size - len, " < %s", l->in);
        }
    }
    if (l->out != NULL && len < size) {
        snprintf(buf + len, size - len, " > %s", l->out);
    }
}

/**
 * @brief Executes a single, simple command in another process: handles the
 *        `jobs` builtin, forks, wires up stdin/stdout, execs the program,
 *        and waits for foreground ones when asked to. Background children
 *        are recorded in the job table by execute(), once per pipeline.
 * @param cmd  Program to run (looked up in PATH).
 * @param args NULL-terminated argument vector, args[0] == cmd.
 * @param in   File descriptor the command reads from (STDIN_FILENO if none).
 * @param out  File descriptor the command writes to (STDOUT_FILENO if none).
 * @param bg   Non-zero to run in the background (never waited on).
 * @param wait_last Non-zero to wait for the last pipe command before returning.
 *             execute() passes 0 for every command of a pipeline but the last,
 *             so the whole pipeline is running before anything is waited on.
 * @return The child's pid, 0 if the command was a builtin run in the shell
 *         itself, or -1 if fork failed.
 */
pid_t execute_command(char* cmd, char** args, int in, int out, int bg, int wait_last) {

    sigset_t mask, prev;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

    if (strcmp(args[0], "jobs") == 0) {
        sigprocmask(SIG_BLOCK, &mask, &prev); // the handler must not edit the table mid-listing
        list_jobs();
        sigprocmask(SIG_SETMASK, &prev, NULL); // Restore previous signal mask
        return 0; // nothing to wait for
    }

    // Anything still in stdout's buffer would be copied into the child and
    // printed a second time if the child flushed it, so empty it first.
    fflush(stdout);
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return -1;
    }
    else if (pid == 0) { // child
        // execute() blocks SIGCHLD while it starts a background job; don't
        // give a blocked SIGCHLD to the program.
        sigprocmask(SIG_UNBLOCK, &mask, NULL);
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
        _exit(EXIT_FAILURE); // not exit(): it would flush the shell's stdio buffers again
    }

    // parent
    if (bg) {
        printf("child background process pid: %d\n", (int) pid);
    }
    else if (wait_last) {
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
 * A background command line is recorded as a single job, whatever the
 * number of commands in its pipeline.
 *
 * @param l A pointer to a cmdline structure containing the parsed command.
 * @return 0 on success, or a non-zero value on error.
 */
int execute(struct cmdline *l){
    int i,j;
    int cmd_in = STDIN_FILENO;
    int file_out = STDOUT_FILENO;

    if (l->in) {
        cmd_in = open(l->in, O_RDONLY);
        if (cmd_in == -1) {
            perror("Input file open failed");
            return EXIT_FAILURE;
        }
        set_cloexec(cmd_in);
    }
    if (l->out) {
        file_out = open(l->out, O_WRONLY | O_CREAT | O_TRUNC, 0644); // O_TRUNC clears previous file contents
        if (file_out == -1) {
            perror("Output file open failed");
            if (cmd_in != STDIN_FILENO) close(cmd_in);
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

    // Background: block SIGCHLD until the whole pipeline is in the job table,
    // so the handler cannot reap one of its processes before the job exists.
    sigset_t sigchld_mask, prev_mask;
    sigemptyset(&sigchld_mask);
    sigaddset(&sigchld_mask, SIGCHLD);
    if (l->bg) sigprocmask(SIG_BLOCK, &sigchld_mask, &prev_mask);

    /* Start each command of the pipe */
    for (i=0; l->seq[i]!=0; i++) {
        char **cmd = l->seq[i];
        int cmd_out = STDOUT_FILENO;
        if (l->seq[i+1] == 0) cmd_out = file_out; // if no further pipes remain, set output to outfile (or default stdout)
        int fds[2];

        /* Expand wildcards and environment variables: rebuild the command as
         * one string (quoting arguments with special characters) for wordexp */
        size_t cmd_length = 0;
        for (j = 0; cmd[j] != NULL; j++) {
            // space (1) + quotes (2) + each char may be a ' written as '\'' (4)
            cmd_length += 4 * strnlen(cmd[j], MAX_ARG_LENGTH) + 3;
        }
        cmd_length++; // + \0 (1)
        char *cmd_buffer = malloc(cmd_length);
        if (cmd_buffer == NULL) {
            perror("malloc failed");
            if (cmd_in != STDIN_FILENO) close(cmd_in);
            if (file_out != STDOUT_FILENO) close(file_out);
            status = EXIT_FAILURE;
            break; // still wait for the commands already started
        }
        strlcpy(cmd_buffer, cmd[0], cmd_length);
        for (j = 1; cmd[j] != NULL; j++) {
            strlcat(cmd_buffer, " ", cmd_length);
            if (strpbrk(cmd[j], " \t\\{}<>()|&;'\"") != NULL) {
                append_single_quoted(cmd_buffer, cmd_length, cmd[j]);
            }
            else {
                strlcat(cmd_buffer, cmd[j], cmd_length);
            }
        }
        wordexp_t output;
        int expand_status = wordexp(cmd_buffer, &output, WRDE_NOCMD);
        free(cmd_buffer);
        if (expand_status != 0) {
            fprintf(stderr, "%s: cannot expand arguments: %s\n",
                    cmd[0], wordexp_error_message(expand_status));
            if (expand_status == WRDE_NOSPACE) wordfree(&output); // may be partly allocated
            if (cmd_in != STDIN_FILENO) close(cmd_in);
            if (file_out != STDOUT_FILENO) close(file_out);
            status = EXIT_FAILURE;
            break; // still wait for the commands already started
        }

        if (l->seq[i+1] != 0) {
            if (pipe(fds) == -1) {
                perror("Pipe failed");
                if (cmd_in != STDIN_FILENO) close(cmd_in);
                if (file_out != STDOUT_FILENO) close(file_out);
                wordfree(&output);
                status = EXIT_FAILURE;
                break; // still wait for the commands already started
            }
            set_cloexec(fds[0]);
            set_cloexec(fds[1]);
            cmd_out = fds[1];
        }

        /* start execution */
        int is_last = (l->seq[i+1] == 0);
        pids[i] = 0; // nothing to wait for unless a child is started below
        if (output.we_wordc > 0) { // a command can expand to zero words
            pids[i] = execute_command(output.we_wordv[0], output.we_wordv, cmd_in, cmd_out, l->bg, is_last);
            if (pids[i] < 0) status = EXIT_FAILURE;
            if (is_last && !l->bg) pids[i] = 0; // already waited on by the foreground
        }
        wordfree(&output);
        // The children have their own copies now; the shell must drop its
        // copies so each reader sees EOF once its writer exits.
        if (cmd_in != STDIN_FILENO) close(cmd_in);
        if (cmd_out != STDOUT_FILENO) close(cmd_out);
        // Check for a next command
        if (l->seq[i+1] != 0) {
            cmd_in = fds[0];
        }
    }

    /* Background: record the whole pipeline as one job */
    if (l->bg) {
        char cmdline[JOB_CMD_MAX];
        format_command_line(l, cmdline, sizeof(cmdline));
        if (add_job(pids, i, cmdline) == -1) {
            fprintf(stderr, "cannot track job (table of %d jobs full, or more than %d processes):"
                            " it will not be listed or timed\n", MAX_JOBS, JOB_PIDS_MAX);
        }
        sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    }
    /* Foreground: wait for the earlier commands of the pipeline too */
    else {
        for (j = 0; j < i; j++) {
            if (pids[j] > 0) waitpid(pids[j], NULL, 0); // ECHILD if the handler reaped it which is fine
        }
    }
    free(pids);
    return status;
}
