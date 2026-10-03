#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -z "${BC_DATA_DIR:-}" ]; then
    printf '%s\n' 'Set BC_DATA_DIR to your prepared data directory; see docs/DATA.md.' >&2
    exit 1
fi
BC_DATA_DIR=$(CDPATH= cd -- "$BC_DATA_DIR" && pwd)
# Reconfigure so switching data packs also rebuilds the matching catalogs.
cmake -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Debug \
    -DBC_BUILD_APP=ON -DBC_DATA_DIR="$BC_DATA_DIR"
cmake --build "$root/build" --parallel
exec "$root/build/battlechess" --data "$BC_DATA_DIR" "$@"
