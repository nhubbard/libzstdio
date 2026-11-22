#include "codec.h"
#include "decoded_fd.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

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

  const int fd = zstdio_open_decoded_fd(frame, frame_size, sizeof(source));
  if (fd < 0) {
    perror("open decoded frame");
    return EXIT_FAILURE;
  }

  memset(restored, 0, sizeof(restored));
  if (read(fd, restored, sizeof(restored)) != (ssize_t)sizeof(restored) ||
      memcmp(source, restored, sizeof(source)) != 0 ||
      write(fd, source, 1) != -1 || errno != EPERM) {
    fputs("decoded fd: expected original bytes and sealed writes\n", stderr);
    (void)close(fd);
    return EXIT_FAILURE;
  }

  void *mapping =
      mmap(NULL, sizeof(source), PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  if (mapping == MAP_FAILED) {
    perror("map decoded frame");
    (void)close(fd);
    return EXIT_FAILURE;
  }

  const int close_status = close(fd);
  const int match = memcmp(mapping, source, sizeof(source)) == 0;
  const int unmap_status = munmap(mapping, sizeof(source));
  if (close_status != 0 || !match || unmap_status != 0) {
    fputs("decoded fd: expected mapping to survive close\n", stderr);
    return EXIT_FAILURE;
  }

  const int bad_fd =
      zstdio_open_decoded_fd(frame, frame_size, sizeof(source) + 1);
  if (bad_fd != -1 || errno != EBADMSG) {
    fputs("decoded fd: expected rejection of a wrong logical size\n", stderr);
    if (bad_fd >= 0) {
      (void)close(bad_fd);
    }
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