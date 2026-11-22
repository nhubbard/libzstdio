#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void) {
  char path[] = "zstdio-mapping-XXXXXX";
  const unsigned char expected = 0xa5;
  void *first = MAP_FAILED;
  void *second = MAP_FAILED;
  int status = EXIT_FAILURE;

  int fd = mkstemp(path);
  if (fd < 0) {
    perror("mkstemp");
    return EXIT_FAILURE;
  }

  if (unlink(path) != 0 || write(fd, &expected, 1) != 1) {
    perror("prepare mapping file");
    goto cleanup;
  }

  first = mmap(NULL, 1, PROT_READ, MAP_PRIVATE, fd, 0);
  if (first == MAP_FAILED) {
    perror("mmap");
    goto cleanup;
  }

  second = mmap64(NULL, 1, PROT_READ, MAP_PRIVATE, fd, 0);
  if (second == MAP_FAILED) {
    perror("mmap64");
    goto cleanup;
  }

  const int close_result = close(fd);
  fd = -1;
  if (close_result != 0) {
    perror("close");
    goto cleanup;
  }

  if (*(const unsigned char *)first != expected ||
      *(const unsigned char *)second != expected) {
    fputs("expected both mappings to survive close and expose the byte\n",
          stderr);
    goto cleanup;
  }

  if (munmap(first, 0) != -1 || errno != EINVAL) {
    fputs("munmap: expected EINVAL for zero length\n", stderr);
    goto cleanup;
  }

  status = EXIT_SUCCESS;

cleanup:
  if (second != MAP_FAILED && munmap(second, 1) != 0) {
    perror("munmap second");
    status = EXIT_FAILURE;
  }
  if (first != MAP_FAILED && munmap(first, 1) != 0) {
    perror("munmap first");
    status = EXIT_FAILURE;
  }
  if (fd >= 0 && close(fd) != 0) {
    perror("close");
    status = EXIT_FAILURE;
  }
  return status;
}