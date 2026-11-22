#include "resolve.h"

#include <errno.h>
#include <stdio.h>

typedef FILE *(*fopen_fn)(const char *, const char *);
typedef FILE *(*fdopen_fn)(int, const char *);
typedef size_t (*fread_fn)(void *, size_t, size_t, FILE *);
typedef size_t (*fwrite_fn)(const void *, size_t, size_t, FILE *);
typedef int (*fclose_fn)(FILE *);

static _Atomic(void *) fopen_symbol;
static _Atomic(void *) fdopen_symbol;
static _Atomic(void *) fread_symbol;
static _Atomic(void *) fwrite_symbol;
static _Atomic(void *) fclose_symbol;

FILE *fopen(const char *restrict path, const char *restrict mode) {
  __extension__ const fopen_fn next =
      (fopen_fn)zstdio_resolve_next(&fopen_symbol, "fopen");
  if (next == NULL) {
    errno = ENOSYS;
    return NULL;
  }
  return next(path, mode);
}

FILE *fdopen(int fd, const char *mode) {
  __extension__ const fdopen_fn next =
      (fdopen_fn)zstdio_resolve_next(&fdopen_symbol, "fdopen");
  if (next == NULL) {
    errno = ENOSYS;
    return NULL;
  }
  return next(fd, mode);
}

size_t fread(void *restrict buffer, size_t size, size_t count,
             FILE *restrict stream) {
  __extension__ const fread_fn next =
      (fread_fn)zstdio_resolve_next(&fread_symbol, "fread");
  if (next == NULL) {
    errno = ENOSYS;
    return 0;
  }
  return next(buffer, size, count, stream);
}

size_t fwrite(const void *restrict buffer, size_t size, size_t count,
              FILE *restrict stream) {
  __extension__ const fwrite_fn next =
      (fwrite_fn)zstdio_resolve_next(&fwrite_symbol, "fwrite");
  if (next == NULL) {
    errno = ENOSYS;
    return 0;
  }
  return next(buffer, size, count, stream);
}

int fclose(FILE *stream) {
  __extension__ const fclose_fn next =
      (fclose_fn)zstdio_resolve_next(&fclose_symbol, "fclose");
  if (next == NULL) {
    errno = ENOSYS;
    return EOF;
  }
  return next(stream);
}