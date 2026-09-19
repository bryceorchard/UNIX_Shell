//
// Created by Paula on 2025-08-11.
//

#ifndef EXECUTOR_H
#define EXECUTOR_H

#endif //EXECUTOR_H

#include "parser.h"

int execute_command(char* cmd, char** args, int in, int out, int bg);

int execute(struct cmdline *l);
