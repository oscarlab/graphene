cflags_libos = [
    # TODO: check whether all standard functions implemented in Graphene match their APIs and remove
    # this
    '-fno-builtin',

    '-DIN_SHIM',
]

cflags_libos += cc.get_supported_arguments(
    # TODO: -Wnull-dereference option detection was broken in Makefile, and some problems slipped
    # through. In meson we enabled this option globally, so we need to debug before reenabling.
    '-Wno-null-dereference',

    # TODO: This is due to use of packed structs in IPC code, which triggers "taking address of
    # a packed member" warning in Clang and newer GCC. That code needs to be rewritten.
    '-Wno-address-of-packed-member',
)

# use TLS-based stack protector of GCC (we rely on the fact that LibOS reuses the same TCB as its
# underlying PAL which must have a canary in its PAL_TCB at offset 0x8, so no additional enabling
# is required in the LibOS code);
cflags_libos += cflags_custom_stack_protector

if host_machine.cpu_family() == 'x86_64'
    cflags_libos += ['-mfxsr', '-mxsave']
endif

subdir('include')
subdir('src')
