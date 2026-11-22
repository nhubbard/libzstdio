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
typedef int (*dup_fn)(int);
typedef int (*dup2_fn)(int, int);
typedef int (*ftruncate_fn)(int, off_t);
typedef int (*ftruncate64_fn)(int, off64_t);
typedef int (*sync_fd_fn)(int);
typedef ssize_t (*pread_fn)(int, void *, size_t, off_t);
typedef ssize_t (*pread64_fn)(int, void *, size_t, off64_t);
typedef ssize_t (*pwrite_fn)(int, const void *, size_t, off_t);
typedef ssize_t (*pwrite64_fn)(int, const void *, size_t, off64_t);

static _Atomic(void *) read_symbol;
static _Atomic(void *) write_symbol;
static _Atomic(void *) close_symbol;
static _Atomic(void *) lseek_symbol;
static _Atomic(void *) lseek64_symbol;
static _Atomic(void *) fflush_symbol;
static _Atomic(void *) dup_symbol;
static _Atomic(void *) dup2_symbol;
static _Atomic(void *) ftruncate_symbol;
static _Atomic(void *) ftruncate64_symbol;
static _Atomic(void *) fsync_symbol;
static _Atomic(void *) fdatasync_symbol;
static _Atomic(void *) pread_symbol;
static _Atomic(void *) pread64_symbol;
static _Atomic(void *) pwrite_symbol;
static _Atomic(void *) pwrite64_symbol;

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

int dup(int oldfd) {
  __extension__ const dup_fn next =
      (dup_fn)zstdio_resolve_next(&dup_symbol, "dup");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(oldfd);
}

int dup2(int oldfd, int newfd) {
  __extension__ const dup2_fn next =
      (dup2_fn)zstdio_resolve_next(&dup2_symbol, "dup2");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(oldfd, newfd);
}

int ftruncate(int fd, off_t length) {
  __extension__ const ftruncate_fn next =
      (ftruncate_fn)zstdio_resolve_next(&ftruncate_symbol, "ftruncate");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(fd, length);
}

int ftruncate64(int fd, off64_t length) {
  __extension__ const ftruncate64_fn next =
      (ftruncate64_fn)zstdio_resolve_next(&ftruncate64_symbol, "ftruncate64");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(fd, length);
}

int fsync(int fd) {
  __extension__ const sync_fd_fn next =
      (sync_fd_fn)zstdio_resolve_next(&fsync_symbol, "fsync");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(fd);
}

int fdatasync(int fd) {
  __extension__ const sync_fd_fn next =
      (sync_fd_fn)zstdio_resolve_next(&fdatasync_symbol, "fdatasync");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(fd);
}

ssize_t pread(int fd, void *buffer, size_t count, off_t offset) {
  __extension__ const pread_fn next =
      (pread_fn)zstdio_resolve_next(&pread_symbol, "pread");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(fd, buffer, count, offset);
}

ssize_t pread64(int fd, void *buffer, size_t count, off64_t offset) {
  __extension__ const pread64_fn next =
      (pread64_fn)zstdio_resolve_next(&pread64_symbol, "pread64");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(fd, buffer, count, offset);
}

ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset) {
  __extension__ const pwrite_fn next =
      (pwrite_fn)zstdio_resolve_next(&pwrite_symbol, "pwrite");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(fd, buffer, count, offset);
}

ssize_t pwrite64(int fd, const void *buffer, size_t count, off64_t offset) {
  __extension__ const pwrite64_fn next =
      (pwrite64_fn)zstdio_resolve_next(&pwrite64_symbol, "pwrite64");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }
  return next(fd, buffer, count, offset);
}