#!/bin/sh

CFLAGS='-O2 -DMBEDTLS_CONFIG_FILE=\"mbedtls/config-pal.h\"'
export CFLAGS
exec "$(dirname "$0")"/compile.sh "$@"
