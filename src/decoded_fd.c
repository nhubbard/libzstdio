#include "decoded_fd.h"

#include "codec.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

int zstdio_open_decoded_fd(const void *frame, size_t frame_size,
                           size_t logical_size) {
  if ((uintmax_t)logical_size > (uintmax_t)INT64_MAX) {
    errno = EOVERFLOW;
    return -1;
  }

  unsigned char *bytes = malloc(logical_size == 0 ? 1 : logical_size);
  if (bytes == NULL) {
    return -1;
  }

  size_t written = 0;
  if (zstdio_decompress_frame(frame, frame_size, bytes, logical_size,
                              &written) != 0 ||
      written != logical_size) {
    free(bytes);
    errno = EBADMSG;
    return -1;
  }

  const int fd =
      memfd_create("zstdio-decoded", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) {
    const int saved_errno = errno;
    free(bytes);
    errno = saved_errno;
    return -1;
  }

  const size_t max_chunk = 1024U * 1024U;
  size_t offset = 0;
  while (offset < logical_size) {
    size_t chunk = logical_size - offset;
    if (chunk > max_chunk) {
      chunk = max_chunk;
    }

    const ssize_t count = pwrite64(fd, bytes + offset, chunk, (off64_t)offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      if (count == 0) {
        errno = EIO;
      }
      goto fail;
    }
    offset += (size_t)count;
  }

  if (fcntl(fd, F_ADD_SEALS,
            F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) != 0) {
    goto fail;
  }

  free(bytes);
  return fd;

fail:
  {
    const int saved_errno = errno;
    free(bytes);
    (void)close(fd);
    errno = saved_errno;
    return -1;
  }
}