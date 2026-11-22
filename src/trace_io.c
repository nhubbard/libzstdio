#include "forward.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>
#include <unistd.h>

/* glibc fortified entry points, also needed without _FORTIFY_SOURCE. */
extern int __open_2(const char *, int);
extern int __open64_2(const char *, int);
extern ssize_t __read_chk(int, void *, size_t, size_t);
extern size_t __fread_chk(void *, size_t, size_t, size_t, FILE *);

FORWARD(int, rename, (const char *old_path, const char *new_path),
        (old_path, new_path), -1)
FORWARD(int, unlink, (const char *path), (path), -1)

FORWARD(int, __open_2, (const char *path, int flags), (path, flags), -1)
FORWARD(int, __open64_2, (const char *path, int flags), (path, flags), -1)
FORWARD(FILE *, fopen64, (const char *path, const char *mode), (path, mode),
        NULL)
FORWARD(FILE *, freopen64, (const char *path, const char *mode, FILE *stream),
        (path, mode, stream), NULL)
FORWARD(ssize_t, __read_chk,
        (int fd, void *buffer, size_t count, size_t buffer_size),
        (fd, buffer, count, buffer_size), -1)
FORWARD(size_t, __fread_chk,
        (void *buffer, size_t buffer_size, size_t size, size_t count,
         FILE *stream),
        (buffer, buffer_size, size, count, stream), 0)

FORWARD(int, stat64, (const char *path, struct stat64 *info), (path, info), -1)
FORWARD(int, lstat64, (const char *path, struct stat64 *info), (path, info), -1)
FORWARD(int, fstat64, (int fd, struct stat64 *info), (fd, info), -1)

FORWARD(int, fseek, (FILE * stream, long offset, int whence),
        (stream, offset, whence), -1)
FORWARD(long, ftell, (FILE * stream), (stream), -1L)

FORWARD(DIR *, opendir, (const char *path), (path), NULL)
FORWARD(int, closedir, (DIR * dir), (dir), -1)
FORWARD(struct dirent *, readdir, (DIR * dir), (dir), NULL)
FORWARD(struct dirent64 *, readdir64, (DIR * dir), (dir), NULL)
FORWARD(ssize_t, readlink, (const char *path, char *buffer, size_t count),
        (path, buffer, count), -1)
FORWARD(int, statfs64, (const char *path, struct statfs64 *info), (path, info),
        -1)
FORWARD(int, statvfs64, (const char *path, struct statvfs64 *info),
        (path, info), -1)

FORWARD(int, execve, (const char *path, char *const argv[], char *const envp[]),
        (path, argv, envp), -1)
FORWARD(int, execvp, (const char *file, char *const argv[]), (file, argv), -1)

#ifdef ZSTDIO_X86_64_VARIADIC

/* Assembly forwards the original argument list, including stack arguments. */
#define VARIADIC_TARGET(name)                                                  \
  static _Atomic(void *) name##_symbol;                                        \
  __attribute__((visibility("hidden"))) void *zstdio_target_##name(void);      \
  void *zstdio_target_##name(void) {                                           \
    void *const next = zstdio_resolve_next(&name##_symbol, #name);             \
    if (next == NULL) {                                                        \
      errno = ENOSYS;                                                          \
    }                                                                          \
    return next;                                                               \
  }

VARIADIC_TARGET(open64)
VARIADIC_TARGET(fcntl64)
VARIADIC_TARGET(execl)
VARIADIC_TARGET(execlp)

#undef VARIADIC_TARGET
#endif

/* Keep loader lookups off the normal post-fork exec path. */
__attribute__((constructor)) static void prime_exec_symbols(void) {
  const int saved_errno = errno;
  (void)zstdio_resolve_next(&execve_symbol, "execve");
  (void)zstdio_resolve_next(&execvp_symbol, "execvp");
#ifdef ZSTDIO_X86_64_VARIADIC
  (void)zstdio_target_execl();
  (void)zstdio_target_execlp();
#endif
  errno = saved_errno;
}
