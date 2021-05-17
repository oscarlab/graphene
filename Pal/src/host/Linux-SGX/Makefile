include ../../../../Scripts/Makefile.configs
include Makefile.am

CFLAGS += \
	-I. \
	-I../.. \
	-I../../../../common/include \
	-I../../../../common/include/arch/$(ARCH) \
	-I../../../../common/src/crypto/mbedtls/include \
	-I../../../include \
	-I../../../include/pal \
	-I../../../include/arch/$(ARCH) \
	-I../../../include/arch/$(ARCH)/Linux \
	-I../../../include/host/Linux-SGX \
	-I../../../include/host/Linux-common \
	-Iprotected-files

host_files = libpal-Linux-SGX.a pal-sgx gdb_integration/sgx_gdb.so pal.map generated_offsets.py

graphene_lib = .lib/graphene-lib.a

.PHONY: all
all: $(host_files) tools

libpal-Linux-SGX.a: $(enclave-objs) $(enclave-asm-objs)
	$(call cmd,ar_a_o)

CFLAGS-pal-sgx = -Wl,-z,relro,-z,now -pie

gdb_integration/sgx_gdb.so: CFLAGS =
CFLAGS-gdb_integration/sgx_gdb.so = -shared -Wall -fPIC -O2 -std=c11
gdb_integration/sgx_gdb.so: gdb_integration/sgx_gdb.c
	$(call cmd,csingle)

ifeq ($(filter %clean,$(MAKECMDGOALS)),)
include $(wildcard *.d) $(wildcard gdb_integration/*.d)
endif

include ../../../../Scripts/Makefile.rules

.PHONY: tools
tools:
	$(MAKE) -C tools

CLEAN_FILES += $(notdir $(pal_static) $(pal_lib) $(pal_loader))
CLEAN_FILES += gdb_integration/sgx_gdb.so

.PHONY: clean_
clean_:
	$(RM) -r *.o *.e *.i *.s $(host_files) $(CLEAN_FILES) *.d gdb_integration/*.d signer/*.pyc \
	         __pycache__ signer/__pycache__
	$(RM) -r protected-files/*.o protected-files/*.d

.PHONY: clean
clean: clean_
	$(MAKE) -C tools $@

.PHONY: distclean
distclean: clean_
	$(MAKE) -C tools $@

.PHONY: test
test:
