//
// Created by Paula on 2024-07-27.
//

#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

void memory_error(void)
{
    errno = ENOMEM;
    perror(0);
    exit(1);
}

void *xmalloc(size_t size)
{
    void *p = malloc(size);
    if (!p) memory_error();
    return p;
}

void *xrealloc(void *ptr, size_t size)
{
    void *p = realloc(ptr, size);
    if (!p) memory_error();
    return p;
}
