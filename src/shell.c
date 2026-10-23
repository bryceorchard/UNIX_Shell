//
// Created by Paula on 2025-08-11.
//

/**
 * @file shell.c
 * @brief Reads a command line at the prompt and terminates the shell.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h> //for INT_MAX
#include <signal.h>

#include "shell.h"
#include "utils.h"
#include "jobs.h"

#define PROMPT_LINE_MAX 128

/**
 * Terminate the shell program
 * @param line
 */
void terminate(char *line) {
    if (line)
        free(line); //release memory allocated to line pointer

    printf("bye\n");
    exit(0);
}

/**
 * Read a line from standard input. While waiting for input, the shell is
 * "at the prompt": a background job finishing meanwhile redraws the prompt
 * below its completion message (see set_waiting_prompt()).
 * @param prompt A prompt to print in the shell before user input (ie: myshell>:)
 * @return User input read from standard input
 */
char* readline(const char *prompt)
{
    static char prompt_line[PROMPT_LINE_MAX]; // static: the SIGCHLD handler redraws it while we wait
    size_t buf_len = 16;
    char *buf = xmalloc(buf_len * sizeof(char));

    // Print the prompt and register it with SIGCHLD blocked, so a job that
    // finishes meanwhile cannot interleave its message with either step.
    sigset_t mask, prev;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    
    sigprocmask(SIG_BLOCK, &mask, &prev);
    snprintf(prompt_line, sizeof(prompt_line), "%s %s %s", KMAG, prompt, KRESET);
    fputs(prompt_line, stdout);
    fflush(stdout); // on screen before a completion message can erase it
    set_waiting_prompt(prompt_line);
    sigprocmask(SIG_SETMASK, &prev, NULL);

    char *first = fgets(buf, buf_len, stdin);
    set_waiting_prompt(NULL); // the user pressed Enter (or input ended)
    if (first == NULL) {
        free(buf);
        return NULL;
    }

    do {
        size_t l = strlen(buf);
        if ((l > 0) && (buf[l-1] == '\n')) { // if the buffer is large enough to contain the entire line, return it
            l--;
            buf[l] = 0;
            return buf;
        }
        if (buf_len >= (INT_MAX / 2)) memory_error();
        buf_len *= 2; // IMPORTANT! if the buffer is not large enough to contain the entire line, double its size and read more input
        buf = xrealloc(buf, buf_len * sizeof(char));
        if (fgets(buf + l, buf_len - l, stdin) == NULL) return buf;
    } while (1);
}


