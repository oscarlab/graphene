project('mbedtls', 'c')

# TODO this is custom_target, because we need to patch mbedtls before compiling
# PR for patch support in wraps: https://github.com/mesonbuild/meson/pull/4570

mbedtls_libs = custom_target('mbedtls',
    command: [
        find_program('compile.sh'),
        '@CURRENT_SOURCE_DIR@',
        meson.current_build_dir(),
        '@PRIVATE_DIR@',
        '@OUTPUT@',
        'SHARED=1',
    ],
    input: ['Makefile', 'graphene.patch'],
    output: [
        'libmbedcrypto.so',
        'libmbedtls.so',
        'libmbedx509.so',
    ],
    build_by_default: true,
)

mbedtls_pal_libs = custom_target('mbedtls_pal',
    command: [
        find_program('compile-pal.sh'),
        '@CURRENT_SOURCE_DIR@',
        meson.current_build_dir(),
        '@PRIVATE_DIR@',
        '@OUTPUT@',
    ],
    input: ['Makefile', 'graphene.patch'],
    output: [
        'libmbedcrypto_pal.a',
        'libmbedtls_pal.a',
        'libmbedx509_pal.a',
    ],
    build_by_default: true,
)

mbedtls_inc = include_directories('include')
#mbedtls_incpath = meson.current_source_dir() / 'include'

mbedtls_dep = declare_dependency(
    sources: mbedtls_libs,
    include_directories: mbedtls_inc,
)
mbedtls_pal_dep = declare_dependency(
    sources: mbedtls_pal_libs,
    include_directories: mbedtls_inc,
    compile_args: '-DMBEDTLS_CONFIG_FILE="mbedtls/config-pal.h"',
)
