#include "codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  const unsigned char source[] = {0x00, 0x80, 0xff, 0x41};
  unsigned char frame[128];
  unsigned char restored[sizeof(source)];
  size_t frame_size = 0;
  size_t restored_size = 0;

  if (zstdio_compress_frame(source, sizeof(source), frame, sizeof(frame) - 1,
                            &frame_size) != 0 ||
      zstdio_decompress_frame(frame, frame_size, restored, sizeof(restored),
                              &restored_size) != 0 ||
      restored_size != sizeof(source) ||
      memcmp(source, restored, sizeof(source)) != 0) {
    fputs("codec: binary round trip failed\n", stderr);
    return EXIT_FAILURE;
  }

  frame[frame_size] = 0;
  if (zstdio_decompress_frame(frame, frame_size + 1, restored, sizeof(restored),
                              &restored_size) != -1 ||
      zstdio_decompress_frame(frame, frame_size - 1, restored, sizeof(restored),
                              &restored_size) != -1 ||
      zstdio_decompress_frame(frame, frame_size, restored, sizeof(restored) - 1,
                              &restored_size) != -1) {
    fputs("codec: expected rejection of trailing bytes, truncation, or a "
          "short output buffer\n",
          stderr);
    return EXIT_FAILURE;
  }

  frame[frame_size - 1] ^= 1;
  if (zstdio_decompress_frame(frame, frame_size, restored, sizeof(restored),
                              &restored_size) != -1 ||
      restored_size != 0) {
    fputs("codec: expected checksum failure\n", stderr);
    return EXIT_FAILURE;
  }

  if (zstdio_compress_frame(source, 0, frame, sizeof(frame), &frame_size) !=
          0 ||
      zstdio_decompress_frame(frame, frame_size, restored, 0, &restored_size) !=
          0 ||
      restored_size != 0) {
    fputs("codec: empty round trip failed\n", stderr);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}