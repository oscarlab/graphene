#
# Project configuration, options, modules, scripts
#

project(
    'graphene',
    'c', 'cpp',
    version: '1.0',
    license: 'LGPLv3+',

    # DISTRO            EOL         meson_version
    # xenial            2021.04     0.29
    # xenial-backports  2021.04     0.40
    # bionic            2023.04     0.45
    # buster            2022        0.49
    # buster-backports  2022        0.52
    # focal             2025.04     0.53
    #
    # https://wiki.ubuntu.com/Releases
    # https://wiki.debian.org/DebianReleases#Production_Releases
    meson_version: '>=0.45',

    default_options: [
        'c_std=c11',
        'cpp_std=c++14',
        'werror=true',
    ],
)

subdir('Scripts')

prefix = get_option('prefix')
pkglibdir = join_paths(get_option('libdir'), meson.project_name())
pkgdatadir = join_paths(get_option('datadir'), meson.project_name())

direct = get_option('direct') == 'enabled'
sgx = get_option('sgx') == 'enabled'
skeleton = get_option('skeleton') == 'enabled'

dcap = get_option('dcap') == 'enabled'

cc = meson.get_compiler('c')
objcopy = find_program('objcopy')
protoc_c_prog = find_program('protoc-c')

# TODO: after deprecating 18.04/bionic, update this to import('python')
python3mod = import('python3')
python3 = python3mod.find_python()
python3_platlib = run_command(
    python3, get_python_platlib_prog, get_option('prefix')).stdout()
python3_pkgdir = join_paths(python3_platlib, 'graphenelibos')


add_project_arguments(
    '-Wa,--noexecstack',

    '-Wall',
    '-Wextra',

    '-Wmissing-prototypes',
    '-Wstrict-prototypes',
    '-Wwrite-strings',

    '-Wno-sign-compare', #TODO

    cc.get_supported_arguments(
        '-Wtrampolines',
        '-Wnull-dereference',

        # Some of the code uses alignof on expressions, which is a GNU extension.
        # Silence Clang - it complains but does support it.
        '-Wno-gnu-alignof-expression',
    ),

    language: 'c')

if get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized'
    add_project_arguments('-DDEBUG', language: 'c')
endif

if sgx
    conf_sgx = configuration_data()

    # SGX driver options (in make, this was ISGX_DRIVER_PATH and link-intel-driver.py)
    sgx_driver = get_option('sgx_driver')
    sgx_driver_include_path = get_option('sgx_driver_include_path')
    sgx_driver_device = get_option('sgx_driver_device')

    if sgx_driver == 'upstream'
        conf_sgx.set('CONFIG_SGX_DRIVER_UPSTREAM', true)
        if sgx_driver_include_path == ''
            sgx_driver_include_path = '/usr/src/linux-headers-@0@/arch/x86/include'.format(
                run_command('uname', '-r').stdout().strip())
        endif
        if sgx_driver_device == ''
            sgx_driver_device = '/dev/sgx_enclave'
        endif

    elif sgx_driver == 'oot'
        conf_sgx.set('CONFIG_SGX_DRIVER_OOT', true)
        if sgx_driver_include_path == ''
            sgx_driver_include_path = '/opt/intel/linux-sgx-driver'
        endif
        if sgx_driver_device == ''
            sgx_driver_device = '/dev/isgx'
        endif

    else # dcap1.6 or dcap1.10
        if sgx_driver == 'dcap1.6'
            conf_sgx.set('CONFIG_SGX_DRIVER_DCAP_1_6', true)
        elif sgx_driver == 'dcap1.10'
            conf_sgx.set('CONFIG_SGX_DRIVER_DCAP_1_10', true)
        endif

        if sgx_driver_device == ''
            sgx_driver_device = '/dev/sgx/enclave'
        endif
        if sgx_driver_include_path == ''
            sgx_driver_include_path = (
                '/opt/intel/SGXDataCenterAttestationPrimitives/driver/linux/include')
        endif
    endif

    conf_sgx.set_quoted('CONFIG_SGX_DRIVER_DEVICE', sgx_driver_device)
endif

#
# Common checks and flags
#

# Not all compilers support mstack-protector-guard, so use stack protector only if supported.
# Graphene-custom stack protector uses the canary stored in the TCB (same for both in LibOS and PAL)
# at offset 0x8.
cflags_custom_stack_protector = [
    '-fstack-protector-strong',
    '-mstack-protector-guard=tls',
    '-mstack-protector-guard-reg=%gs',
    '-mstack-protector-guard-offset=8',
]
if not cc.has_multi_arguments(cflags_custom_stack_protector)
    cflags_custom_stack_protector = '-fno-stack-protector'
endif

#
# Dependencies
#

threads_dep = dependency('threads')

# TODO remove `#include "toml.h"` from "pal.h", then only _dep should be needed (_incpath and _src
# are needed for generated offsets in Linux-SGX)
tomlc99_proj = subproject('tomlc99-5be06807ad5f2230cad99e15380c4f4076c9dd83')
tomlc99_dep = tomlc99_proj.get_variable('tomlc99_files_dep')
tomlc99_inc = tomlc99_proj.get_variable('tomlc99_inc')
tomlc99_incpath = tomlc99_proj.get_variable('tomlc99_incpath')
tomlc99_src = tomlc99_proj.get_variable('tomlc99_src')

uthash_dep = subproject('uthash-2.1.0').get_variable('uthash_dep')

# this one is bizarre, will be compiled in different ways: one for LibOS, one for PAL
mbedtls_proj = subproject('mbedtls-mbedtls-2.26.0')
mbedtls_dep = mbedtls_proj.get_variable('mbedtls_dep')
mbedtls_pal_dep = mbedtls_proj.get_variable('mbedtls_pal_dep')

if sgx
    libcurl_dep = dependency('libcurl', version: '>7.58.0')

    # on Debian < 11 .pc is not installed and dependency() fails, so we fallback to
    # cc.find_library()
    cjson_dep = dependency('cjson', required: false)
    if not cjson_dep.found()
        cjson_dep = cc.find_library('cjson')
    endif

    if dcap
        sgx_dcap_quoteverify_dep = cc.find_library('sgx_dcap_quoteverify')
    endif
endif

#
# The compilation
#

subdir('include')
subdir('proto')

subdir('common')
subdir('Pal')
subdir('LibOS')
subdir('Runtime')
subdir('python')

run_target('clang-format', command: [meson_clang_format_prog])
