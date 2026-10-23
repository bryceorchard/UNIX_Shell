//
// Created by Paula on 2024-07-27.
//

/**
 * @file utils.h
 * @brief Memory allocation helpers.
 */

#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

void memory_error(void);
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);

#endif //UTILS_H