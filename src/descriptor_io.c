#include <dlfcn.h>
#include <errno.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef ssize_t (*read_fn)(int, void *, size_t);
typedef ssize_t (*write_fn)(int, const void *, size_t);
typedef int (*close_fn)(int);

// One guard covers reentry into any of these wrappers during lookup.
static _Thread_local int resolving;

static void *resolve_next(const char *name) {
  const int saved_errno = errno;
  resolving = 1;
  void *const symbol = dlsym(RTLD_NEXT, name);
  resolving = 0;
  errno = saved_errno;
  return symbol;
}

ssize_t read(int fd, void *buffer, size_t count) {
  if (resolving) {
    return (ssize_t)syscall(SYS_read, fd, buffer, count);
  }

  // POSIX supports converting a dlsym result to a function pointer.
  __extension__ const read_fn next = (read_fn)resolve_next("read");
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

  __extension__ const write_fn next = (write_fn)resolve_next("write");
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

  __extension__ const close_fn next = (close_fn)resolve_next("close");
  if (next == NULL) {
    errno = ENOSYS;
    return -1;
  }

  return next(fd);
}