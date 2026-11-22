#ifndef ZSTDIO_RESOLVE_H
#define ZSTDIO_RESOLVE_H

#include <stdatomic.h>

/* Internal to libzstdio, including when linked with other preload libraries. */
__attribute__((visibility("hidden"))) void *
zstdio_resolve_next(_Atomic(void *) *slot, const char *name);

#endif