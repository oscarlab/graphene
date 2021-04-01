ifneq ($(findstring x86_64,$(SYS))$(findstring linux,$(SYS)),x86_64linux)
	$(error Error: Graphene glibc patches are not supported on your architecture)
endif

GNU_MIRRORS ?= https://ftp.gnu.org/gnu/ \
               https://mirrors.kernel.org/gnu/ \
               https://mirrors.ocf.berkeley.edu/gnu/

# ------------------------------------------------------------------------------------------------
# Glibc build: patch libc.so and other C standard libraries to replace raw SYSCALL instructions
# with function calls into Graphene (plus some smaller patches for Graphene). The resulting
# libraries are symlinked under $RUNTIME_DIR.
#
# Override CC, CXX, AS in case we're compiling the rest of the project with clang.
# ------------------------------------------------------------------------------------------------
GLIBC_VERSION ?= 2.31
GLIBC_SRC = glibc-$(GLIBC_VERSION)
GLIBC_HASH = $(firstword $(shell grep $(GLIBC_SRC).tar.gz glibc-checksums))
BUILD_DIR = glibc-build
GLIBC_LIBS = \
	dlfcn/libdl.so.2 \
	libc.so \
	libc.so.6 \
	login/libutil.so.1 \
	math/libm.so.6 \
	nptl/libpthread.so.0 \
	nptl_db/libthread_db.so.1 \
	resolv/libnss_dns.so.2 \
	resolv/libresolv.so.2 \
	rt/librt.so.1
GLIBC_LIBS += elf/ld-linux-x86-64.so.2

LIBC_LINK_TARGETS = $(addprefix $(BUILD_DIR)/, $(GLIBC_LIBS))
GLIBC_RUNTIME = $(addprefix $(RUNTIME_DIR)/, $(notdir $(LIBC_LINK_TARGETS)))

GLIBC_CFLAGS = -O2 -Wp,-U_FORTIFY_SOURCE -Wno-unused-value
ifeq ($(DEBUG),1)
	GLIBC_CFLAGS += -g
endif
export GLIBC_CFLAGS

# For LibOS entry API
GLIBC_CPPFLAGS = -I$(abspath $(SHIM_DIR))/include -I$(abspath $(SHIM_DIR))/include/arch/$(ARCH)
export GLIBC_CPPFLAGS
GLIBC_DEPS = shim/include/arch/$(ARCH)/shim_entry_api.h

LIBC_TARGETS += $(LIBC_LINK_TARGETS) $(GLIBC_RUNTIME) $(BUILD_DIR)/Build.success
CLEAN_FILES += $(GLIBC_SRC) $(BUILD_DIR) $(GCC_BUILD_DIR) $(GCC_SRC) build.log gcc-build.log
DISTCLEAN_FILES += $(GLIBC_SRC).tar.gz $(GCC_SRC).tar.gz

.SECONDARY: $(BUILD_DIR)/Build.success
$(BUILD_DIR)/Build.success: $(BUILD_DIR)/Makefile
	@echo -n "Building glibc, may take a while to finish. Warning messages may show up. If this "
	@echo -n "process terminates with failures, see \"$(BUILD_DIR)/build.log\" for more "
	@echo    "information."
	CC=gcc CXX=g++ AS=gcc $(MAKE) -C $(BUILD_DIR) 2>&1 > build.log
	touch $@

$(LIBC_LINK_TARGETS): $(BUILD_DIR)/Build.success

$(BUILD_DIR)/Makefile: $(GLIBC_SRC)/.extracted
	mkdir -p $(BUILD_DIR)
	(cd $(BUILD_DIR) || exit 1; \
	CC=gcc CXX=g++ AS=gcc \
	CFLAGS=$$GLIBC_CFLAGS CPPFLAGS=$$GLIBC_CPPFLAGS \
	../$(GLIBC_SRC)/configure \
		--prefix=$(RUNTIME_DIR) \
		--with-tls \
		--without-selinux \
		--disable-test \
		--disable-nscd \
	)

GLIBC_PATCHES = \
	glibc-patches/$(GLIBC_SRC).patch

GLIBC_PATCHES_2.27 = \
	glibc-patches/hp-timing-2.27.patch

GLIBC_PATCHES_2.31 = \
	glibc-patches/hp-timing-2.31.patch

GLIBC_PATCHES += $(GLIBC_PATCHES_$(GLIBC_VERSION))

.SECONDARY: $(GLIBC_SRC)/.extracted
$(GLIBC_SRC)/.extracted: $(GLIBC_PATCHES) $(GLIBC_SRC).tar.gz $(GLIBC_DEPS)
	$(RM) -r $(GLIBC_SRC)
	tar -mxzf $(GLIBC_SRC).tar.gz
	cd $(GLIBC_SRC) && \
	for p in $(GLIBC_PATCHES); do \
		echo applying $$p; \
		patch -p1 -l < ../$$p || exit 255; \
	done
	touch $@

$(GLIBC_SRC).tar.gz:
	../Scripts/download --output $@ --sha256 $(GLIBC_HASH) $(foreach mirror,$(GNU_MIRRORS),--url $(mirror)glibc/$(GLIBC_SRC).tar.gz)

# ------------------------------------------------------------------------------------------------
# GCC build: patch libgomp.so.1 (OpenMP runtime library) to replace raw SYSCALL instruction with
# function call into Graphene. GCC is not built by default with Graphene; use `make -C LibOS gcc`
# to build it. The resulting libgomp.so.1 is symlinked under $RUNTIME_DIR. This patched version
# makes sense only on x86_64 platforms. NOTE: We'd prefer to build libgomp.so.1 alone but it is
# impossible (the only way to build it is as part of the complete GCC build).
#
# We explicitly unset CC, CXX, AS environment variables for the case we're compiling the rest of the
# project with clang. This is because in GCC build, "defining certain environment variables such as
# CC can interfere with the functioning of make" (https://gcc.gnu.org/install/build.html). Indeed,
# defining CC=gcc or CC=clang leads to errors during GCC build.
# ------------------------------------------------------------------------------------------------
GCC_VERSION ?= 10.2.0
GCC_SRC = gcc-$(GCC_VERSION)
GCC_HASH = 27e879dccc639cd7b0cc08ed575c1669492579529b53c9ff27b0b96265fa867d
GCC_BUILD_DIR = gcc-build
GCC_LIBS = x86_64-pc-linux-gnu/libgomp/.libs/libgomp.so.1
GCC_TARGET = $(addprefix $(GCC_BUILD_DIR)/, $(GCC_LIBS))
GCC_RUNTIME = $(addprefix $(RUNTIME_DIR)/, $(notdir $(GCC_TARGET)))

.SECONDARY: $(GCC_BUILD_DIR)/Build.success

$(GCC_BUILD_DIR)/Build.success: $(GCC_BUILD_DIR)/Makefile
	@echo -n "Building gcc, may take a while to finish. Warning messages may show up. If this "
	@echo -n "process terminates with failures, see \"$(GCC_BUILD_DIR)/gcc-build.log\" for more "
	@echo    "information."
	(unset CC CXX AS; $(MAKE) -C $(GCC_BUILD_DIR) 2>&1 > gcc-build.log) && touch $@

$(GCC_TARGET): $(GCC_BUILD_DIR)/Build.success

$(GCC_BUILD_DIR)/Makefile: $(GCC_SRC)/.extracted
	mkdir -p $(GCC_BUILD_DIR)
	(cd $(GCC_BUILD_DIR) || exit 1; \
	unset CC CXX AS; \
	../$(GCC_SRC)/configure --prefix=$(RUNTIME_DIR) \
		--enable-languages=c \
		--disable-multilib \
	)

$(foreach lib,$(GCC_TARGET),$(eval $(call LN_SF_TO_RUNTIME_DIR_template,$(lib))))

GCC_PATCHES = \
	gcc-patches/libgomp-replace-futex-instruction.patch

# For LibOS entry API (file symlinked in patch)
GCC_DEPS = shim/include/arch/$(ARCH)/shim_entry_api.h

.SECONDARY: $(GCC_SRC)/.extracted
$(GCC_SRC)/.extracted: $(GCC_PATCHES) $(GCC_SRC).tar.gz $(GCC_DEPS)
	$(RM) -r $(GCC_SRC)
	tar -mxzf $(GCC_SRC).tar.gz
	cd $(GCC_SRC) && \
	for p in $(GCC_PATCHES); do \
		echo applying $$p; \
		patch -p1 -l < ../$$p || exit 255; \
	done
	cd $(GCC_SRC) && ./contrib/download_prerequisites
	touch $@

$(GCC_SRC).tar.gz:
	../Scripts/download --output $@ --sha256 $(GCC_HASH) $(foreach mirror,$(GNU_MIRRORS),--url $(mirror)gcc/$(GCC_SRC)/$(GCC_SRC).tar.gz)

.PHONY: gcc
gcc: $(GCC_TARGET) $(GCC_RUNTIME)
