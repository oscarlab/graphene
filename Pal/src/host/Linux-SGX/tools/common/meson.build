sgx_util = shared_library('sgx_util',
    'attestation.c',
    'attestation.h',
    'ias.c',
    'ias.h',
    'pf_util.c',
    'pf_util.h',
    'util.c',
    'util.h',

    include_directories: [
        sgx_inc,
        common_inc,
    ],
    dependencies: [
        cjson_dep,
        libcurl_dep,
        protected_files_dep,
        mbedtls_dep,
    ],
)

sgx_util_dep = declare_dependency(
    link_with: sgx_util,
    include_directories: [
        include_directories('.'),
        sgx_inc, # this is mostly for sgx_arch.h
    ],
)
