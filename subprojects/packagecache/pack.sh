#!/bin/sh

set -e

if test -z "$1" || ! test -d "$1"
then
    echo "usage: $0 DIRNAME" >&2
    exit 2
fi

# stolen from https://reproducible-builds.org/docs/archives/
# requires GNU Tar 1.28+ (Ubuntu >=16.04)

tar --sort=name \
    --mtime="@0" \
    --owner=0 --group=0 --numeric-owner \
    --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime \
    -cf "$1"-wrap.tar "$1"

patch_hash=$(sha256sum "$1"-wrap.tar | awk '{ print $1 }')

sed -i -e "s:^patch_hash.*:patch_hash = $patch_hash:" ../"$1".wrap 

rm -rf "$1"
