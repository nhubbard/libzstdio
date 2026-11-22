#include "codec.h"

#include <zstd.h>

int zstdio_compress_frame(const void *source, size_t source_size,
                          void *destination, size_t capacity, size_t *written) {
  *written = 0;

  ZSTD_CCtx *context = ZSTD_createCCtx();
  if (context == NULL) {
    return -1;
  }

  size_t result = ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, 1);
  if (!ZSTD_isError(result)) {
    result =
        ZSTD_compress2(context, destination, capacity, source, source_size);
  }
  ZSTD_freeCCtx(context);

  if (ZSTD_isError(result)) {
    return -1;
  }
  *written = result;
  return 0;
}

int zstdio_decompress_frame(const void *source, size_t source_size,
                            void *destination, size_t capacity,
                            size_t *written) {
  *written = 0;

  const size_t frame_size = ZSTD_findFrameCompressedSize(source, source_size);
  if (ZSTD_isError(frame_size) || frame_size != source_size) {
    return -1;
  }

  const unsigned long long content_size =
      ZSTD_getFrameContentSize(source, source_size);
  if (content_size == ZSTD_CONTENTSIZE_ERROR ||
      content_size == ZSTD_CONTENTSIZE_UNKNOWN || content_size > capacity) {
    return -1;
  }

  const size_t result =
      ZSTD_decompress(destination, capacity, source, source_size);
  if (ZSTD_isError(result) || result != content_size) {
    return -1;
  }

  *written = result;
  return 0;
}