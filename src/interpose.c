#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static bool zstdio_requires_mode(int flags) {
  if ((flags & O_CREAT) != 0) {
    return true;
  }

#ifdef O_TMPFILE
  if ((flags & O_TMPFILE) == O_TMPFILE) {
    return true;
  }
#endif

  return false;
}

static int zstdio_openat(int dirfd, const char *path, int flags, mode_t mode) {
  return (int)syscall(SYS_openat, dirfd, path, flags, mode);
}

int open(const char *path, int flags, ...) {
  mode_t mode = 0;

  if (zstdio_requires_mode(flags)) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, mode_t);
    va_end(args);
  }

  return zstdio_openat(AT_FDCWD, path, flags, mode);
}

int openat(int dirfd, const char *path, int flags, ...) {
  mode_t mode = 0;

  if (zstdio_requires_mode(flags)) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, mode_t);
    va_end(args);
  }

  return zstdio_openat(dirfd, path, flags, mode);
}