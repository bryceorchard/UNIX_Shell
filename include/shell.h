//
// Created by Paula on 2025-08-11.
//

#ifndef SHELL_H
#define SHELL_H

#endif //SHELL_H

#define KBLUE  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KRESET "\x1b[0m"

void terminate(char *line);

char* readline(const char *prompt);
