#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "line %d: %s (errno %d)\n",                        \
                    __LINE__, #condition, errno);                                \
            exit(EXIT_FAILURE);                                                 \
        }                                                                       \
    } while (0)

static void check_size(int fd, const char *path, int flags, uint64_t size)
{
    struct statx info;
    CHECK(statx(fd, path, flags | AT_STATX_FORCE_SYNC, STATX_SIZE, &info) == 0);
    CHECK(info.stx_mask & STATX_SIZE);
    CHECK(info.stx_size == size);
}

static int create_file(const char *path, const char *contents)
{
    int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0);
    check_size(fd, "", AT_EMPTY_PATH, 0);
    size_t length = strlen(contents);
    CHECK(write(fd, contents, length) == (ssize_t)length);
    check_size(AT_FDCWD, path, 0, length);
    return fd;
}

static void check_bytes(int fd, const char *expected)
{
    char buffer[32];
    ssize_t count = pread(fd, buffer, sizeof(buffer), 0);
    CHECK(count == (ssize_t)strlen(expected));
    CHECK(memcmp(buffer, expected, (size_t)count) == 0);
}

static void check_missing(const char *path)
{
    struct statx info;
    CHECK(statx(AT_FDCWD, path, 0, STATX_SIZE, &info) == -1 && errno == ENOENT);
}

static void test_truncate(void)
{
    int fd = create_file("truncate", "abcdef");
    CHECK(ftruncate(fd, 3) == 0);
    check_size(fd, "", AT_EMPTY_PATH, 3);
    check_bytes(fd, "abc");
    CHECK(close(fd) == 0);

    CHECK(truncate("truncate", 8193) == 0);
    check_size(AT_FDCWD, "truncate", 0, 8193);
    fd = open("truncate", O_RDONLY);
    CHECK(fd >= 0);
    char tail = 'x';
    CHECK(pread(fd, &tail, 1, 8192) == 1 && tail == '\0');
    CHECK(close(fd) == 0);

    fd = open("truncate", O_WRONLY | O_TRUNC);
    CHECK(fd >= 0);
    check_size(AT_FDCWD, "truncate", 0, 0);
    CHECK(write(fd, "plain", 5) == 5);
    CHECK(close(fd) == 0);
    check_size(AT_FDCWD, "truncate", 0, 5);
}

static void test_rename_and_unlink(void)
{
    int source = create_file("source", "replacement");
    int replaced = create_file("destination", "old");
    CHECK(rename("source", "destination") == 0);
    check_missing("source");

    int current = open("destination", O_RDONLY);
    CHECK(current >= 0);
    check_bytes(current, "replacement");
    check_bytes(source, "replacement");
    check_bytes(replaced, "old");
    check_size(replaced, "", AT_EMPTY_PATH, 3);

    CHECK(unlink("destination") == 0);
    check_missing("destination");
    check_bytes(source, "replacement");
    check_bytes(current, "replacement");
    check_size(current, "", AT_EMPTY_PATH, 11);
    CHECK(close(current) == 0);
    CHECK(close(replaced) == 0);
    CHECK(close(source) == 0);
}

static void test_mappings(void)
{
    int fd = create_file("sample.olean", "olean payload");
    long page_size = sysconf(_SC_PAGESIZE);
    CHECK(page_size > 0);
    size_t length = (size_t)page_size;
    CHECK(ftruncate(fd, (off_t)length) == 0);
    CHECK(close(fd) == 0);
    fd = open("sample.olean", O_RDONLY);
    CHECK(fd >= 0);

    char *plain = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK(plain != MAP_FAILED && memcmp(plain, "olean payload", 13) == 0);

    // Lean requests the saved page-aligned address on a read-only descriptor.
    char *address = mmap(NULL, length, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(address != MAP_FAILED);
    address[0] = 'x';
    int flags = MAP_PRIVATE | MAP_FIXED_NOREPLACE;
    CHECK(mmap(address, length, PROT_READ | PROT_WRITE, flags, fd, 0) ==
          MAP_FAILED && errno == EEXIST);
    CHECK(address[0] == 'x');
    CHECK(munmap(address, length) == 0);
    char *fixed = mmap(address, length, PROT_READ | PROT_WRITE, flags, fd, 0);
    CHECK(fixed != MAP_FAILED && fixed == address);
    CHECK(memcmp(fixed, "olean payload", 13) == 0);
    fixed[0] = 'O';

    char first;
    CHECK(pread(fd, &first, 1, 0) == 1 && first == 'o' && plain[0] == 'o');
    CHECK(unlink("sample.olean") == 0);
    CHECK(close(fd) == 0);
    CHECK(fixed[0] == 'O' && plain[0] == 'o');
    CHECK(munmap(fixed, length) == 0);
    CHECK(munmap(plain, length) == 0);
}

enum { COMPRESSED_SIZE = 65536 };

static void verify_compressed(void)
{
    int fd = open("compressed.olean", O_RDONLY);
    CHECK(fd >= 0);
    check_size(fd, "", AT_EMPTY_PATH, COMPRESSED_SIZE);
    check_size(AT_FDCWD, "compressed.olean", 0, COMPRESSED_SIZE);
    const char *data = mmap(NULL, COMPRESSED_SIZE, PROT_READ, MAP_PRIVATE, fd, 0);
    CHECK(data != MAP_FAILED);
    for (size_t i = 0; i < COMPRESSED_SIZE; i++) {
        char expected = i == 2048 ? 'B' : (i == 4096 ? 'C' : 'A');
        CHECK(data[i] == expected);
    }
    CHECK(munmap((void *)data, COMPRESSED_SIZE) == 0);
    CHECK(close(fd) == 0);
}

static void test_compression(void)
{
    int fd = open("pending.olean", O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0);
    char contents[COMPRESSED_SIZE];
    memset(contents, 'A', sizeof(contents));
    CHECK(write(fd, contents, sizeof(contents)) == (ssize_t)sizeof(contents));
    CHECK(fsync(fd) == 0);
    CHECK(rename("pending.olean", "compressed.olean") == 0);

    int reader = open("compressed.olean", O_RDONLY);
    CHECK(reader >= 0);
    CHECK(pwrite(fd, "B", 1, 2048) == 1);
    char written;
    CHECK(pread(reader, &written, 1, 2048) == 1 && written == 'B');
    CHECK(close(reader) == 0);

    char *shared = mmap(NULL, COMPRESSED_SIZE, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    CHECK(shared != MAP_FAILED);
    CHECK(fsync(fd) == 0);
    CHECK(close(fd) == 0);
    shared[4096] = 'C';
    CHECK(msync(shared, COMPRESSED_SIZE, MS_SYNC) == 0);
    CHECK(munmap(shared, COMPRESSED_SIZE) == 0);
    verify_compressed();
}

static void test_append_to_open_plain_file(void)
{
    CHECK(close(create_file("append", "old")) == 0);
    int reader = open("append", O_RDONLY);
    CHECK(reader >= 0);
    int writer = open("append", O_WRONLY | O_APPEND);
    CHECK(writer >= 0);
    CHECK(write(writer, "+new", 4) == 4);
    check_bytes(reader, "old+new");
    CHECK(close(writer) == 0);
    CHECK(close(reader) == 0);
    CHECK(unlink("append") == 0);
}

static void test_many_mappings(void)
{
    enum { COUNT = 160 };
    const char *mappings[COUNT];
    CHECK(mkdir("mapped", 0700) == 0);
    for (size_t i = 0; i < COUNT; i++) {
        char path[32];
        snprintf(path, sizeof(path), "mapped/%zu", i);
        int fd = create_file(path, "x");
        mappings[i] = mmap(NULL, 1, PROT_READ, MAP_PRIVATE, fd, 0);
        CHECK(mappings[i] != MAP_FAILED);
        CHECK(close(fd) == 0);
        CHECK(unlink(path) == 0);
    }
    for (size_t i = 0; i < COUNT; i++) {
        CHECK(mappings[i][0] == 'x');
        CHECK(munmap((void *)mappings[i], 1) == 0);
    }
    CHECK(rmdir("mapped") == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 || argc == 3);
    CHECK(chdir(argv[1]) == 0);
    if (argc == 3) {
        CHECK(strcmp(argv[2], "verify-compressed") == 0);
        verify_compressed();
        return EXIT_SUCCESS;
    }
    test_truncate();
    test_rename_and_unlink();
    test_mappings();
    test_compression();
    test_append_to_open_plain_file();
    test_many_mappings();
    puts("statx, truncate, rename/unlink, and private/shared mapping probes passed");
    return EXIT_SUCCESS;
}
