#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define ZSTDIO_SUFFIX ".setup.json"

static bool zstdio_is_target(const char *path) {
  if (path == NULL) {
    return false;
  }

  const size_t path_len = strlen(path);
  const size_t suffix_len = sizeof(ZSTDIO_SUFFIX) - 1;

  if (path_len < suffix_len) {
    return false;
  }

  return memcmp(path + path_len - suffix_len, ZSTDIO_SUFFIX, suffix_len) == 0;
}

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

static void zstdio_log_open(void) {
  static const char message[] = "[zstdio] intercepted setup file\n";

  /*
   * Avoid libc stdio and its potential reentrancy.
   * Logging is best-effort and must not change
   * application-visible errors.
   */
  (void)syscall(SYS_write, STDERR_FILENO, message, sizeof(message) - 1);
}

static int zstdio_openat(int dirfd, const char *path, int flags, mode_t mode) {
  /*
   * Linux-specific syscall forwarding, do not recurse through interposed libc
   * symbols.
   */
  const long result = syscall(SYS_openat, dirfd, path, flags, mode);
  const int saved_errno = errno;

  if (result >= 0 && zstdio_is_target(path)) {
    zstdio_log_open();
  }

  errno = saved_errno;
  return (int)result;
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
