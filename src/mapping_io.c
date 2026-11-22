#include "forward.h"

#include <sys/mman.h>
#include <sys/types.h>

FORWARD(void *, mmap,
        (void *address, size_t length, int protection, int flags, int fd,
         off_t offset),
        (address, length, protection, flags, fd, offset), MAP_FAILED)

FORWARD(void *, mmap64,
        (void *address, size_t length, int protection, int flags, int fd,
         off64_t offset),
        (address, length, protection, flags, fd, offset), MAP_FAILED)

FORWARD(int, munmap, (void *address, size_t length), (address, length), -1)

FORWARD(int, msync, (void *address, size_t length, int flags),
        (address, length, flags), -1)