#ifndef ZSTDIO_FORWARD_H
#define ZSTDIO_FORWARD_H

#include "resolve.h" // IWYU pragma: keep

#include <errno.h>
#include <stddef.h>

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
