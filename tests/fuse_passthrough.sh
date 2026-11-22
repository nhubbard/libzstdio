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
executable=$3
start_mount() {
    (
        ulimit -S -n 128
        exec "$executable" "$@" "$scratch/backing" "$scratch/mount"
    ) >"$scratch/fuse.log" 2>&1 &
    daemon=$!
    for attempt in $(seq 100); do
        if mountpoint -q "$scratch/mount"; then
            mounted=1
            return
        fi
        if ! kill -0 "$daemon" 2>/dev/null; then
            break
        fi
        sleep 0.05
    done

    cat "$scratch/fuse.log" >&2
    if grep -Eq 'fuse:.*(failed to open /dev/fuse|device /dev/fuse not found)|fusermount3:.*(Operation not permitted|Permission denied|device not found)' "$scratch/fuse.log"; then
        echo "FUSE mount unavailable in this environment" >&2
        exit 77
    fi
    echo "FUSE daemon did not mount $scratch/mount" >&2
    exit 1
}

stop_mount() {
    fusermount3 -u "$scratch/mount"
    mounted=0
    wait "$daemon"
    daemon=
}

if [ "$mode" = dictionary ]; then
    trainer=$4
    mkdir "$scratch/samples"
    for i in $(seq 96); do
        awk -v sample="$i" 'BEGIN {
            for (line = 0; line < 64; line++)
                print "olean constant theorem expression import sample=" sample " line=" line
        }' >"$scratch/samples/$i"
    done
    "$trainer" --train --maxdict=4096 "$scratch"/samples/* -o "$scratch/dictionary"
    start_mount --level 7 --min-size 0 --dictionary "$scratch/dictionary"
else
    start_mount
fi

if ! "$probe" "$scratch/mount"; then
    cat "$scratch/fuse.log" >&2
    exit 1
fi
printf '%s' plain >"$scratch/expected"
cmp "$scratch/expected" "$scratch/backing/truncate"
test "$(stat -c %s "$scratch/backing/compressed.olean")" -lt 4096
test "$(ls -A "$scratch/mount" | LC_ALL=C sort)" = "$(printf '%s\n' compressed.olean truncate)"
test ! -e "$scratch/mount/.zstdio-storage"
stop_mount

if [ "$mode" = dictionary ]; then
    "$trainer" -t -D "$scratch/dictionary" "$scratch/backing/compressed.olean"
    if "$trainer" -qt "$scratch/backing/compressed.olean"; then
        echo "Expected the persisted frame to require its dictionary" >&2
        exit 1
    fi
    rm "$scratch/dictionary"
fi

cp --preserve=xattr "$scratch/backing/compressed.olean" "$scratch/backing/corrupt.olean"
truncate -s -1 "$scratch/backing/corrupt.olean"
start_mount
"$probe" "$scratch/mount" verify-compressed
if cat "$scratch/mount/corrupt.olean" > /dev/null 2>&1; then
    echo "Truncated compressed frame was accepted" >&2
    exit 1
fi
stop_mount
