#include "forward.h"

#include <stdio.h>

FORWARD(FILE *, fopen, (const char *restrict path, const char *restrict mode),
        (path, mode), NULL)

FORWARD(FILE *, fdopen, (int fd, const char *mode), (fd, mode), NULL)

FORWARD(size_t, fread,
        (void *restrict buffer, size_t size, size_t count,
         FILE *restrict stream),
        (buffer, size, count, stream), 0)

FORWARD(size_t, fwrite,
        (const void *restrict buffer, size_t size, size_t count,
         FILE *restrict stream),
        (buffer, size, count, stream), 0)

FORWARD(int, fclose, (FILE * stream), (stream), EOF)