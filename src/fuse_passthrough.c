#define FUSE_USE_VERSION 31
#include <fuse.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct backing {
    int fd;
};

static int backing_fd(void)
{
    struct backing *backing = fuse_get_context()->private_data;
    return backing->fd;
}

static const char *relative_path(const char *path)
{
    return path[1] == '\0' ? "." : path + 1;
}

static int status(int result)
{
    return result == -1 ? -errno : 0;
}

static void *passthrough_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    // Handle I/O and metadata by descriptor even after a name disappears.
    cfg->hard_remove = 1;
    cfg->nullpath_ok = 1;
    conn->want &= ~(unsigned int)FUSE_CAP_WRITEBACK_CACHE;
    return fuse_get_context()->private_data;
}

static int passthrough_getattr(const char *path, struct stat *st,
                               struct fuse_file_info *fi)
{
    if (fi)
        return status(fstat((int)fi->fh, st));
    return status(fstatat(backing_fd(), relative_path(path), st,
                          AT_SYMLINK_NOFOLLOW));
}

static int open_handle(const char *path, struct fuse_file_info *fi,
                       int flags, mode_t mode)
{
    int fd = openat(backing_fd(), relative_path(path),
                    flags | O_CLOEXEC | O_NOFOLLOW, mode);
    if (fd == -1)
        return -errno;
    fi->fh = (uint64_t)fd;
    return 0;
}

static int passthrough_open(const char *path, struct fuse_file_info *fi)
{
    return open_handle(path, fi, fi->flags, 0);
}

static int passthrough_create(const char *path, mode_t mode,
                              struct fuse_file_info *fi)
{
    return open_handle(path, fi, fi->flags | O_CREAT, mode);
}

static int passthrough_release(const char *path, struct fuse_file_info *fi)
{
    (void)path;
    return status(close((int)fi->fh));
}

static int passthrough_read(const char *path, char *buf, size_t size,
                            off_t offset, struct fuse_file_info *fi)
{
    (void)path;
    ssize_t count = pread((int)fi->fh, buf, size, offset);
    return count == -1 ? -errno : (int)count;
}

static int passthrough_write(const char *path, const char *buf, size_t size,
                             off_t offset, struct fuse_file_info *fi)
{
    (void)path;
    // Linux pwrite honors O_APPEND on the backing descriptor.
    ssize_t count = pwrite((int)fi->fh, buf, size, offset);
    return count == -1 ? -errno : (int)count;
}

static int passthrough_truncate(const char *path, off_t size,
                                struct fuse_file_info *fi)
{
    if (fi)
        return status(ftruncate((int)fi->fh, size));

    int fd = openat(backing_fd(), relative_path(path),
                    O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd == -1)
        return -errno;
    int result = status(ftruncate(fd, size));
    close(fd);
    return result;
}

static int passthrough_fsync(const char *path, int datasync,
                             struct fuse_file_info *fi)
{
    (void)path;
    int fd = (int)fi->fh;
    return status(datasync ? fdatasync(fd) : fsync(fd));
}

static int passthrough_rename(const char *from, const char *to, unsigned int flags)
{
    int fd = backing_fd();
    return status(renameat2(fd, relative_path(from), fd, relative_path(to), flags));
}

static int passthrough_unlink(const char *path)
{
    return status(unlinkat(backing_fd(), relative_path(path), 0));
}

static int passthrough_mkdir(const char *path, mode_t mode)
{
    return status(mkdirat(backing_fd(), relative_path(path), mode));
}

static int passthrough_rmdir(const char *path)
{
    return status(unlinkat(backing_fd(), relative_path(path), AT_REMOVEDIR));
}

static int passthrough_opendir(const char *path, struct fuse_file_info *fi)
{
    return open_handle(path, fi, O_RDONLY | O_DIRECTORY, 0);
}

static int passthrough_readdir(const char *path, void *buf, fuse_fill_dir_t fill,
                               off_t offset, struct fuse_file_info *fi,
                               enum fuse_readdir_flags flags)
{
    (void)path;
    (void)offset;
    (void)flags;
    // With zero offsets libfuse buffers the entire listing per directory open.
    int fd = openat((int)fi->fh, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
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
    return status(symlinkat(target, backing_fd(), relative_path(path)));
}

static int passthrough_chmod(const char *path, mode_t mode,
                             struct fuse_file_info *fi)
{
    if (fi)
        return status(fchmod((int)fi->fh, mode));
    return status(fchmodat(backing_fd(), relative_path(path), mode, 0));
}

static int passthrough_utimens(const char *path, const struct timespec times[2],
                               struct fuse_file_info *fi)
{
    if (fi)
        return status(futimens((int)fi->fh, times));
    return status(utimensat(backing_fd(), relative_path(path), times,
                            AT_SYMLINK_NOFOLLOW));
}

static const struct fuse_operations operations = {
    .init = passthrough_init,
    .getattr = passthrough_getattr,
    .open = passthrough_open,
    .create = passthrough_create,
    .release = passthrough_release,
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
    .releasedir = passthrough_release,
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

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s BACKING_DIRECTORY MOUNTPOINT\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *backing_path = realpath(argv[1], NULL);
    if (!backing_path) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }
    char *mountpoint = realpath(argv[2], NULL);
    if (!mountpoint) {
        perror(argv[2]);
        free(backing_path);
        return EXIT_FAILURE;
    }

    int result = EXIT_FAILURE;
    if (contains_path(backing_path, mountpoint) ||
        contains_path(mountpoint, backing_path)) {
        fprintf(stderr, "Backing directory and mountpoint must be disjoint\n");
    } else {
        struct backing backing = {
            .fd = open(backing_path, O_PATH | O_DIRECTORY | O_CLOEXEC),
        };
        if (backing.fd == -1) {
            perror(backing_path);
        } else {
            char *fuse_argv[] = {
                argv[0], "-f", "-o", "default_permissions", mountpoint, NULL,
            };
            // The kernel already applies the caller's umask to creation modes.
            umask(0);
            result = fuse_main(5, fuse_argv, &operations, &backing);
            close(backing.fd);
        }
    }
    free(mountpoint);
    free(backing_path);
    return result;
}
