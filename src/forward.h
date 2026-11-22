#ifndef ZSTDIO_FORWARD_H
#define ZSTDIO_FORWARD_H

#include "resolve.h" // IWYU pragma: keep

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h> // IWYU pragma: keep
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <unistd.h>

#define FORWARD(type, name, parameters, arguments, failure)                    \
  static _Atomic(void *) name##_symbol;                                        \
  type name parameters {                                                       \
    __extension__ __typeof__(&name) const next =                               \
        (__typeof__(&name))zstdio_resolve_next(&name##_symbol, #name);         \
    if (next == NULL) {                                                        \
      errno = ENOSYS;                                                          \
      return failure;                                                          \
    }                                                                          \
    return next arguments;                                                     \
  }

#endif
