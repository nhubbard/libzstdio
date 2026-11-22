#include "decoded_fd.h"

#include "codec.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

int zstdio_open_decoded_fd(const void *frame, size_t frame_size,
                           size_t logical_size) {
  if ((uintmax_t)logical_size > (uintmax_t)INT64_MAX) {
    errno = EOVERFLOW;
    return -1;
  }

  const int fd =
      memfd_create("zstdio-decoded", MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) {
    return -1;
  }

  if (ftruncate64(fd, (off64_t)logical_size) != 0) {
    goto fail;
  }

  unsigned char empty = 0;
  void *destination = &empty;
  if (logical_size != 0) {
    destination =
        mmap(NULL, logical_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (destination == MAP_FAILED) {
      goto fail;
    }
  }

  size_t written = 0;
  const int decode_status = zstdio_decompress_frame(
      frame, frame_size, destination, logical_size, &written);

  if (logical_size != 0 && munmap(destination, logical_size) != 0) {
    goto fail;
  }
  if (decode_status != 0 || written != logical_size) {
    errno = EBADMSG;
    goto fail;
  }

  if (fcntl(fd, F_ADD_SEALS,
            F_SEAL_WRITE | F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL) != 0) {
    goto fail;
  }

  return fd;

fail:
  {
    const int saved_errno = errno;
    (void)close(fd);
    errno = saved_errno;
    return -1;
  }
}