#!/bin/sh

# TODO Debug/Release

set -x
set -e

pwd >&2

CURRENT_SOURCE_DIR="$1"
CURRENT_BUILD_DIR="$2"
PRIVATE_DIR="$3"
O1="$4"
O2="$5"
O3="$6"
shift 6

rm -rf "$PRIVATE_DIR"

cp -ar "$CURRENT_SOURCE_DIR" "$PRIVATE_DIR"
patch -p1 --directory "$PRIVATE_DIR" <"$CURRENT_SOURCE_DIR"/graphene.patch

make -C "$PRIVATE_DIR" lib "$@"

for output in "$O1" "$O2" "$O3"
do
    input=$(basename "$output" | sed -e s:_pal::)
    cp -L "$PRIVATE_DIR"/library/"$input" "$output"
done
