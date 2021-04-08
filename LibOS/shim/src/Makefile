RUNTIME_DIR = $(CURDIR)/../../../Runtime

include ../../../Scripts/Makefile.configs

CFLAGS += -fPIC -Winline -Wwrite-strings \
	  -fmerge-all-constants -Wstrict-prototypes $(call cc-option,-Wtrampolines) \
	  -Werror=implicit-function-declaration \
	  $(cc-option, -Wnull-dereference) \
	  -fno-builtin -Wno-inline \
	  -I../include \
	  -I../include/arch/$(ARCH) \
	  -I../../../Pal/include \
	  -I../../../Pal/include/arch/$(ARCH) \
	  -I../../../Pal/include/arch/$(ARCH)/$(PAL_HOST) \
	  -I../../../Pal/include/elf \
	  -I../../../Pal/include/lib \
	  -I../../../Pal/include/pal

CFLAGS += -Wextra

# use TLS-based stack protector of GCC (we rely on the fact that LibOS reuses the same TCB as its
# underlying PAL which must have a canary in its PAL_TCB at offset 0x8, so no additional enabling
# is required in the LibOS code);
# not all compilers support mstack-protector-guard, so use stack protector only if supported
CFLAGS += -fno-stack-protector
ifeq ($(ARCH),x86_64)
CFLAGS += $(call cc-option, -fstack-protector-strong -mstack-protector-guard=tls \
	                        -mstack-protector-guard-reg=%gs -mstack-protector-guard-offset=8)
endif

# Some of the code uses alignof on expressions, which is a GNU extension.
# Silence Clang - it complains but does support it.
CFLAGS += $(cc-option,-Wno-gnu-alignof-expression)

# TODO: This is due to use of packed structs in IPC code, which triggers "taking address of a packed
# member" warning in Clang and newer GCC. That code needs to be rewritten.
CFLAGS += -Wno-address-of-packed-member

ASFLAGS += -Wa,--noexecstack -x assembler-with-cpp -I../include -I../include/arch/$(ARCH)

LDFLAGS += -shared -nostdlib -z combreloc -z relro -z now -z defs \
	  -rpath-link=$(abspath $(RUNTIME_DIR)) $(UBSAN_LDFLAGS)

files_to_build = libsysdb.a libsysdb.so
files_to_install = $(addprefix $(RUNTIME_DIR)/,$(files_to_build))

defs	= -DIN_SHIM
CFLAGS += $(defs)
ASFLAGS += $(defs)

arch_objs = \
	shim_arch_prctl.o \
	shim_context.o \
	shim_table.o \
	start.o \
	syscallas.o

objs = \
	shim_async.o \
	shim_checkpoint.o \
	shim_debug.o \
	shim_init.o \
	shim_malloc.o \
	shim_object.o \
	shim_parser.o \
	shim_rtld.o \
	shim_syscalls.o \
	bookkeep/shim_handle.o \
	bookkeep/shim_process.o \
	bookkeep/shim_signal.o \
	bookkeep/shim_thread.o \
	bookkeep/shim_vma.o \
	fs/shim_dcache.o \
	fs/shim_fs.o \
	fs/shim_fs_hash.o \
	fs/shim_fs_pseudo.o \
	fs/shim_namei.o \
	fs/chroot/fs.o \
	fs/dev/attestation.o \
	fs/dev/fs.o \
	fs/dev/null.o \
	fs/dev/random.o \
	fs/dev/std.o \
	fs/dev/zero.o \
	fs/eventfd/fs.o \
	fs/pipe/fs.o \
	fs/proc/fs.o \
	fs/proc/info.o \
	fs/proc/ipc-thread.o \
	fs/proc/thread.o \
	fs/socket/fs.o \
	fs/str/fs.o \
	fs/sys/cache_info.o \
	fs/sys/cpu_info.o \
	fs/sys/fs.o \
	fs/sys/node_info.o \
	fs/tmpfs/fs.o \
	ipc/shim_ipc.o \
	ipc/shim_ipc_child.o \
	ipc/shim_ipc_helper.o \
	ipc/shim_ipc_pid.o \
	ipc/shim_ipc_ranges.o \
	ipc/shim_ipc_sysv.o \
	sys/shim_access.o \
	sys/shim_alarm.o \
	sys/shim_brk.o \
	sys/shim_clone.o \
	sys/shim_dup.o \
	sys/shim_epoll.o \
	sys/shim_eventfd.o \
	sys/shim_exec.o \
	sys/shim_exit.o \
	sys/shim_fcntl.o \
	sys/shim_fork.o \
	sys/shim_fs.o \
	sys/shim_futex.o \
	sys/shim_getcwd.o \
	sys/shim_getpid.o \
	sys/shim_getrandom.o \
	sys/shim_getrlimit.o \
	sys/shim_getuid.o \
	sys/shim_ioctl.o \
	sys/shim_mmap.o \
	sys/shim_msgget.o \
	sys/shim_open.o \
	sys/shim_pipe.o \
	sys/shim_poll.o \
	sys/shim_sched.o \
	sys/shim_semget.o \
	sys/shim_sigaction.o \
	sys/shim_sleep.o \
	sys/shim_socket.o \
	sys/shim_stat.o \
	sys/shim_time.o \
	sys/shim_uname.o \
	sys/shim_wait.o \
	sys/shim_wrappers.o \
	utils/log.o \
	utils/strobjs.o \
	vdso/vdso-data.o \
	$(addprefix arch/$(ARCH)/,$(arch_objs))

all_objs = \
	$(objs) \
	vdso/vdso-note.o \
	vdso/arch/$(ARCH)/vdso.o

graphene_lib = .lib/graphene-lib.a
pal_lib = $(RUNTIME_DIR)/libpal-$(PAL_HOST).so

ifeq ($(ARCH),x86_64)
	CFLAGS += -mfxsr -mxsave
endif

ifeq ($(findstring x86_64,$(SYS))$(findstring linux,$(SYS)),x86_64linux)
all: $(files_to_build) $(files_to_install)
else
all:
endif

$(files_to_install): $(RUNTIME_DIR)/%: %
	$(call cmd,ln_sfr)

LDFLAGS-libsysdb.so += --version-script shim.map -T arch/$(ARCH)/shim.lds --eh-frame-hdr
libsysdb.so: $(objs) $(filter %.map %.lds,$(LDFLAGS-$@)) \
	     $(graphene_lib) $(pal_lib) shim.map arch/$(ARCH)/shim.lds
	$(call cmd,ld_so_o)

.PHONY: graphene_lib_recurse
graphene_lib_recurse:
	$(MAKE) -C ../../../Pal/lib target=$(abspath .lib)/

$(graphene_lib): graphene_lib_recurse
	@true

libsysdb.a: $(objs)
	$(call cmd,ar_a_o)

%.o: %.c
	$(call cmd,cc_o_c)

%.i: %.c
	$(call cmd,cpp_i_c)

%.s: %.c
	$(call cmd,cc_s_c)

%.o: %.S
	$(call cmd,as_o_S)

%.s: %.S
	$(call cmd,cpp_s_S)

arch/$(ARCH)/asm-offsets.h: asm-offsets.h
	ln -fs ../../$^ arch/$(ARCH)/asm-offsets.h
CLEAN_FILES += arch/$(ARCH)/asm-offsets.h

arch/$(ARCH)/syscallas.S arch/$(ARCH)/shim_context.c: arch/$(ARCH)/asm-offsets.h

include ../../../Scripts/Makefile.rules

LDFLAGS-vdso/vdso.so.dbg = -nostdlib -shared \
	--hash-style=both --build-id -Bsymbolic \
	-m elf_x86_64 --no-undefined \
	-z max-page-size=4096 -z common-page-size=4096 \
	-T vdso/arch/$(ARCH)/vdso.lds -soname linux-vdso.so.1
vdso/vdso.so.dbg: LDFLAGS =
vdso/vdso.so.dbg: vdso/arch/$(ARCH)/vdso.lds vdso/arch/$(ARCH)/vdso.o vdso/vdso-note.o | vdso/.vdso-no-reloc-checked
	$(call cmd,ld)

# vdso.so is required to have no relocations. This rule checks it.
# Use default linker script to retain relocations if exist.
LDFLAGS-vdso/.vdso-linked-for-reloc-check.so = -nostdlib -shared -Bsymbolic
vdso/.vdso-linked-for-reloc-check.so: LDFLAGS =
vdso/.vdso-linked-for-reloc-check.so: vdso/arch/$(ARCH)/vdso.o
	$(call cmd,ld)

vdso/.vdso-no-reloc-checked: vdso/.vdso-linked-for-reloc-check.so
	$(call cmd,check_no_reloc)
	$Q touch $@

OBJCOPYFLAGS-vdso/vdso.so = -S
vdso/vdso.so: vdso/vdso.so.dbg
	$(call cmd,objcopy)

vdso/vdso-data.o: vdso/vdso.so
CLEAN_FILES += vdso/vdso.so.dbg vdso/vdso.so vdso/.vdso-linked-for-reloc-check.so vdso/.vdso-no-reloc-checked

.PHONY: clean
clean:
	$(RM) -r $(all_objs) $(patsubst %.o,%.d,$(all_objs)) \
	         $(patsubst %.o,%.i.d,$(all_objs)) $(patsubst %.o,%.s.d,$(all_objs)) \
	         $(shim_target) $(files_to_build) .lib $(CLEAN_FILES)

ifeq ($(filter %clean,$(MAKECMDGOALS)),)
-include $(patsubst %.o,%.d,$(all_objs)) $(patsubst %.o,%.i.d,$(all_objs)) $(patsubst %.o,%.s.d,$(all_objs))
endif

.PHONY: distclean
distclean: clean
