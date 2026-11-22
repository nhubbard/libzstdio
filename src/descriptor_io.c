#include <dlfcn.h>
#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "resolve.h"

typedef ssize_t (*read_fn)(int, void *, size_t);
typedef ssize_t (*write_fn)(int, const void *, size_t);
typedef int (*close_fn)(int);
typedef off_t (*lseek_fn)(int, off_t, int);
typedef off64_t (*lseek64_fn)(int, off64_t, int);
typedef int (*fflush_fn)(FILE *);

static _Atomic(void *) read_symbol;
static _Atomic(void *) write_symbol;
static _Atomic(void *) close_symbol;
static _Atomic(void *) lseek_symbol;
static _Atomic(void *) lseek64_symbol;
static _Atomic(void *) fflush_symbol;

// One guard covers reentry into any of these wrappers during lookup.
static _Thread_local int resolving;

void *zstdio_resolve_next(_Atomic(void *) *slot, const char *name) {
  void *symbol = atomic_load_explicit(slot, memory_order_acquire);
  if (symbol != NULL)
    return symbol;
  if (resolving)
    return NULL;
  const int saved_errno = errno;
  resolving = 1;
  symbol = dlsym(RTLD_NEXT, name);

  if (symbol != NULL) {
    void *expected = NULL;
    if (!atomic_compare_exchange_strong_explicit(slot, &expected, symbol,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
      symbol = expected;
    }
  } else {
    symbol = atomic_load_explicit(slot, memory_order_acquire);
  }
  errno = saved_errno;
  resolving = 0;
  return symbol;
}

ssize_t read(int fd, void *buffer, size_t count) {
  if (resolving) {
    return (ssize_t)syscall(SYS_read, fd, buffer, count);
  }

  // POSIX supports converting a dlsym result to a function pointer.
  __extension__ const read_fn next =
      (read_fn)zstdio_resolve_next(&read_symbol, "read");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }

  return next(fd, buffer, count);
}

ssize_t write(int fd, const void *buffer, size_t count) {
  if (resolving) {
    return (ssize_t)syscall(SYS_write, fd, buffer, count);
  }

  __extension__ const write_fn next =
      (write_fn)zstdio_resolve_next(&write_symbol, "write");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }

  return next(fd, buffer, count);
}

int close(int fd) {
  if (resolving) {
    return (int)syscall(SYS_close, fd);
  }

  __extension__ const close_fn next =
      (close_fn)zstdio_resolve_next(&close_symbol, "close");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }

  return next(fd);
}

off_t lseek(int fd, off_t offset, int whence) {
  __extension__ const lseek_fn next =
      (lseek_fn)zstdio_resolve_next(&lseek_symbol, "lseek");
  if (next == NULL) {
    errno = ENOSYS;
    return (off_t)-1;
  }
  return next(fd, offset, whence);
}

off64_t lseek64(int fd, off64_t offset, int whence) {
  __extension__ const lseek64_fn next =
      (lseek64_fn)zstdio_resolve_next(&lseek64_symbol, "lseek64");
  if (next == NULL) {
    errno = ENOSYS;
    return (off64_t)-1;
  }
  return next(fd, offset, whence);
}

int fflush(FILE *stream) {
  __extension__ const fflush_fn next =
      (fflush_fn)zstdio_resolve_next(&fflush_symbol, "fflush");
  if (next == NULL) {
    errno = ENOSYS;
    return EOF;
  }
  return next(stream);
}