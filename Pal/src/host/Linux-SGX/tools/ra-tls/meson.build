libra_tls_inc = include_directories('.')

ra_tls_args = [
    '-fvisibility=hidden',
]

libra_tls_attest = shared_library('ra_tls_attest',
    'ra_tls_attest.c',
    args: ra_tls_args,
    include_directories: sgx_inc,
    dependencies: [
        mbedtls_dep,
    ],
)

libra_tls_verify_epid = shared_library('ra_tls_verify_epid',
    'ra_tls_verify_epid.c',
    'ra_tls_verify_common.c',
    'ra_tls.h',

    args: ra_tls_args,
    include_directories: sgx_inc,
    dependencies: [
        sgx_util_dep,
        mbedtls_dep,
    ],
)

libsecret_prov_attest = shared_library('secret_prov_attest',
    'secret_prov_attest.c',
    'secret_prov_common.c',
    'ra_tls_attest.c',
    'ra_tls.h',
    'secret_prov.h',

    args: ra_tls_args,
    include_directories: sgx_inc,
    dependencies: [
        mbedtls_dep,
    ],
)

libsecret_prov_verify_epid = shared_library('secret_prov_verify_epid',
    'ra_tls_verify_epid.c',
    'ra_tls_verify_common.c',
    'secret_prov_verify.c',
    'secret_prov_common.c',
    'ra_tls.h',
    'secret_prov.h',

    args: ra_tls_args,
    include_directories: sgx_inc,
    dependencies: [
        threads_dep,
        sgx_util_dep,
        mbedtls_dep,
    ],
)

if dcap
    libra_tls_verify_dcap = shared_library('ra_tls_verify_dcap',
        'ra_tls_verify_dcap.c',
        'ra_tls_verify_common.c',
        'ra_tls.h',

        args: ra_tls_args,
        include_directories: sgx_inc,
        dependencies: [
            sgx_util_dep,
            mbedtls_dep,
        ],
    )

    libra_tls_verify_dcap_graphene = shared_library('ra_tls_verify_dcap_graphene',
        'ra_tls_verify_dcap.c',
        'ra_tls_verify_dcap_urts.c',
        'ra_tls_verify_common.c',
        'ra_tls.h',

        args: ra_tls_args,
        include_directories: sgx_inc,
        dependencies: [
            sgx_util_dep,
            mbedtls_dep,
        ],
    )

    libsecret_prov_verify_dcap = shared_library('secret_prov_verify_dcap',
        'ra_tls_verify_dcap.c',
        'ra_tls_verify_common.c',
        'secret_prov_verify.c',
        'secret_prov_common.c',
        'ra_tls.h',
        'secret_prov.h',

        args: ra_tls_args,
        include_directories: sgx_inc,
        dependencies: [
            threads_dep,
            sgx_util_dep,
            mbedtls_dep,
        ],
    )
endif
