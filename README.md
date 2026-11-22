# libzstdio

Linux FUSE filesystem with transparent Zstd compression for Lean/Lake.
The standalone codec also supports shadow compression experiments.

## Linux FUSE mount

With the `fuse3` development package installed, CMake builds `zstdio_fuse`.
Give it two existing, disjoint directories, with an empty mountpoint.
The daemon runs in the foreground.

```sh
cmake -S . -B build
cmake --build build
build/zstdio_fuse --level 3 --min-size 256 --min-savings 32 \
    /path/to/backing /path/to/mountpoint
```

- `--level` sets the Zstd compression level. The default is 3. Negative
  levels trade compression ratio for speed.
- `--min-size` is the minimum logical size in bytes to try compressing.
  The default is 256 and zero tries every nonempty file.
- `--min-savings` is the minimum reduction in stored bytes to keep a Zstd
  frame. The default is 32. Data that would grow is stored verbatim.

The compression policy applies to writes to regular files regardless of name
or extension. Existing plain files can be read through the mount and become
eligible for compression when rewritten. Applications see logical sizes and
ordinary file bytes, including through file-backed mappings. `fsync` commits
file contents to the backing store.

Use the mounted view for filesystem operations. Keep the backing directory
exclusive to this mount on a local filesystem with user xattrs and
`O_TMPFILE` support. Back up the mount view as ordinary files, or preserve
both the backing xattrs and the private `.zstdio-storage` directory when
backing up the store itself. From another terminal, unmount with
`fusermount3 -u /path/to/mountpoint`.

## Run the clean Lean corpus

The `~/grh-verification/lean.tar.gz` archive extracts under `lean/` and
contains no `.lake` cache. Extract it through the mount so the source files
go through the same compression policy as the build outputs.

### Build and mount

In a host terminal with access to `/dev/fuse`, choose a fresh run directory
on a local volume with room for the build. If you use a different path,
set `run` to that same path in every terminal below. The run directory
also holds temporary files and logs.

```bash
set -e
repo="$HOME/libzstdio"
run="$HOME/grh-fuse-run"

ls -l /dev/fuse
mkdir -- "$run"
mkdir -- "$run/backing" "$run/mount" "$run/tmp"
export TMPDIR="$run/tmp"
df -h "$run/backing"

cmake -S "$repo" -B "$repo/build-fuse"
cmake --build "$repo/build-fuse" --target zstdio_fuse
"$repo/build-fuse/zstdio_fuse" \
    --level 3 --min-size 256 --min-savings 32 \
    "$run/backing" "$run/mount"
```

### Extract and build

Keep the daemon running. In a second terminal, extract the archive and run
Lake without a timeout. Lake fetches the dependencies pinned in the
manifest as needed.

```bash
set -euo pipefail
run="$HOME/grh-fuse-run"
export TMPDIR="$run/tmp"

mountpoint -q "$run/mount"
tar --no-same-owner -xzf "$HOME/grh-verification/lean.tar.gz" -C "$run/mount"
(
    cd "$run/mount/lean"
    lake build 2>&1 | tee "$run/lake.log"
)
```

In another terminal, watch free space during the build. The daemon stages
decoded files and can temporarily need space for both staged data and its
compressed replacement. `df` includes this volume-wide usage. If space
runs low, interrupt Lake with `Ctrl-C` and wait for it to exit before
unmounting.

```bash
watch -n 10 'df -h "$HOME/grh-fuse-run/backing"'
```

### Measure and unmount

After Lake exits, leave the mounted directory and measure logical bytes
through the live mount. Then unmount and measure disk allocation on the
backing volume. The commands report the whole corpus and its `.lake`
subtree when present.

```bash
(
    set -e
    run="$HOME/grh-fuse-run"
    cd "$run"
    mountpoint -q "$run/mount"

    du -sb -- "$run/mount/lean"
    if [ -d "$run/mount/lean/.lake" ]; then
        du -sb -- "$run/mount/lean/.lake"
    fi

    fusermount3 -u "$run/mount"

    du -s -B1 -- "$run/backing"
    if [ -d "$run/backing/lean/.lake" ]; then
        du -s -B1 -- "$run/backing/lean/.lake"
    fi
)
```

If unmount reports that the mount is busy, exit any shells within the mount
and stop any processes still using it, then retry. The daemon should exit
after unmount. To compare allocated-space savings against an uncompressed
build, measure the same subtree at the same build milestone in both runs.

### Remove the test data

Keep the backing directory if you want to remount the run. To reclaim its
space after unmounting, remove the empty mountpoint first. The commands
below remove the backing data and temporary files only if that succeeds.
They leave `lake.log`, the original archive, and the original proof tree
in place.

```bash
run="$HOME/grh-fuse-run"
cd "$run" && rmdir -- "$run/mount" && rm -rf -- "$run/backing" "$run/tmp"
```

## Train a dictionary

Collect many representative logical files, ideally from a family of small,
similar files such as Lake `.setup.json` outputs. Train on a sample set
and evaluate the resulting dictionary on a separate set.

```sh
zstd --train --maxdict=32768 /path/to/samples/* -o lake.zdict
build/zstdio_fuse --dictionary lake.zdict /path/to/backing /path/to/mountpoint
```

The first mount with `--dictionary` imports the trained dictionary into
the store. Later mounts automatically load that copy. The store supports
one installed dictionary. Keep it with the backing directory so existing
dictionary-compressed frames remain readable.

## Validation and limits

The filesystem stages decoded files on disk and rewrites complete Zstd
frames on commit. This costs temporary space and can amplify writes on
large files updated in place. The request loop is single-threaded.
Hard links, special files, arbitrary xattrs and ACLs, and multi-user
mounts are outside the supported scope.

`ctest --test-dir build -R fuse -V` runs the same filesystem probe against
a disposable native directory and disposable FUSE mounts. The mounted tests
also verify compression on disk and data after remount. With the `zstd`
CLI installed, the dictionary test trains a dictionary and verifies that
the persisted frame requires it. Mounted tests need access to `/dev/fuse`,
`fusermount3`, and permission to mount.
