# crt_init files are needed solely for PAL regression tests. We use the default
# GCC linker script when building these tests. This script expects an entrypoint
# symbol named `_start` that prepares the stack layout (argc, argv, env) and
# jumps into `main`. So we have a trivial `user_start.S` file that contains this
# symbol and logic, and link its *.o version into each PAL regression test.
# There is no other usage for this crt_init/ dir.

user_start_src = files(join_paths('arch', host_machine.cpu_family(), 'user_start.S'))

crt_init_static_dep = declare_dependency(
    user_start_src,
)

crt_init_shared_dep = declare_dependency(
    user_start_src,
    compile_args: '-DSHARED',
)
