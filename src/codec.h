#ifndef ZSTDIO_CODEC_H
#define ZSTDIO_CODEC_H

#include <stddef.h>

/*
 * Supply non-null, non-overlapping buffers, even for empty input.
 * Return 0 on success or -1 on failure. On failure, *written is zero
 * and the caller must discard the destination contents.
 */
int zstdio_compress_frame(const void *source, size_t source_size,
                          void *destination, size_t capacity, size_t *written);
int zstdio_decompress_frame(const void *source, size_t source_size,
                            void *destination, size_t capacity,
                            size_t *written);

#endif