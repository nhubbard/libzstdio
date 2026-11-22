#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
  char path[] = "zstdio-stream-XXXXXX";
  const unsigned char expected[] = {0x5a, 0x00, 0x80, 0xff};
  unsigned char actual[sizeof(expected) + 1];
  int status = EXIT_FAILURE;
  FILE *stream = NULL;

  const int fd = mkstemp(path);
  if (fd < 0) {
    perror("mkstemp");
    return EXIT_FAILURE;
  }

  stream = fdopen(fd, "wb");
  if (stream == NULL) {
    perror("fdopen");
    (void)close(fd);
    goto cleanup;
  }

  if (fwrite(expected, 2, 2, stream) != 2) {
    fputs("fwrite: expected two complete elements\n", stderr);
    goto cleanup;
  }

  if (fflush(stream) != 0) {
    perror("fflush writer");
    goto cleanup;
  }

  if (pread(fd, actual, sizeof(expected), 0) != (ssize_t)sizeof(expected) ||
      memcmp(actual, expected, sizeof(expected)) != 0) {
    fputs("fflush: expected matching bytes before fclose\n", stderr);
    goto cleanup;
  }

  if (fflush(NULL) != 0) {
    perror("fflush all streams");
    goto cleanup;
  }

  const int close_result = fclose(stream);
  stream = NULL;
  if (close_result != 0) {
    perror("fclose writer");
    goto cleanup;
  }

  if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
    fputs("fclose: expected fdopen's descriptor to be closed\n", stderr);
    goto cleanup;
  }

  stream = fopen(path, "rb");
  if (stream == NULL) {
    perror("fopen reader");
    goto cleanup;
  }

  if (fread(actual, 1, sizeof(actual), stream) != sizeof(expected) ||
      memcmp(actual, expected, sizeof(expected)) != 0 || !feof(stream) ||
      ferror(stream)) {
    fputs("fread: expected matching bytes followed by EOF\n", stderr);
    goto cleanup;
  }

  if (unlink(path) != 0) {
    perror("unlink");
    goto cleanup;
  }

  const int read_close_result = fclose(stream);
  stream = NULL;
  if (read_close_result != 0) {
    perror("fclose reader");
    return EXIT_FAILURE;
  }

  stream = fopen(path, "rb");
  if (stream != NULL || errno != ENOENT) {
    fputs("fopen: expected ENOENT after unlink\n", stderr);
    goto cleanup;
  }

  return EXIT_SUCCESS;

cleanup:
  if (stream != NULL) {
    (void)fclose(stream);
  }
  (void)unlink(path);
  return status;
}