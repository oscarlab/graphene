pal_direct_sources = files(
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
)

pal_direct_sources_arch = [
    'clone.S',
    'db_arch_exception.c',
    'db_arch_misc.c',
    'pal_start.S',
]
foreach src : pal_direct_sources_arch
    pal_direct_sources += join_paths('arch', host_machine.cpu_family(), src)
endforeach

pal_direct_lds = join_paths(
    meson.current_source_dir(), 'arch', host_machine.cpu_family(), 'pal.lds')

pal_direct_map = custom_target('pal.map[direct]',
    command: [gen_pal_map_prog, '@INPUT@', '@OUTPUT@'],
    input: ['../../pal-symbols', 'pal.map.template'],
    output: 'pal.map',
)

# TODO after meson>=0.46 change this to both_libraries
foreach target_type : ['shared_library', 'static_library']
    target = build_target('pal',
        pal_common_sources,
        pal_linux_common_sources,
        pal_direct_sources,

        include_directories: [
            includes_pal_common,
            include_directories(
                '../../../include/arch/@0@/Linux'.format(host_machine.cpu_family()),
                '../../../include/host/Linux-common',
            ),
        ],

        c_args: [
            cflags_pal_common,
            '-fno-builtin',
            '-DHOST_TYPE=Linux',
        ],

        link_args: [
            '-nostdlib',
            '-Wl,-zcombreloc',
            '-Wl,-zdefs',
            '-Wl,--version-script=@0@'.format(pal_direct_map.full_path()),
            '-Wl,-T@0@'.format(pal_direct_lds),
            '-Wl,--eh-frame-hdr',
            '-Wl,-zrelro',
            '-Wl,-znow',
        ],
        link_depends: [
            pal_direct_map,
            pal_direct_lds,
        ],

        dependencies: [
            common_dep,
        ],

        install: true,
        install_dir: join_paths(pkglibdir, 'direct'),

        target_type: target_type)

    if target_type == 'shared_library'
        libpal_direct = target
    else
        libpal_direct_static = target
    endif
endforeach

libpal_direct_dep = declare_dependency(
    link_with: libpal_direct,
)

install_subdir('gdb_integration',
    install_dir: join_paths(pkglibdir, 'direct'),
    exclude_directories: ['common'])
install_subdir('../../../gdb_integration',
    install_dir: join_paths(pkglibdir, 'direct/gdb_integration/common'),
    strip_directory: true)
meson.add_install_script('/bin/sh', '-c',
    'ln -sf libpal.so "$MESON_INSTALL_DESTDIR_PREFIX"/@0@'.format(
        join_paths(pkglibdir, 'direct/loader')))
