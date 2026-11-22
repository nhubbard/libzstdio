#include "fuse_storage.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/xattr.h>
#include <unistd.h>

#define SIZE_ATTRIBUTE "user.zstdio.logical-size"
#define DICTIONARY_PATH STORAGE_DIRECTORY "/dictionary"
#define FORMAT_PATH STORAGE_DIRECTORY "/format-v1"

static int write_all(int fd, const void *data, size_t size)
{
    const char *cursor = data;
    while (size) {
        ssize_t count = write(fd, cursor, size);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return count == 0 ? -EIO : -errno;
        cursor += count;
        size -= (size_t)count;
    }
    return 0;
}

static int copy_data(int from, int to)
{
    char buffer[65536];
    off_t offset = 0;
    for (;;) {
        ssize_t count = pread(from, buffer, sizeof(buffer), offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return count == 0 ? 0 : -errno;
        int result = write_all(to, buffer, (size_t)count);
        if (result < 0)
            return result;
        offset += count;
    }
}

static int size_attribute(ssize_t count, const char *value, off_t *size)
{
    if (count < 0)
        return errno == ENODATA ? 0 : -errno;
    if (count == 0 || count > 19)
        return -EIO;

    uint64_t decoded = 0;
    for (ssize_t i = 0; i < count; i++) {
        if (value[i] < '0' || value[i] > '9')
            return -EIO;
        decoded = decoded * 10 + (unsigned)(value[i] - '0');
    }
    if (decoded > INT64_MAX)
        return -EIO;
    *size = (off_t)decoded;
    return 1;
}

int storage_stat(struct storage *storage, const char *path, struct stat *st)
{
    if (fstatat(storage->root, path, st, AT_SYMLINK_NOFOLLOW) < 0)
        return -errno;
    if (!S_ISREG(st->st_mode))
        return 0;

    char *resolved;
    if (asprintf(&resolved, "/proc/self/fd/%d/%s", storage->root, path) < 0)
        return -ENOMEM;
    char value[20];
    ssize_t count = getxattr(resolved, SIZE_ATTRIBUTE, value, sizeof(value));
    int result = size_attribute(count, value, &st->st_size);
    free(resolved);
    return result < 0 ? result : 0;
}

static int decode(struct storage *storage, int from, int to, off_t logical_size)
{
    char input[65536], output[65536];
    ZSTD_DCtx *context = ZSTD_createDCtx();
    if (!context)
        return -ENOMEM;
    size_t next = ZSTD_DCtx_setParameter(context, ZSTD_d_windowLogMax, 27);
    if (!ZSTD_isError(next))
        next = ZSTD_DCtx_loadDictionary(context, storage->dictionary,
                                       storage->dictionary_size);
    int result = -EIO;
    off_t remaining = logical_size;
    off_t offset = 0;
    int complete = 0;
    while (!ZSTD_isError(next)) {
        ssize_t count = pread(from, input, sizeof(input), offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            result = count < 0 ? -errno : (complete && remaining == 0 ? 0 : -EIO);
            break;
        }
        offset += count;
        ZSTD_inBuffer in = { input, (size_t)count, 0 };
        size_t produced;
        do {
            if (complete)
                break;
            ZSTD_outBuffer out = { output, sizeof(output), 0 };
            next = ZSTD_decompressStream(context, &out, &in);
            if (ZSTD_isError(next) || out.pos > (uint64_t)remaining)
                goto done;
            int written = write_all(to, output, out.pos);
            if (written < 0) {
                result = written;
                goto done;
            }
            remaining -= (off_t)out.pos;
            complete = next == 0;
            produced = out.pos;
        } while (in.pos < in.size || produced == sizeof(output));
        if (in.pos != in.size)
            break;
    }
done:
    ZSTD_freeDCtx(context);
    return result;
}

static int encode(struct storage *storage, int from, int to, off_t size)
{
    char input[65536], output[65536];
    ZSTD_CCtx *context = ZSTD_createCCtx();
    if (!context)
        return -ENOMEM;
    size_t next = ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel,
                                         storage->level);
    if (!ZSTD_isError(next))
        next = ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, 1);
    if (!ZSTD_isError(next))
        next = ZSTD_CCtx_setParameter(context, ZSTD_c_windowLog, 27);
    if (!ZSTD_isError(next))
        next = ZSTD_CCtx_loadDictionary(context, storage->dictionary,
                                       storage->dictionary_size);
    if (!ZSTD_isError(next))
        next = ZSTD_CCtx_setPledgedSrcSize(context, (uint64_t)size);
    int result = -EIO;
    off_t offset = 0;
    while (!ZSTD_isError(next)) {
        ssize_t count = pread(from, input, sizeof(input), offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            result = -errno;
            break;
        }
        offset += count;
        ZSTD_inBuffer in = { input, (size_t)count, 0 };
        do {
            ZSTD_outBuffer out = { output, sizeof(output), 0 };
            next = ZSTD_compressStream2(context, &out, &in,
                                        count ? ZSTD_e_continue : ZSTD_e_end);
            if (ZSTD_isError(next))
                goto done;
            int written = write_all(to, output, out.pos);
            if (written < 0) {
                result = written;
                goto done;
            }
        } while (in.pos < in.size || (!count && next));
        if (!count) {
            result = 0;
            break;
        }
    }
done:
    ZSTD_freeCCtx(context);
    return result;
}

int storage_stage(struct storage *storage)
{
    int fd = openat(storage->root, STORAGE_DIRECTORY,
                    O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);
    return fd < 0 ? -errno : fd;
}

int storage_checkout(struct storage *storage, const char *path, int writable,
                     int *direct, int *fd)
{
    int source = openat(storage->root, path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (source < 0)
        return -errno;
    char value[20];
    ssize_t count = fgetxattr(source, SIZE_ATTRIBUTE, value, sizeof(value));
    off_t logical_size = 0;
    int result = size_attribute(count, value, &logical_size);
    if (result < 0) {
        close(source);
        return result;
    }
    *direct = !result && !writable;
    if (*direct) {
        *fd = source;
        return 0;
    }

    int staged = storage_stage(storage);
    if (staged < 0) {
        result = staged;
    } else {
        result = result ? decode(storage, source, staged, logical_size)
                        : copy_data(source, staged);
        struct stat st;
        if (result == 0 && fstat(source, &st) < 0)
            result = -errno;
        if (result == 0) {
            struct timespec times[] = { st.st_atim, st.st_mtim };
            if (futimens(staged, times) < 0)
                result = -errno;
        }
        if (result < 0)
            close(staged);
        else
            *fd = staged;
    }
    close(source);
    return result;
}

static int sync_parent(struct storage *storage, const char *path)
{
    char *parent = strdup(path);
    if (!parent)
        return -ENOMEM;
    char *slash = strrchr(parent, '/');
    if (slash)
        *slash = '\0';
    int dir = openat(storage->root, slash ? parent : ".",
                     O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    free(parent);
    if (dir < 0)
        return -errno;
    int result = fsync(dir) < 0 ? -errno : 0;
    close(dir);
    return result;
}

int storage_sync(struct storage *storage, const char *path)
{
    int fd = openat(storage->root, path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -errno;
    int result = fsync(fd) < 0 ? -errno : 0;
    close(fd);
    return result < 0 ? result : sync_parent(storage, path);
}

int storage_commit(struct storage *storage, const char *path, int fd, int durable)
{
    struct stat stage, original;
    if (fstat(fd, &stage) < 0 ||
        fstatat(storage->root, path, &original, AT_SYMLINK_NOFOLLOW) < 0)
        return -errno;
    if (!S_ISREG(original.st_mode) || original.st_nlink != 1)
        return -EOPNOTSUPP;

    uint64_t nonce;
    if (getrandom(&nonce, sizeof(nonce), 0) != (ssize_t)sizeof(nonce))
        return -EIO;
    char temp[sizeof(STORAGE_DIRECTORY) + 32];
    snprintf(temp, sizeof(temp), STORAGE_DIRECTORY "/write-%016" PRIx64, nonce);
    int out = openat(storage->root, temp,
                     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (out < 0)
        return -errno;

    int result = 0;
    int compressed = stage.st_size >= storage->min_size && stage.st_size > 0;
    if (compressed) {
        result = encode(storage, fd, out, stage.st_size);
        off_t encoded_size = result == 0 ? lseek(out, 0, SEEK_CUR) : -1;
        if (result == 0 && encoded_size < 0)
            result = -errno;
        if (result == 0 && (encoded_size >= stage.st_size ||
                           stage.st_size - encoded_size < storage->min_savings)) {
            compressed = 0;
            if (ftruncate(out, 0) < 0 || lseek(out, 0, SEEK_SET) < 0)
                result = -errno;
        }
    }
    if (!compressed && result == 0)
        result = copy_data(fd, out);
    if (compressed && result == 0) {
        char value[20];
        int length = snprintf(value, sizeof(value), "%" PRIdMAX, (intmax_t)stage.st_size);
        if (fsetxattr(out, SIZE_ATTRIBUTE, value, (size_t)length, 0) < 0)
            result = -errno;
    }
    struct timespec times[] = { stage.st_atim, stage.st_mtim };
    mode_t mode = original.st_mode & 07777;
    mode &= (mode_t)~S_ISUID;
    if (mode & S_IXGRP)
        mode &= (mode_t)~S_ISGID;
    if (result == 0 &&
        (((original.st_uid != geteuid() || original.st_gid != getegid()) &&
          fchown(out, original.st_uid, original.st_gid) < 0) ||
         fchmod(out, mode) < 0 || futimens(out, times) < 0 ||
         (durable && fsync(out) < 0)))
        result = -errno;
    if (close(out) < 0 && result == 0)
        result = -errno;
    if (result == 0 && renameat(storage->root, temp, storage->root, path) < 0)
        result = -errno;
    if (result < 0)
        unlinkat(storage->root, temp, 0);
    else if (durable) {
        result = sync_parent(storage, path);
        if (!result)
            result = sync_parent(storage, temp);
    }
    return result;
}

static int load_dictionary(struct storage *storage)
{
    int fd = openat(storage->root, DICTIONARY_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return errno == ENOENT ? 0 : -errno;
    struct stat st;
    int result = -EINVAL;
    if (fstat(fd, &st) < 0) {
        result = -errno;
    } else if (S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size <= 1024 * 1024) {
        size_t size = (size_t)st.st_size;
        void *data = malloc(size);
        if (!data) {
            result = -ENOMEM;
        } else if (pread(fd, data, size, 0) == (ssize_t)size &&
                   ZSTD_getDictID_fromDict(data, size)) {
            storage->dictionary = data;
            storage->dictionary_size = size;
            result = 0;
        } else {
            free(data);
        }
    }
    close(fd);
    return result;
}

int storage_init(struct storage *storage, const char *dictionary)
{
    if (flock(storage->root, LOCK_EX | LOCK_NB) < 0)
        return -errno;
    int created = mkdirat(storage->root, STORAGE_DIRECTORY, 0700) == 0;
    if (!created && errno != EEXIST)
        return -errno;
    int work = openat(storage->root, STORAGE_DIRECTORY,
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (work < 0)
        return -errno;
    struct stat work_stat;
    int result = 0;
    if (fstat(work, &work_stat) < 0)
        result = -errno;
    else if (work_stat.st_uid != geteuid() || (work_stat.st_mode & 0077))
        result = -EACCES;

    int probe = result == 0 ? storage_stage(storage) : result;
    if (probe < 0) {
        result = probe;
    } else {
        if (fsetxattr(probe, SIZE_ATTRIBUTE, "0", 1, XATTR_CREATE) < 0)
            result = -errno;
        close(probe);
    }

    if (result == 0) {
        int flags = created ? O_CREAT | O_EXCL | O_WRONLY : O_RDONLY;
        int format = openat(storage->root, FORMAT_PATH,
                            flags | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
        if (format < 0) {
            result = errno == ENOENT ? -EINVAL : -errno;
        } else {
            struct stat format_stat;
            if (fstat(format, &format_stat) < 0)
                result = -errno;
            else if (!S_ISREG(format_stat.st_mode) || format_stat.st_size != 0)
                result = -EINVAL;
            else if (created && (fsync(format) < 0 || fsync(work) < 0 ||
                                 fsync(storage->root) < 0))
                result = -errno;
            close(format);
        }
    }
    close(work);
    if (result < 0) {
        if (created) {
            unlinkat(storage->root, FORMAT_PATH, 0);
            unlinkat(storage->root, STORAGE_DIRECTORY, AT_REMOVEDIR);
        }
        return result;
    }

    result = load_dictionary(storage);
    if (result < 0 || !dictionary)
        return result;
    if (storage->dictionary)
        return -EEXIST;

    int source = open(dictionary, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (source < 0)
        return -errno;
    struct stat st;
    if (fstat(source, &st) < 0 || !S_ISREG(st.st_mode) ||
        st.st_size <= 0 || st.st_size > 1024 * 1024) {
        close(source);
        return -EINVAL;
    }
    size_t size = (size_t)st.st_size;
    void *sample = malloc(size);
    if (!sample) {
        close(source);
        return -ENOMEM;
    }
    if (pread(source, sample, size, 0) != (ssize_t)size ||
        !ZSTD_getDictID_fromDict(sample, size)) {
        result = -EINVAL;
    } else {
        int out = openat(storage->root, DICTIONARY_PATH,
                         O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (out < 0) {
            result = -errno;
        } else {
            result = write_all(out, sample, size);
            if (result == 0 && fsync(out) < 0)
                result = -errno;
            close(out);
            if (result == 0)
                result = sync_parent(storage, DICTIONARY_PATH);
            if (result < 0)
                unlinkat(storage->root, DICTIONARY_PATH, 0);
        }
    }
    close(source);
    if (result == 0) {
        storage->dictionary = sample;
        storage->dictionary_size = size;
    } else {
        free(sample);
    }
    return result;
}
