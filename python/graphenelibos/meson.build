conf = configuration_data()
conf.set('VERSION', meson.project_version())
conf.set('PKGLIBDIR', join_paths(get_option('prefix'), pkglibdir))
conf.set('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))

init_py = configure_file(
    input: '__init__.py',
    output: '__init__.py',
    configuration: conf,
)

python3_platlib = run_command(
    python3, get_python_platlib_prog, get_option('prefix')).stdout()
python3_pkgdir = join_paths(python3_platlib, 'graphenelibos')

install_data([
    init_py,
    'manifest.py',
], install_dir: python3_pkgdir)

if sgx
    offsets_py = configure_file(
        input: '../../Pal/src/host/Linux-SGX/generated_offsets.py',
        output: '_offsets.py',
        configuration: configuration_data(),
    )

    custom_target('aesm_pb2.py',
        command: ['protoc', '--python_out=@OUTDIR@', '@INPUT@'],
        input: ['aesm.proto'],
        output: ['aesm_pb2.py'],
        install_dir: python3_pkgdir)

    install_data([
        offsets_py,
        '_aesm_pb2.py',
        'sgx_get_token.py',
        'sgx_sign.py',
    ], install_dir: python3_pkgdir)
endif
