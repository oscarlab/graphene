subdir('arch')

libos_sources += custom_target('vdso-data.o',
    input: 'vdso-data.S',
    output: 'vdso-data.o',
    command: [
        cc.cmd_array(),
        cflags_libos,
        '-DVDSO_SO_FULL_PATH="@0@"'.format(vdso_so.full_path()),
        '-c',
        '@INPUT@',
        '-o', '@OUTPUT@',
    ],
    depends: vdso_so,
)
