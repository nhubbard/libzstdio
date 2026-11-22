#!/usr/bin/env python3

import argparse
import os
import stat
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

CHUNK_SIZE = 1024 * 1024


def signature(info):
    return (
        info.st_dev,
        info.st_ino,
        info.st_size,
        info.st_mtime_ns,
        info.st_ctime_ns,
    )


def verify_frame(source, frame):
    source.seek(0)
    with subprocess.Popen(
        ["zstd", "-q", "-d", "-c", "--", str(frame)],
        stdout=subprocess.PIPE,
    ) as decoder:
        while True:
            expected = source.read(CHUNK_SIZE)
            actual = decoder.stdout.read(CHUNK_SIZE)
            if actual != expected:
                decoder.kill()
                raise ValueError("decoded bytes differ from the source")
            if not expected:
                break
        if decoder.wait() != 0:
            raise ValueError("zstd could not verify the frame")


def publish(source_path, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(
        source_path, os.O_RDONLY | os.O_CLOEXEC | os.O_NOFOLLOW | os.O_NONBLOCK
    )

    with os.fdopen(fd, "rb") as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError("source is not a regular file")

        temporary = None
        try:
            with tempfile.NamedTemporaryFile(
                dir=destination.parent, prefix=".zstdio-", delete=False
            ) as output:
                temporary = Path(output.name)
                subprocess.run(
                    [
                        "zstd",
                        "-q",
                        "--check",
                        f"--stream-size={before.st_size}",
                        "-c",
                    ],
                    stdin=source,
                    stdout=output,
                    check=True,
                )
                output.flush()
                os.fsync(output.fileno())
                frame_size = os.fstat(output.fileno()).st_size

            verify_frame(source, temporary)
            if (
                signature(os.fstat(source.fileno())) != signature(before)
                or signature(source_path.lstat()) != signature(before)
            ):
                raise ValueError("source changed during compression")

            os.replace(temporary, destination)
            directory_fd = os.open(destination.parent, os.O_DIRECTORY | os.O_RDONLY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)

            return before.st_size, frame_size
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("shadow", type=Path)
    parser.add_argument("--suffix", required=True)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()

    source = args.source.resolve(strict=True)
    shadow = args.shadow.resolve()
    if not source.is_dir() or not args.suffix or args.jobs < 1:
        parser.error("expected a source directory, nonempty suffix, and positive jobs")
    if source == shadow or source in shadow.parents or shadow in source.parents:
        parser.error("source and shadow must be separate directory trees")

    files = []
    for directory, _, names in os.walk(source, followlinks=False):
        for name in names:
            if name.endswith(args.suffix):
                path = Path(directory) / name
                if not path.is_symlink():
                    files.append(path)

    def process(path):
        relative = path.relative_to(source)
        destination = shadow / relative.parent / (relative.name + ".zst")
        try:
            original_size, frame_size = publish(path, destination)
            return original_size, frame_size, None
        except (OSError, ValueError, subprocess.CalledProcessError) as error:
            return 0, 0, f"{relative}: {error}"

    original_bytes = frame_bytes = verified = failed = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        for original_size, frame_size, error in executor.map(process, sorted(files)):
            if error is not None:
                failed += 1
                print(f"FAILED {error}", file=os.sys.stderr)
            else:
                verified += 1
                original_bytes += original_size
                frame_bytes += frame_size

    print(f"Verified files: {verified}; failed files: {failed}")
    print(f"Original bytes: {original_bytes}; frame bytes: {frame_bytes}")
    if original_bytes:
        print(f"Potential payload reduction: {1 - frame_bytes / original_bytes:.2%}")
    return 1 if failed or not verified else 0


if __name__ == "__main__":
    raise SystemExit(main())