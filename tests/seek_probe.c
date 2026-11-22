#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int check_duplication(int fd) {
  int status = EXIT_FAILURE;
  const int duplicate = dup(fd);
  if (duplicate < 0) {
    perror("dup");
    return EXIT_FAILURE;
  }

  const int target = open("/dev/null", O_RDONLY | O_CLOEXEC);
  if (target < 0) {
    perror("open /dev/null");
    (void)close(duplicate);
    return EXIT_FAILURE;
  }

  if (lseek(duplicate, 19, SEEK_SET) != 19 || lseek(fd, 0, SEEK_CUR) != 19) {
    fputs("dup: expected a shared offset\n", stderr);
    goto cleanup;
  }

  if (dup2(-1, target) != -1 || errno != EBADF ||
      fcntl(target, F_GETFD) != FD_CLOEXEC) {
    fputs("dup2: expected an invalid source to leave the target open\n",
          stderr);
    goto cleanup;
  }

  if (dup2(target, target) != target || fcntl(target, F_GETFD) != FD_CLOEXEC) {
    fputs("dup2: expected a same-descriptor call to preserve flags\n", stderr);
    goto cleanup;
  }

  if (dup2(fd, target) != target || fcntl(target, F_GETFD) != 0 ||
      lseek(target, 23, SEEK_SET) != 23 ||
      lseek(duplicate, 0, SEEK_CUR) != 23) {
    fputs("dup2: expected replacement, cleared CLOEXEC, and a shared offset\n",
          stderr);
    goto cleanup;
  }

  status = EXIT_SUCCESS;

cleanup:
  if (close(target) != 0) {
    perror("close target");
    status = EXIT_FAILURE;
  }
  if (close(duplicate) != 0) {
    perror("close duplicate");
    status = EXIT_FAILURE;
  }
  return status;
}

static int check_positional_writes(int fd) {
  const off64_t large_offset = 4294967296LL;
  char observed;

  if (lseek(fd, 7, SEEK_SET) != 7) {
    perror("lseek before positional writes");
    return EXIT_FAILURE;
  }

  if (pwrite(fd, "A", 1, 0) != 1) {
    perror("pwrite");
    return EXIT_FAILURE;
  }

  if (pwrite64(fd, "B", 1, large_offset) != 1) {
    perror("pwrite64");
    return EXIT_FAILURE;
  }

  if (lseek(fd, 0, SEEK_CUR) != 7) {
    fputs("positional writes changed the descriptor offset\n", stderr);
    return EXIT_FAILURE;
  }

  if (pread(fd, &observed, 1, 0) != 1 || observed != 'A' ||
      pread64(fd, &observed, 1, large_offset) != 1 || observed != 'B') {
    fputs("expected distinct bytes at offsets zero and 4 GiB\n", stderr);
    return EXIT_FAILURE;
  }

  if (pwrite64(fd, "C", 1, -1) != -1 || errno != EINVAL) {
    fputs("pwrite64: expected EINVAL for a negative offset\n", stderr);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

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

  if (check_positional_writes(fd) != EXIT_SUCCESS) {
    goto cleanup;
  }

  status = check_duplication(fd);

cleanup:
  if (close(fd) != 0) {
    perror("close");
    status = EXIT_FAILURE;
  }
  return status;
}