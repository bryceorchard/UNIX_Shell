//
// Created by Paula on 2025-08-11.
//

/**
 * @file executor.h
 * @brief Command execution: running a single command and a whole
 *        command line.
 */

#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"

int execute_command(char* cmd, char** args, int in, int out, int bg);

int execute(struct cmdline *l);

#endif //EXECUTOR_H