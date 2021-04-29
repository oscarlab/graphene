cflags_libos = [
    '-fPIC',
    '-fmerge-all-constants',
    '-fno-builtin',

    '-Werror=implicit-function-declaration',
    '-Wextra',
    '-Winline',
    '-Wno-inline',
    '-Wstrict-prototypes',
    '-Wwrite-strings',

    '-DIN_SHIM',
]

foreach i : [
    '-Wtrampolines',
    '-Wnull-dereference',

    # Some of the code uses alignof on expressions, which is a GNU extension.
    # Silence Clang - it complains but does support it.
    '-Wno-gnu-alignof-expression',

    # TODO: This is due to use of packed structs in IPC code, which triggers "taking address of
    # a packed member" warning in Clang and newer GCC. That code needs to be rewritten.
    '-Wno-address-of-packed-member',
]
    if cc.has_argument(i)
        cflags_libos += i
    endif
endforeach

# use TLS-based stack protector of GCC (we rely on the fact that LibOS reuses the same TCB as its
# underlying PAL which must have a canary in its PAL_TCB at offset 0x8, so no additional enabling
# is required in the LibOS code);
# not all compilers support mstack-protector-guard, so use stack protector only if supported
cflags_libos += '-fno-stack-protector'
cflags_libos_stack_protector = [
    '-fstack-protector-strong',
    '-mstack-protector-guard=tls',
    '-mstack-protector-guard-reg=%gs',
    '-mstack-protector-guard-offset=8',
]
if cc.has_multi_arguments(cflags_libos_stack_protector)
    cflags_libos += cflags_libos_stack_protector
endif

#LDFLAGS-libsysdb.so += --version-script shim.map -T arch/$(ARCH)/shim.lds --eh-frame-hdr
#libsysdb.so: $(objs) $(filter %.map %.lds,$(LDFLAGS-$@)) \
#	     $(graphene_lib) $(pal_lib) shim.map arch/$(ARCH)/shim.lds
#	$(call cmd,ld_so_o)
# cmd_ld_so_o = ... $(filter-out pal-symbols %.map %.map.template %.lds,$^) -soname $(notdir $@)

subdir('include')
subdir('src')
