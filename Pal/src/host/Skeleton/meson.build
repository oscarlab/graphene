pal_skeleton_lds = join_paths(
    meson.current_source_dir(), 'arch', host_machine.cpu_family(), 'pal.lds')

pal_skeleton_map = custom_target('pal.map[skeleton]',
    command: [gen_pal_map_prog, '@INPUT@', '@OUTPUT@'],
    input: ['../../pal-symbols', 'pal.map.template'],
    output: 'pal.map',
)

# TODO after meson>=0.46 change this to both_libraries
foreach target_type : ['shared_library', 'static_library']
    target = build_target('pal-skeleton',
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
        pal_common_sources,

        include_directories: [
            includes_pal_common,
            include_directories(
                '../../../include/arch/@0@/Skeleton'.format(host_machine.cpu_family()),
            ),
        ],

        c_args: [
            cflags_pal_common,
            '-fno-builtin',

            # in Skeleton, every single parameter is unused
            '-Wno-unused-parameter',
        ],

        link_args: [
            '-nostdlib',
            '-Wl,-zcombreloc',
            '-Wl,-zdefs',
            '-Wl,--version-script=@0@'.format(pal_skeleton_map.full_path()),
            '-Wl,-T@0@'.format(pal_skeleton_lds),
            '-Wl,--eh-frame-hdr',
            '-Wl,-zrelro',
            '-Wl,-znow',
        ],
        link_depends: [
            pal_skeleton_map,
            pal_skeleton_lds,
        ],

        dependencies: [
            common_dep,
        ],

        install: true,
        install_dir: join_paths(pkglibdir, 'skeleton'),

        target_type: target_type)

    if target_type == 'shared_library'
        libpal_skeleton = target
    else
        libpal_skeleton_static = target
    endif
endforeach

libpal_skeleton_dep = declare_dependency(
    link_with: libpal_skeleton,
)
