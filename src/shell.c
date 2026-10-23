//
// Created by Paula on 2025-08-11.
//
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h> //for INT_MAX

#include "shell.h"
#include "utils.h"

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
 * Read a line from standard input
 * @param prompt A prompt to print in the shell before user input (ie: myshell>:)
 * @return User input read from standard input
 */
char* readline(const char *prompt)
{
    size_t buf_len = 16;
    char *buf = xmalloc(buf_len * sizeof(char));

    printf("%s %s %s", KMAG, prompt, KRESET);
    if (fgets(buf, buf_len, stdin) == NULL) {
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


