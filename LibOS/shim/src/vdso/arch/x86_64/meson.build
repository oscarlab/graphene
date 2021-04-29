libos_vdso_so_dbg = shared_library('vdso',
    'vdso.c',
    name_prefix: '',
    name_suffix: 'so.dbg',
    include_directories: includes_libos,
    link_args: [
        '-Wl,-T@0@'.format(join_paths(meson.current_source_dir(), 'vdso.lds')),
        '-Wl,-nostdlib',
        '-Wl,-shared',
        '-Wl,--hash-style=both',
        '-Wl,--build-id',
        '-Wl,-Bsymbolic',
        '-Wl,-melf_x86_64',
        '-Wl,--no-undefined',
        '-Wl,-zmax-page-size=4096',
        '-Wl,-zcommon-page-size=4096',
        '-Wl,--soname=linux-vdso.so.1',
    ],
    link_depends: ['vdso.lds'],
)

# TODO check_no_reloc

vdso_so = custom_target(
    'vdso.so',
    command: [objcopy, '-S', '@INPUT@', '@OUTPUT@'],
    input: libos_vdso_so_dbg,
    output: 'vdso.so',
)
