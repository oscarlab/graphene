# sanity check against graphene-config.h
# TODO after meson 0.47 convert this to cc.check_header() (cc.has_header() is inadequate, because it
# does #if __has_include)
# NOTE Both include_directories() and -I are needed, because graphene-config.h is generated,
# and include_directories in cc.compiles() supply only source dir.
incdir = '../../../../include'
if not cc.compiles('#include "gsgx.h"',
        include_directories: include_directories('.', incdir, sgx_driver_include_path),
        args: '-I@0@'.format(join_paths(meson.current_build_dir(), incdir)))
    error('Invalid SGX driver configuration (-Dsgx_driver and/or -Dsgx_driver_include_path)')
endif

cflags_pal_sgx = [
    '-DIN_PAL',
    '-DCRYPTO_USE_MBEDTLS', # TODO either promote this to meson -Doption + CONFIG_, or yeet
]

cflags_urts_stack_protector = ['-fstack-protector-strong']
if not cc.has_multi_arguments(cflags_urts_stack_protector)
    cflags_urts_stack_protector = ['-fno-stack-protector']
endif

sgx_inc = [
    includes_pal_common,
    include_directories(
        '.',
        '../../../include/arch/@0@/Linux'.format(host_machine.cpu_family()),
        '../../../include/host/Linux-common',
        'protected-files',
    ),
]


pal_sgx_generated_offsets_s = custom_target('generated-offsets-pal-sgx.s',
    command: [
        cc.cmd_array(),
        cflags_pal_sgx,
        '-I@0@'.format(meson.current_source_dir()),
        '-I@0@'.format(join_paths(meson.current_source_dir(),
            'protected-files')),
        '-I@0@'.format(join_paths(meson.current_source_dir(),
            '../../../../include')),
        '-I@0@'.format(join_paths(meson.current_build_dir(),
            '../../../../include')),
        '-I@0@'.format(join_paths(meson.current_source_dir(),
            '../../../../common/include')),
        '-I@0@'.format(join_paths(meson.current_source_dir(),
            '../../../../common/include/arch', host_machine.cpu_family())),
#       '-I@0@'.format(join_paths(meson.current_source_dir(),
#           '../../../../common/src/crypto/mbedtls/include')),
        '-I@0@'.format(join_paths(meson.current_source_dir(),
            '../../../../Pal/include')),
        '-I@0@'.format(join_paths(meson.current_source_dir(),
            '../../../../Pal/include/pal')),
        '-I@0@'.format(join_paths(meson.current_source_dir(),
            '../../../../Pal/include/arch', host_machine.cpu_family())),
        '-I@0@'.format(join_paths(meson.current_source_dir(),
            '../../../../Pal/include/arch', host_machine.cpu_family(), 'Linux')),
        '-I@0@'.format(join_paths(meson.current_source_dir(),
            '../../../../Pal/include/host/Linux-common')),
        '-I@0@'.format(tomlc99_incpath),
        '-I@0@'.format(sgx_driver_include_path),
#       includes*,
        '-v',
        '-S',
        '-MD', '-MP', '-MF', '@DEPFILE@',
        '@INPUT@',
        '-o', '@OUTPUT@',
    ],
    output: 'generated-offsets-pal-sgx.s',
    input: 'generated-offsets.c',
    depfile: 'generated-offsets.c.d',

    depends: tomlc99_src,
)

# TODO: after meson 0.54 those two targets can be combined, and the second one might be passed to
# installs in python/ directory
pal_sgx_asm_offsets = custom_target('asm-offsets.h[pal-sgx]',
    command: [gen_offsets_prog, '--output-h', '@OUTPUT@', '@INPUT@'],
    input: pal_sgx_generated_offsets_s,
    output: 'asm-offsets.h',
)
pal_sgx_offsets_py = custom_target('_offsets.py',
    command: [gen_offsets_prog, '--output-py', '@OUTPUT@', '@INPUT@'],
    input: pal_sgx_generated_offsets_s,
    output: '_offsets.py',

    install: true,
    install_dir: python3_pkgdir,
)

pal_sgx_map = custom_target('pal.map',
    command: [gen_pal_map_prog, '@INPUT@', '@OUTPUT@'],
    input: ['../../pal-symbols', 'pal.map.template'],
    output: 'pal.map',
)
pal_sgx_lds = join_paths(meson.current_source_dir(), 'enclave.lds')

libpal_sgx = shared_library('pal',
    'db_devices.c',
    'db_eventfd.c',
    'db_events.c',
    'db_exception.c',
    'db_files.c',
    'db_main.c',
    'db_memory.c',
    'db_misc.c',
    'db_object.c',
    'db_pipes.c',
    'db_process.c',
    'db_rtld.c',
    'db_sockets.c',
    'db_streams.c',
    'db_threading.c',
    'enclave_ecalls.c',
    'enclave_entry.S',
    'enclave_framework.c',
    'enclave_ocalls.c',
    'enclave_pages.c',
    'enclave_pf.c',
    'enclave_platform.c',
    'enclave_untrusted.c',
    'enclave_xstate.c',
    'protected-files/lru_cache.c',
    'protected-files/protected_files.c',

    pal_common_sources,
    pal_linux_common_sources_enclave,
    pal_sgx_asm_offsets,

    c_args: [
        cflags_pal_common,
        cflags_pal_sgx,
        cflags_custom_stack_protector,
        '-DIN_ENCLAVE',
    ],

    link_args: [
        '-nostdlib',
        '-Wl,-zcombreloc',
        '-Wl,-zdefs',
        '-Wl,--version-script=@0@'.format(pal_sgx_map.full_path()),
        '-Wl,-T@0@'.format(pal_sgx_lds),
        '-Wl,--eh-frame-hdr',
        '-Wl,-zrelro',
        '-Wl,-znow',
    ],
    link_depends: [
        pal_sgx_map,
        pal_sgx_lds,
    ],

    include_directories: sgx_inc,
    dependencies: [
        config_dep,
        common_dep,
        mbedtls_pal_dep,
    ],

    install: true,
    install_dir: join_paths(pkglibdir, 'sgx'),
)

executable('loader',
    'clone.S',
    'sgx_enclave.c',
    'sgx_exception.c',
    'sgx_framework.c',
    'sgx_log.c',
    'sgx_main.c',
    'sgx_perf_data.c',
    'sgx_platform.c',
    'sgx_process.c',
    'sgx_profile.c',
    'sgx_profile_glibc.c',
    'sgx_gdb_info.c',
    'sgx_thread.c',
    'sgx_entry.S',
    pal_linux_common_sources_urts,
    pal_sgx_asm_offsets,

    include_directories: sgx_inc,
    c_args: [
        cflags_pal_common,
        cflags_pal_sgx,
        cflags_urts_stack_protector,
        '-pie',
    ],
    link_args: [
        '-Wl,-zrelro',
        '-Wl,-znow',
    ],

    dependencies: [
        config_dep,
        common_dep,
        aesm_proto_dep,
        dependency('libprotobuf-c'),
    ],

    install: true,
    install_dir: join_paths(pkglibdir, 'sgx'),
)

libpal_sgx_dep = declare_dependency(
    link_with: libpal_sgx,
)

subdir('protected-files')
subdir('tools')
