#!/bin/sh

set -e

if test -z "$1" || ! test -f "$1"
then
    echo "usage: $0 TARBALL" >&2
    exit 2
fi

# stolen from https://reproducible-builds.org/docs/archives/
# requires GNU Tar 1.28+ (Ubuntu >=16.04)

tar -xf "$1"
