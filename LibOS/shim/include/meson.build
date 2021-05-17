# don't put this in include_directories for now, we need this for assembly-only custom_target
includes_libos = [
    '.',
    'arch/@0@'.format(host_machine.cpu_family()),
    '../../../include',
    '../../../common/include',
    '../../../common/include/arch/@0@'.format(host_machine.cpu_family()),
    '../../../Pal/include',
    '../../../Pal/include/arch/@0@'.format(host_machine.cpu_family()),
    '../../../Pal/include/arch/@0@/Linux'.format(host_machine.cpu_family()),
    '../../../Pal/include/elf',
    '../../../Pal/include/pal',
]

includes_libos_i = []
foreach inc : includes_libos
    includes_libos_i += '-I@0@'.format(join_paths(meson.current_source_dir(), inc))
endforeach
includes_libos = [
    include_directories(includes_libos),
    tomlc99_inc,
]

libos_generated_offsets_s = custom_target('generated-offsets-libos.s',
    command: [
        cc.cmd_array(),
        cflags_libos,
        includes_libos_i,
        '-I@0@'.format(tomlc99_incpath),
        '-S',
        '-MD', '-MP', '-MF', '@DEPFILE@',
        '@INPUT@',
        '-o', '@OUTPUT@'
    ],
    output: 'generated-offsets-libos.s',
    input: 'generated-offsets.c',
    depfile: 'generated-offsets.c.d',

    depends: tomlc99_src,
)

libos_asm_offsets_h = custom_target('asm-offsets.h[libos]',
    command: [gen_offsets_prog, '--output-h', '@OUTPUT@', '@INPUT@'],
    input: libos_generated_offsets_s,
    output: 'asm-offsets.h',
)
