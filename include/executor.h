//
// Created by Paula on 2025-08-11.
//

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <sys/types.h> // for pid_t

#include "parser.h"

pid_t execute_command(char* cmd, char** args, int in, int out, int bg, int wait);

int execute(struct cmdline *l);

#endif //EXECUTOR_H