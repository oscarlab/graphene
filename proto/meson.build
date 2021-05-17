aesm_proto_ch = custom_target('aesm.pb-c.[ch]',
    input: 'aesm.proto',

    # TODO after ninja 1.10 (bionic EOL) add two outputs
    # https://github.com/ninja-build/ninja/issues/1184
#   output: ['aesm.pb-c.c', 'aesm.pb-c.h'],
    output: 'aesm.pb-c.c',

    depfile: 'aesm.pb-c.c.d',
    command: [protoc_c_prog,
        '--c_out=@OUTDIR@',
        '--dependency_out=@DEPFILE@',

        # XXX @INPUT@ doesn't work, because protoc-c expects this file to be inside -I, and the
        # check for that is dumb, notably fails when one path is absolute and one is relative, so we
        # have to generate -I and input the same way
        '-I@0@'.format(meson.current_source_dir()),
        join_paths(meson.current_source_dir(), 'aesm.proto'),
    ],
)

aesm_proto_dep = declare_dependency(
    include_directories: include_directories('.'),
    sources: aesm_proto_ch,
)
