# libzstdio

Linux FUSE passthrough for Lean/Lake, with a standalone Zstd codec for
shadow compression experiments.

## Linux FUSE passthrough

With the `fuse3` development package installed, CMake also builds
`zstdio_fuse`. This executable passes file contents through unchanged and
runs in the foreground. Give it two existing, disjoint directories, with an
empty mountpoint.

```sh
cmake -S . -B build
cmake --build build
build/zstdio_fuse /path/to/backing /path/to/mountpoint
```

Use the mounted view for filesystem operations while the daemon is running.
Use a backing directory you control. From another terminal, unmount with
`fusermount3 -u /path/to/mountpoint`.

`ctest --test-dir build -R fuse -V` runs the same filesystem probe against
a disposable native directory and a disposable FUSE mount. The mounted test
requires access to `/dev/fuse`, `fusermount3`, and permission to mount in the
current environment. It reports a skip when FUSE device or mount permissions
prevent it from running.
