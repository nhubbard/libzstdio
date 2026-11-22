#include "resolve.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/mman.h>

typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);
typedef void *(*mmap64_fn)(void *, size_t, int, int, int, off64_t);
typedef int (*munmap_fn)(void *, size_t);

static _Atomic(void *) mmap_symbol;
static _Atomic(void *) mmap64_symbol;
static _Atomic(void *) munmap_symbol;

void *mmap(void *address, size_t length, int protection, int flags, int fd,
           off_t offset) {
  __extension__ const mmap_fn next =
      (mmap_fn)zstdio_resolve_next(&mmap_symbol, "mmap");
  if (next == NULL) {
    errno = ENOSYS;
    return MAP_FAILED;
  }
  return next(address, length, protection, flags, fd, offset);
}

void *mmap64(void *address, size_t length, int protection, int flags, int fd,
             off64_t offset) {
  __extension__ const mmap64_fn next =
      (mmap64_fn)zstdio_resolve_next(&mmap64_symbol, "mmap64");
  if (next == NULL) {
    errno = ENOSYS;
    return MAP_FAILED;
  }
  return next(address, length, protection, flags, fd, offset);
}

int munmap(void *address, size_t length) {
  __extension__ const munmap_fn next =
      (munmap_fn)zstdio_resolve_next(&munmap_symbol, "munmap");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(address, length);
}