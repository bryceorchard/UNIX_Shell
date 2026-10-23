//
// Created by Paula on 2025-08-11.
//

/**
 * @file shell.h
 * @brief Prompt colours, line reading and shell termination.
 */

#ifndef SHELL_H
#define SHELL_H

#define KBLUE  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KRESET "\x1b[0m"

void terminate(char *line);

char* readline(const char *prompt);

#endif //SHELL_H