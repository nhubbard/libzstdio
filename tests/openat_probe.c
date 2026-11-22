#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
  char directory[] = "zstdio-openat-XXXXXX";
  const char *name = "probe.setup.json";
  int status = EXIT_FAILURE;
  int dirfd = -1;
  int fd = -1;
  bool created = false;
  struct stat info;
  char byte;

  if (mkdtemp(directory) == NULL) {
    perror("mkdtemp");
    return EXIT_FAILURE;
  }

  dirfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dirfd < 0) {
    perror("open directory");
    goto cleanup;
  }

  fd = openat(dirfd, name, O_RDONLY);
  if (fd != -1 || errno != ENOENT) {
    fputs("openat: expected ENOENT before creation\n", stderr);
    goto cleanup;
  }

  const mode_t old_mask = umask(0);
  fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL, (mode_t)0640);
  const int create_error = errno;
  umask(old_mask);
  if (fd < 0) {
    errno = create_error;
    perror("openat create");
    goto cleanup;
  }
  created = true;

  if (fstat(fd, &info) != 0 || (info.st_mode & 0777) != 0640) {
    fputs("openat: expected mode 0640\n", stderr);
    goto cleanup;
  }
  if (write(fd, "Z", 1) != 1) {
    fputs("openat: failed to write probe byte\n", stderr);
    goto cleanup;
  }
  const int close_result = close(fd);
  fd = -1;
  if (close_result != 0) {
    perror("close writer");
    goto cleanup;
  }

  fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL, (mode_t)0640);
  if (fd != -1 || errno != EEXIST) {
    fputs("openat: expected EEXIST for exclusive creation\n", stderr);
    goto cleanup;
    if (fd >= 0 && close(fd) != 0)
      status = EXIT_FAILURE;
    if (created && unlinkat(dirfd, name, 0) != 0)
      status = EXIT_FAILURE;
    if (dirfd >= 0 && close(dirfd) != 0)
      status = EXIT_FAILURE;
    if (rmdir(directory) != 0)
      status = EXIT_FAILURE;
    return status;
  }

  fd = openat(dirfd, name, O_RDONLY);
  if (fd < 0 || read(fd, &byte, 1) != 1 || byte != 'Z') {
    fputs("openat: failed to read back the probe byte\n", stderr);
    goto cleanup;
  }

  status = EXIT_SUCCESS;

cleanup:
  if (fd >= 0 && close(fd) != 0)
    status = EXIT_FAILURE;
  if (created && unlinkat(dirfd, name, 0) != 0)
    status = EXIT_FAILURE;
  if (dirfd >= 0 && close(dirfd) != 0)
    status = EXIT_FAILURE;
  if (rmdir(directory) != 0)
    status = EXIT_FAILURE;
  return status;
}