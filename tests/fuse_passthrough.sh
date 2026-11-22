#!/bin/sh
set -eu
unset LD_PRELOAD

mode=$1
probe=$2
scratch=$(mktemp -d)
mounted=0
daemon=
cleanup() {
    if [ "$mounted" = 1 ]; then
        if ! fusermount3 -u "$scratch/mount" &&
           ! fusermount3 -uz "$scratch/mount"; then
            echo "Could not unmount $scratch/mount" >&2
            return 1
        fi
    fi
    if [ -n "$daemon" ]; then
        kill "$daemon" 2>/dev/null || :
        wait "$daemon" 2>/dev/null || :
    fi
    if [ -d "$scratch/mount" ] && mountpoint -q "$scratch/mount"; then
        fusermount3 -uz "$scratch/mount" || return 1
    fi
    rm -rf -- "$scratch"
}
trap 'cleanup' EXIT
trap 'exit 1' HUP INT TERM
mkdir "$scratch/backing"

if [ "$mode" = native ]; then
    "$probe" "$scratch/backing"
    exit
fi

mkdir "$scratch/mount"
"$3" "$scratch/backing" "$scratch/mount" >"$scratch/fuse.log" 2>&1 &
daemon=$!
for attempt in $(seq 100); do
    if mountpoint -q "$scratch/mount"; then
        mounted=1
        break
    fi
    if ! kill -0 "$daemon" 2>/dev/null; then
        break
    fi
    sleep 0.05
done

if [ "$mounted" = 0 ]; then
    cat "$scratch/fuse.log" >&2
    if grep -Eq 'fuse:.*(failed to open /dev/fuse|device /dev/fuse not found)|fusermount3:.*(Operation not permitted|Permission denied|device not found)' "$scratch/fuse.log"; then
        echo "FUSE mount unavailable in this environment" >&2
        exit 77
    fi
    echo "FUSE daemon did not mount $scratch/mount" >&2
    exit 1
fi

if ! "$probe" "$scratch/mount"; then
    cat "$scratch/fuse.log" >&2
    exit 1
fi
printf '%s' plain >"$scratch/expected"
cmp "$scratch/expected" "$scratch/backing/truncate"
test "$(ls -A "$scratch/backing")" = truncate
fusermount3 -u "$scratch/mount"
mounted=0
wait "$daemon"
daemon=
