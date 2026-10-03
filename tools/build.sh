#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake -S "$root" -B "$root/build" -DCMAKE_BUILD_TYPE=Debug -DBC_BUILD_APP=OFF "$@"
cmake --build "$root/build" --parallel
ctest --test-dir "$root/build" --output-on-failure
