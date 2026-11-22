#define FUSE_USE_VERSION 31
#include <fuse.h>

#include "fuse_storage.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

struct staged_file {
    struct staged_file *next;
    char *path;
    char *renamed_path;
    int fd;
    int direct;
    int dirty;
    unsigned refs;
};

struct handle {
    struct staged_file *file;
    int dir;
    int is_root;
};

struct backing {
    struct storage storage;
    struct staged_file *files;
};

static struct backing *backing(void)
{
    return fuse_get_context()->private_data;
}

static int backing_fd(void)
{
    return backing()->storage.root;
}

static struct handle *handle(const struct fuse_file_info *fi)
{
    return (struct handle *)(uintptr_t)fi->fh;
}

static struct staged_file *find_file(const char *path)
{
    for (struct staged_file *file = backing()->files; file; file = file->next)
        if (file->path && strcmp(file->path, path) == 0)
            return file;
    return NULL;
}

static void drop_file(struct staged_file *file)
{
    struct staged_file **entry = &backing()->files;
    while (*entry != file)
        entry = &(*entry)->next;
    *entry = file->next;
    close(file->fd);
    free(file->path);
    free(file);
}

static int commit(struct staged_file *file, int durable)
{
    if (!file->path)
        return 0;
    if (!file->dirty)
        return durable ? storage_sync(&backing()->storage, file->path) : 0;
    int result = storage_commit(&backing()->storage, file->path, file->fd, durable);
    if (!result)
        file->dirty = 0;
    return result;
}

static const char *relative_path(const char *path)
{
    return path[1] == '\0' ? "." : path + 1;
}

static int has_prefix(const char *path, const char *prefix)
{
    size_t length = strlen(prefix);
    return strncmp(path, prefix, length) == 0 &&
           (path[length] == '\0' || path[length] == '/');
}

static int reserved(const char *relative)
{
    return has_prefix(relative, STORAGE_DIRECTORY);
}

static int status(int result)
{
    return result == -1 ? -errno : 0;
}

static void *passthrough_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    // Keep libfuse's hidden name for open inodes after unlink or replacement.
    cfg->nullpath_ok = 1;
    conn->want &= ~(unsigned int)FUSE_CAP_WRITEBACK_CACHE;
    return fuse_get_context()->private_data;
}

static int passthrough_getattr(const char *path, struct stat *st,
                               struct fuse_file_info *fi)
{
    struct staged_file *file = NULL;
    if (fi) {
        struct handle *opened = handle(fi);
        if (!opened->file)
            return status(fstat(opened->dir, st));
        file = opened->file;
    } else {
        if (reserved(relative_path(path)))
            return -ENOENT;
        file = find_file(relative_path(path));
    }

    if (!file)
        return storage_stat(&backing()->storage, relative_path(path), st);

    struct stat stage;
    if (fstat(file->fd, &stage) < 0)
        return -errno;
    if (!file->path) {
        *st = stage;
        st->st_nlink = 0;
        return 0;
    }
    if (fstatat(backing_fd(), file->path, st, AT_SYMLINK_NOFOLLOW) < 0)
        return -errno;
    st->st_size = stage.st_size;
    st->st_atim = stage.st_atim;
    st->st_mtim = stage.st_mtim;
    if (file->dirty)
        st->st_ctim = stage.st_ctim;
    return 0;
}

static int open_file(const char *path, struct fuse_file_info *fi, int create,
                     mode_t mode)
{
    path = relative_path(path);
    if (reserved(path))
        return create ? -EACCES : -ENOENT;
    struct handle *opened = calloc(1, sizeof(*opened));
    if (!opened)
        return -ENOMEM;

    int flags = (fi->flags & O_ACCMODE) | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
    if (create)
        flags |= O_CREAT | O_EXCL;
    int fd = openat(backing_fd(), path, flags, mode);
    int result = -errno;
    if (fd < 0)
        goto fail_handle;

    struct stat st;
    result = status(fstat(fd, &st));
    close(fd);
    if (result < 0)
        goto fail_create;
    if (!S_ISREG(st.st_mode) ||
        ((fi->flags & O_ACCMODE) != O_RDONLY && st.st_nlink != 1)) {
        result = -EOPNOTSUPP;
        goto fail_create;
    }

    struct staged_file *file = find_file(path);
    int writable = (fi->flags & O_ACCMODE) != O_RDONLY || (fi->flags & O_TRUNC);
    if (!file) {
        file = calloc(1, sizeof(*file));
        result = -ENOMEM;
        if (!file)
            goto fail_create;
        file->path = strdup(path);
        if (file->path) {
            if (create) {
                file->fd = storage_stage(&backing()->storage);
                result = file->fd < 0 ? file->fd : 0;
            } else {
                result = storage_checkout(&backing()->storage, path, writable,
                                          &file->direct, &file->fd);
            }
        }
        if (result < 0) {
            free(file->path);
            free(file);
            goto fail_create;
        }
        file->next = backing()->files;
        backing()->files = file;
    } else if (writable && file->direct) {
        int staged, direct;
        result = storage_checkout(&backing()->storage, path, 1, &direct, &staged);
        if (result < 0)
            goto fail_create;
        close(file->fd);
        file->fd = staged;
        file->direct = direct;
    }
    if (fi->flags & O_TRUNC) {
        result = status(ftruncate(file->fd, 0));
        if (result < 0) {
            if (!file->refs && !file->dirty)
                drop_file(file);
            goto fail_create;
        }
        file->dirty = 1;
    }

    file->refs++;
    opened->file = file;
    fi->fh = (uintptr_t)opened;
    return 0;

fail_create:
    if (create)
        unlinkat(backing_fd(), path, 0);
fail_handle:
    free(opened);
    return result;
}

static int passthrough_open(const char *path, struct fuse_file_info *fi)
{
    return open_file(path, fi, 0, 0);
}

static int passthrough_create(const char *path, mode_t mode,
                              struct fuse_file_info *fi)
{
    return open_file(path, fi, 1, mode);
}

static int passthrough_flush(const char *path, struct fuse_file_info *fi)
{
    (void)path;
    return commit(handle(fi)->file, 0);
}

static int passthrough_release(const char *path, struct fuse_file_info *fi)
{
    (void)path;
    struct handle *opened = handle(fi);
    struct staged_file *file = opened->file;
    free(opened);
    if (--file->refs)
        return 0;

    int result = commit(file, 0);
    if (result < 0) {
        fprintf(stderr, "Could not commit %s: %s\n", file->path, strerror(-result));
        return result;
    }
    drop_file(file);
    return 0;
}

static int passthrough_read(const char *path, char *buf, size_t size,
                            off_t offset, struct fuse_file_info *fi)
{
    (void)path;
    ssize_t count = pread(handle(fi)->file->fd, buf, size, offset);
    return count == -1 ? -errno : (int)count;
}

static int passthrough_write(const char *path, const char *buf, size_t size,
                             off_t offset, struct fuse_file_info *fi)
{
    (void)path;
    struct staged_file *file = handle(fi)->file;
    if ((fi->flags & O_APPEND) && !fi->writepage) {
        // The single-threaded request loop makes the EOF lookup and write atomic.
        offset = lseek(file->fd, 0, SEEK_END);
        if (offset < 0)
            return -errno;
    }
    ssize_t count = pwrite(file->fd, buf, size, offset);
    if (count > 0)
        file->dirty = 1;
    return count == -1 ? -errno : (int)count;
}

static int passthrough_truncate(const char *path, off_t size,
                                struct fuse_file_info *fi)
{
    struct fuse_file_info temporary = { .flags = O_WRONLY };
    int result;
    if (!fi) {
        result = open_file(path, &temporary, 0, 0);
        if (result < 0)
            return result;
        fi = &temporary;
    }

    struct staged_file *file = handle(fi)->file;
    result = status(ftruncate(file->fd, size));
    if (result == 0)
        file->dirty = 1;
    if (fi == &temporary) {
        if (result == 0)
            result = commit(file, 0);
        int released = passthrough_release(path, fi);
        if (result == 0)
            result = released;
    }
    return result;
}

static int passthrough_fsync(const char *path, int datasync,
                             struct fuse_file_info *fi)
{
    (void)path;
    (void)datasync;
    return commit(handle(fi)->file, 1);
}

static int passthrough_fsyncdir(const char *path, int datasync,
                                struct fuse_file_info *fi)
{
    (void)path;
    (void)datasync;
    return status(fsync(handle(fi)->dir));
}

static int passthrough_rename(const char *from, const char *to, unsigned int flags)
{
    from = relative_path(from);
    to = relative_path(to);
    if (reserved(from) || reserved(to))
        return -EACCES;
    if (strcmp(from, to) == 0 && !flags)
        return 0;

    int result = 0;
    for (struct staged_file *file = backing()->files; file; file = file->next) {
        if (!file->path)
            continue;
        const char *old_prefix, *new_prefix;
        if (has_prefix(file->path, from)) {
            old_prefix = from;
            new_prefix = to;
        } else if ((flags & RENAME_EXCHANGE) && has_prefix(file->path, to)) {
            old_prefix = to;
            new_prefix = from;
        } else {
            continue;
        }

        result = commit(file, 0);
        if (result < 0)
            break;
        if (asprintf(&file->renamed_path, "%s%s", new_prefix,
                     file->path + strlen(old_prefix)) < 0) {
            file->renamed_path = NULL;
            result = -ENOMEM;
            break;
        }
    }
    if (result == 0) {
        int fd = backing_fd();
        result = status(renameat2(fd, from, fd, to, flags));
    }
    for (struct staged_file *file = backing()->files; file; file = file->next) {
        if (result == 0 && (file->renamed_path ||
                            (file->path && has_prefix(file->path, to)))) {
            free(file->path);
            file->path = file->renamed_path;
        } else {
            free(file->renamed_path);
        }
        file->renamed_path = NULL;
    }
    return result;
}

static int passthrough_unlink(const char *path)
{
    path = relative_path(path);
    if (reserved(path))
        return -EACCES;
    int result = status(unlinkat(backing_fd(), path, 0));
    struct staged_file *file = find_file(path);
    if (result == 0 && file) {
        free(file->path);
        file->path = NULL;
        if (!file->refs)
            drop_file(file);
    }
    return result;
}

static int passthrough_mkdir(const char *path, mode_t mode)
{
    if (reserved(relative_path(path)))
        return -EACCES;
    return status(mkdirat(backing_fd(), relative_path(path), mode));
}

static int passthrough_rmdir(const char *path)
{
    if (reserved(relative_path(path)))
        return -EACCES;
    return status(unlinkat(backing_fd(), relative_path(path), AT_REMOVEDIR));
}

static int passthrough_opendir(const char *path, struct fuse_file_info *fi)
{
    struct handle *opened = calloc(1, sizeof(*opened));
    if (!opened)
        return -ENOMEM;
    opened->is_root = strcmp(path, "/") == 0;
    opened->dir = openat(backing_fd(), relative_path(path),
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (opened->dir < 0) {
        int result = -errno;
        free(opened);
        return result;
    }
    fi->fh = (uintptr_t)opened;
    return 0;
}

static int passthrough_readdir(const char *path, void *buf, fuse_fill_dir_t fill,
                               off_t offset, struct fuse_file_info *fi,
                               enum fuse_readdir_flags flags)
{
    (void)path;
    (void)offset;
    (void)flags;
    // With zero offsets libfuse buffers the entire listing per directory open.
    int fd = openat(handle(fi)->dir, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd == -1)
        return -errno;
    DIR *dir = fdopendir(fd);
    if (!dir) {
        int result = -errno;
        close(fd);
        return result;
    }

    int result = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            result = -errno;
            break;
        }
        if (handle(fi)->is_root && strcmp(entry->d_name, STORAGE_DIRECTORY) == 0)
            continue;
        if (fill(buf, entry->d_name, NULL, 0, 0))
            break;
    }
    closedir(dir);
    return result;
}

static int passthrough_readlink(const char *path, char *buf, size_t size)
{
    ssize_t count = readlinkat(backing_fd(), relative_path(path), buf, size - 1);
    if (count == -1)
        return -errno;
    buf[count] = '\0';
    return 0;
}

static int passthrough_symlink(const char *target, const char *path)
{
    if (reserved(relative_path(path)))
        return -EACCES;
    return status(symlinkat(target, backing_fd(), relative_path(path)));
}

static int passthrough_releasedir(const char *path, struct fuse_file_info *fi)
{
    (void)path;
    struct handle *opened = handle(fi);
    int result = status(close(opened->dir));
    free(opened);
    return result;
}

static int passthrough_chmod(const char *path, mode_t mode,
                             struct fuse_file_info *fi)
{
    if (fi) {
        struct handle *opened = handle(fi);
        if (!opened->file)
            return status(fchmod(opened->dir, mode));
        if (!opened->file->path)
            return status(fchmod(opened->file->fd, mode));
        return status(fchmodat(backing_fd(), opened->file->path, mode, 0));
    }
    return status(fchmodat(backing_fd(), relative_path(path), mode, 0));
}

static int passthrough_utimens(const char *path, const struct timespec times[2],
                               struct fuse_file_info *fi)
{
    struct staged_file *file;
    if (fi) {
        struct handle *opened = handle(fi);
        if (!opened->file)
            return status(futimens(opened->dir, times));
        file = opened->file;
        if (!file->path)
            return status(futimens(file->fd, times));
        path = file->path;
    } else {
        path = relative_path(path);
        file = find_file(path);
    }
    int result = status(utimensat(backing_fd(), path, times, AT_SYMLINK_NOFOLLOW));
    if (result == 0 && file)
        result = status(futimens(file->fd, times));
    return result;
}

static const struct fuse_operations operations = {
    .init = passthrough_init,
    .getattr = passthrough_getattr,
    .open = passthrough_open,
    .create = passthrough_create,
    .release = passthrough_release,
    .flush = passthrough_flush,
    .read = passthrough_read,
    .write = passthrough_write,
    .truncate = passthrough_truncate,
    .fsync = passthrough_fsync,
    .rename = passthrough_rename,
    .unlink = passthrough_unlink,
    .mkdir = passthrough_mkdir,
    .rmdir = passthrough_rmdir,
    .opendir = passthrough_opendir,
    .readdir = passthrough_readdir,
    .releasedir = passthrough_releasedir,
    .fsyncdir = passthrough_fsyncdir,
    .readlink = passthrough_readlink,
    .symlink = passthrough_symlink,
    .chmod = passthrough_chmod,
    .utimens = passthrough_utimens,
};

static int contains_path(const char *parent, const char *path)
{
    size_t length = strlen(parent);
    return strcmp(parent, "/") == 0 ||
           (strncmp(parent, path, length) == 0 &&
            (path[length] == '\0' || path[length] == '/'));
}

static int parse_number(const char *text, long long minimum, long long maximum,
                        long long *value)
{
    char *end;
    errno = 0;
    *value = strtoll(text, &end, 10);
    return errno == 0 && end != text && !*end && *value >= minimum && *value <= maximum;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [--level N] [--min-size BYTES] "
                    "[--min-savings BYTES] [--dictionary PATH] "
                    "BACKING_DIRECTORY MOUNTPOINT\n", program);
}

static void raise_descriptor_limit(void)
{
    // File mappings keep FUSE handles alive after clients close their descriptors.
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == 0 && limit.rlim_cur < limit.rlim_max) {
        limit.rlim_cur = limit.rlim_max;
        if (setrlimit(RLIMIT_NOFILE, &limit) < 0)
            perror("Could not raise the daemon's file descriptor limit");
    }
}

int main(int argc, char **argv)
{
    struct backing state = {
        .storage = { .root = -1, .level = 3, .min_size = 256, .min_savings = 32 },
    };
    const char *dictionary = NULL;
    static const struct option options[] = {
        { "level", required_argument, NULL, 'l' },
        { "min-size", required_argument, NULL, 's' },
        { "min-savings", required_argument, NULL, 'b' },
        { "dictionary", required_argument, NULL, 'd' },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };

    int option;
    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        long long value;
        switch (option) {
        case 'l':
            if (!parse_number(optarg, ZSTD_minCLevel(), ZSTD_maxCLevel(), &value))
                goto invalid;
            state.storage.level = (int)value;
            break;
        case 's':
            if (!parse_number(optarg, 0, INT64_MAX, &value))
                goto invalid;
            state.storage.min_size = (off_t)value;
            break;
        case 'b':
            if (!parse_number(optarg, 0, INT64_MAX, &value))
                goto invalid;
            state.storage.min_savings = (off_t)value;
            break;
        case 'd':
            dictionary = optarg;
            break;
        case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            goto invalid;
        }
    }
    if (argc - optind != 2)
        goto invalid;

    char *backing_path = realpath(argv[optind], NULL);
    if (!backing_path) {
        perror(argv[optind]);
        return EXIT_FAILURE;
    }
    char *mountpoint = realpath(argv[optind + 1], NULL);
    if (!mountpoint) {
        perror(argv[optind + 1]);
        free(backing_path);
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;
    if (contains_path(backing_path, mountpoint) ||
        contains_path(mountpoint, backing_path)) {
        fprintf(stderr, "Backing directory and mountpoint must be disjoint\n");
    } else {
        state.storage.root = open(backing_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (state.storage.root < 0) {
            perror(backing_path);
        } else {
            // The kernel already applies the caller's umask to creation modes.
            umask(0);
            int initialized = storage_init(&state.storage, dictionary);
            if (initialized < 0) {
                fprintf(stderr, "Could not initialize compression storage: %s\n",
                        strerror(-initialized));
            } else {
                raise_descriptor_limit();
                char *fuse_argv[] = {
                    argv[0], "-f", "-s", "-o", "default_permissions", mountpoint, NULL,
                };
                result = fuse_main(6, fuse_argv, &operations, &state);
            }
        }
    }
    while (state.files) {
        // libfuse can exit without releasing all open handles.
        struct staged_file *file = state.files;
        if (file->dirty && file->path) {
            int committed = storage_commit(&state.storage, file->path, file->fd, 1);
            if (committed < 0) {
                fprintf(stderr, "Could not save %s: %s\n", file->path, strerror(-committed));
                result = EXIT_FAILURE;
            }
        }
        state.files = file->next;
        close(file->fd);
        free(file->path);
        free(file);
    }
    if (state.storage.root >= 0)
        close(state.storage.root);
    free(state.storage.dictionary);
    free(mountpoint);
    free(backing_path);
    return result;

invalid:
    usage(argv[0]);
    return EXIT_FAILURE;
}
