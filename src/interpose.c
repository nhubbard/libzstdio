#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

typedef int (*open_fn)(const char *, int, ...);

static open_fn resolve_open(void) {
    void *symbol = dlsym(RTLD_NEXT, "open");
    open_fn fn = NULL;

    // Copy a dynamic-linker symbol pointer into a function pointer without an ISO C cast.
    if (symbol != NULL) memcpy(&fn, &symbol, sizeof(fn));

    return fn;
}

static bool is_target(const char *path) {
    if (path == NULL) return false;

    const char *suffix = ".setup.json";
    size_t path_len = strlen(path);
    size_t suffix_len = strlen(suffix);

    return path_len >= suffix_len &&
        strcmp(path + path_len - suffix_len, suffix) == 0;
}

static void log_interception(void) {
    static const char message[] = "[zstdio] intercepted setup file\n";

    // Avoid stdio recursion through our own hooks.
    (void)syscall(
        SYS_write,
        STDERR_FILENO,
        message,
        sizeof(message) -1
    );
}

int open(const char *path, int flags, ...) {
    open_fn real_open = resolve_open();

    if (real_open == NULL) {
        errno = ENOSYS;
        return -1;
    }

    bool has_mode = (flags & O_CREAT) || ((flags & O_TMPFILE) == O_TMPFILE);

    mode_t mode = 0;

    if (has_mode) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
    }

    int fd;
    if (has_mode) {
        fd = real_open(path, flags, mode);
    } else {
        fd = real_open(path, flags);
    }

    int saved_errno = errno;

    if (is_target(path)) {
        log_interception();
    }

    errno = saved_errno;
    return fd;
}