#ifndef ZSTDIO_DECODED_FD_H
#define ZSTDIO_DECODED_FD_H

#include <stddef.h>

/*
 * Materialize one frame as a sealed, close-on-exec Linux file descriptor.
 * The caller supplies a non-null frame and a policy-bounded logical size.
 * The returned descriptor starts at offset zero and is owned by the caller.
 * Return -1 with errno on failure.
 */
int zstdio_open_decoded_fd(const void *frame, size_t frame_size,
                           size_t logical_size);

#endif