# Add host-specific compilation rules here
CFLAGS += -fPIC -fno-builtin $(call cc-option,-Wtrampolines)

CFLAGS += -Wextra $(call cc-option,-Wnull-dereference)

ASFLAGS += -DPIC -DSHARED -fPIC -DASSEMBLER -Wa,--noexecstack \
	  -x assembler-with-cpp
LDFLAGS += -shared -nostdlib -z combreloc -z defs \
	  --version-script $(HOST_DIR)/pal.map -T $(HOST_DIR)/arch/$(ARCH)/pal.lds \
	  --eh-frame-hdr \
	  -z relro -z now $(UBSAN_LDFLAGS)

pal_loader = $(HOST_DIR)/libpal.so
pal_lib = $(HOST_DIR)/libpal.so
pal_lib_deps = pal-symbols $(HOST_DIR)/pal.map.template $(HOST_DIR)/arch/$(ARCH)/pal.lds
pal_lib_post =
pal_static = $(HOST_DIR)/libpal.a
