#include "decoded_fd.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum { MAX_INPUT_SIZE = 64 * 1024 * 1024 };

struct input {
  const void *bytes;
  size_t size;
};

static int map_input(const char *path, struct input *input) {
  static const unsigned char empty;
  struct stat info;

  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    perror(path);
    return -1;
  }

  if (fstat(fd, &info) != 0) {
    perror(path);
    (void)close(fd);
    return -1;
  }

  if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
      info.st_size > MAX_INPUT_SIZE) {
    fprintf(stderr, "%s: expected a regular file of at most 64 MiB\n", path);
    (void)close(fd);
    return -1;
  }

  input->size = (size_t)info.st_size;
  input->bytes = &empty;
  if (input->size != 0) {
    input->bytes = mmap(NULL, input->size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (input->bytes == MAP_FAILED) {
      perror(path);
      (void)close(fd);
      return -1;
    }
  }

  (void)close(fd);
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s ORIGINAL FRAME\n", argv[0]);
    return EXIT_FAILURE;
  }

  struct input original;
  struct input frame;
  if (map_input(argv[1], &original) != 0 || map_input(argv[2], &frame) != 0) {
    return EXIT_FAILURE;
  }

  const int fd = zstdio_open_decoded_fd(frame.bytes, frame.size, original.size);
  if (fd < 0) {
    perror(argv[2]);
    return EXIT_FAILURE;
  }

  struct stat info;
  if (fstat(fd, &info) != 0 || info.st_size != (off_t)original.size) {
    fprintf(stderr, "%s: incorrect decoded descriptor size\n", argv[2]);
    return EXIT_FAILURE;
  }

  unsigned char buffer[16384];
  const unsigned char *expected = original.bytes;
  size_t offset = 0;
  while (offset < original.size) {
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count <= 0 || (size_t)count > original.size - offset ||
        memcmp(buffer, expected + offset, (size_t)count) != 0) {
      fprintf(stderr, "%s: decoded bytes differ at offset %zu\n", argv[2],
              offset);
      return EXIT_FAILURE;
    }
    offset += (size_t)count;
  }

  if (read(fd, buffer, 1) != 0 || close(fd) != 0) {
    fprintf(stderr, "%s: expected EOF and successful close\n", argv[2]);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}