
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    const char *path = "zstdio-smoke.setup.json";
    const char *data = "{\"test\":true}\n";

    int fd = open(
        path,
        O_WRONLY | O_CREAT | O_TRUNC,
        0600
    );

    if (fd < 0) {
        perror("open for writing");
        return 1;
    }

    ssize_t written = write(fd, data, strlen(data));
    if (written != (ssize_t)strlen(data)) {
        perror("write");
        close(fd);
        return 1;
    }

    if (close(fd) != 0) {
        perror("close");
        return 1;
    }

    fd = open(path, O_RDONLY);

    if (fd < 0) {
        perror("open for reading");
        return 1;
    }

    char buffer[64] = {0};
    ssize_t count = read(fd, buffer, sizeof(buffer) - 1);

    if (count < 0) {
        perror("read");
        close(fd);
        return 1;
    }

    if (close(fd) != 0) {
        perror("close");
        return 1;
    }

    if ((size_t)count != strlen(data) ||
        memcmp(buffer, data, strlen(data)) != 0) {
        fprintf(stderr, "Data mismatch!\n");
        return 1;
    }

    if (unlink(path) != 0) {
        perror("unlink");
        return 1;
    }

    puts("PASS: filesystem roundtrip");
    return 0;
}
