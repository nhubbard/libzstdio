#ifndef ZSTDIO_FUSE_STORAGE_H
#define ZSTDIO_FUSE_STORAGE_H

#include <stddef.h>
#include <sys/stat.h>
#include <zstd.h>

struct storage {
    int root;
    int level;
    off_t min_size;
    off_t min_savings;
    void *dictionary;
    size_t dictionary_size;
};

#define STORAGE_DIRECTORY ".zstdio-storage"

int storage_init(struct storage *storage, const char *dictionary);
int storage_stat(struct storage *storage, const char *path, struct stat *st);
int storage_stage(struct storage *storage);
int storage_checkout(struct storage *storage, const char *path, int writable,
                     int *direct, int *fd);
int storage_sync(struct storage *storage, const char *path);
int storage_commit(struct storage *storage, const char *path, int fd, int durable);

#endif
