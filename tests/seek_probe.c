#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
  char path[] = "zstdio-seek-XXXXXX";
  const off64_t large_offset = 4294967297LL;
  int status = EXIT_FAILURE;

  const int fd = mkstemp(path);
  if (fd < 0) {
    perror("mkstemp");
    return EXIT_FAILURE;
  }

  if (unlink(path) != 0) {
    perror("unlink");
    goto cleanup;
  }

  if (lseek(fd, 7, SEEK_SET) != 7 || lseek64(fd, 0, SEEK_CUR) != 7) {
    fputs("seek: expected both entry points to share the offset\n", stderr);
    goto cleanup;
  }

  if (lseek64(fd, large_offset, SEEK_SET) != large_offset ||
      lseek64(fd, -1, SEEK_CUR) != large_offset - 1) {
    fputs("lseek64: large offset failed\n", stderr);
    goto cleanup;
  }

  if (lseek(fd, 0, SEEK_END) != 0) {
    fputs("lseek: expected the file to remain empty\n", stderr);
    goto cleanup;
  }

  if (lseek(fd, -1, SEEK_SET) != (off_t)-1 || errno != EINVAL) {
    fputs("lseek: expected EINVAL for a negative absolute offset\n", stderr);
    goto cleanup;
  }

  if (lseek64(-1, 0, SEEK_SET) != (off64_t)-1 || errno != EBADF) {
    fputs("lseek64: expected EBADF for an invalid descriptor\n", stderr);
    goto cleanup;
  }

  status = EXIT_SUCCESS;

cleanup:
  if (close(fd) != 0) {
    perror("close");
    status = EXIT_FAILURE;
  }
  return status;
}