libos_sources = files(
	'shim_async.c',
	'shim_checkpoint.c',
	'shim_debug.c',
	'shim_init.c',
	'shim_malloc.c',
	'shim_object.c',
	'shim_parser.c',
	'shim_rtld.c',
	'shim_syscalls.c',
)

precompiled_libgraphene = cc.find_library('graphene-lib',
    dirs: join_paths(meson.current_source_dir(), '.lib'))

if direct
    precompiled_libpal = cc.find_library('pal-Linux',
        dirs: join_paths(meson.current_source_dir(), '../../../Runtime'))
elif sgx
    precompiled_libpal = cc.find_library('pal-Linux-SGX',
        dirs: join_paths(meson.current_source_dir(), '../../../Runtime'))
else
    error('need to link libsysdb against either PAL')
endif

subdir('arch')
subdir('bookkeep')
subdir('fs')
subdir('ipc')
subdir('sys')
subdir('utils')
subdir('vdso')

shim_map = join_paths(meson.current_source_dir(), 'shim.map')

shared_library('sysdb',
    [libos_sources, libos_asm_offsets_h],

    c_args: cflags_libos,
    link_args: [
        '-Wl,-shared',
        '-Wl,-nostdlib',
        '-Wl,-zcombreloc',
        '-Wl,-zrelro',
        '-Wl,-znow',
        '-Wl,-zdefs',
        '-Wl,--version-script=@0@'.format(shim_map),
        '-Wl,-T@0@'.format(shim_lds),
        '-Wl,--eh-frame-hdr',
    ],
    link_depends: [shim_map, shim_lds],
    dependencies: [
        precompiled_libgraphene,
        precompiled_libpal,
    ],

    include_directories: includes_libos,
    install: true,
    install_dir: pkglibdir,
)
