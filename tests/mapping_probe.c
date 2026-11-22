#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static int check_shared_mapping(int fd) {
  const unsigned char updated = 0x3c;
  unsigned char observed = 0;
  int status = EXIT_FAILURE;

  if (ftruncate(fd, 2) != 0) {
    perror("ftruncate");
    return EXIT_FAILURE;
  }

  unsigned char *bytes =
      mmap(NULL, 2, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (bytes == MAP_FAILED) {
    perror("mmap shared");
    return EXIT_FAILURE;
  }

  bytes[1] = updated;
  if (msync(bytes, 2, MS_SYNC) != 0) {
    perror("msync");
  } else if (pread(fd, &observed, 1, 1) != 1 || observed != updated) {
    fputs("msync: expected the mapped store in the file\n", stderr);
  } else if (msync(bytes + 1, 1, MS_SYNC) != -1 || errno != EINVAL) {
    fputs("msync: expected EINVAL for an unaligned address\n", stderr);
  } else {
    status = EXIT_SUCCESS;
  }

  if (munmap(bytes, 2) != 0) {
    perror("munmap shared");
    status = EXIT_FAILURE;
  }

  if (status == EXIT_SUCCESS) {
    if (ftruncate64(fd, -1) != -1 || errno != EINVAL) {
      fputs("ftruncate64: expected EINVAL for a negative length\n", stderr);
      status = EXIT_FAILURE;
    } else if (ftruncate64(fd, 1) != 0) {
      perror("ftruncate64");
      status = EXIT_FAILURE;
    } else if (pread(fd, &observed, 1, 1) != 0) {
      fputs("ftruncate64: expected EOF at the new file length\n", stderr);
      status = EXIT_FAILURE;
    }
  }

  return status;
}

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

  if (check_shared_mapping(fd) != EXIT_SUCCESS) {
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