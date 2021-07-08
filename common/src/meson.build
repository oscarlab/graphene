mbedtls_dep = declare_dependency(
    dependencies: mbedtls_proj.get_variable('mbedtls_dep'),
    compile_args: [
        '-DCRYPTO_USE_MBEDTLS',
    ],
)

mbedtls_pal_dep = declare_dependency(
    dependencies: mbedtls_proj.get_variable('mbedtls_pal_dep'),
    sources: 'crypto/adapters/mbedtls_adapter.c',
    compile_args: [
        '-DCRYPTO_USE_MBEDTLS',
    ],
)

common_dep = declare_dependency(
    sources: files(
        'avl_tree.c',
        'network/hton.c',
        'network/inet_pton.c',
        'path.c',
        'stack_protector.c',
        'stdlib/printfmt.c',
        'string/atoi.c',
        'string/ctype.c',
        'string/memcmp.c',
        'string/memcpy.c',
        'string/memset.c',
        'string/strchr.c',
        'string/strcmp.c',
        'string/strlen.c',
        'string/strspn.c',
        'string/strstr.c',
        'string/toml_utils.c',
        'string/utils.c',
    ),

    include_directories: common_inc,

    dependencies: [
        uthash_dep,
        tomlc99_dep,
    ],
)
