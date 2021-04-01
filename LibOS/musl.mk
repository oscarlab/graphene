ifneq ($(findstring x86_64,$(SYS))$(findstring linux,$(SYS)),x86_64linux)
	$(error Error: Graphene musl patches are not supported on your architecture)
endif


MUSL_VERSION ?= 1.2.2
MUSL_HASH = 9b969322012d796dc23dda27a35866034fa67d8fb67e0e2c45c913c3d43219dd
MUSL_SRC = musl-$(MUSL_VERSION)
MUSL_MIRRORS ?= https://musl.libc.org/releases/
MUSL_DEPS =
MUSL_BUILD_DIR = musl-build
# For LibOS entry API
MUSL_CFLAGS = -I$(abspath $(SHIM_DIR))/include -I$(abspath $(SHIM_DIR))/include/arch/$(ARCH)
export MUSL_CFLAGS
CLEAN_FILES +=
MUSL_TARGETS = \
	$(MUSL_BUILD_DIR)/lib/libc.so
LIBC_LINK_TARGETS = \
	$(MUSL_TARGETS) \
	$(MUSL_BUILD_DIR)/lib/libc.so.6 \
	$(MUSL_BUILD_DIR)/lib/ld-linux-x86-64.so.2 \
	$(MUSL_BUILD_DIR)/libresolv.so.2
MUSL_PATCHES = \
	musl-patches/musl.patch

$(MUSL_SRC).tar.gz:
	../Scripts/download --output $@ --sha256 $(MUSL_HASH) \
	    $(foreach mirror,$(MUSL_MIRRORS),--url $(mirror)$(MUSL_SRC).tar.gz)
MUSL_RUNTIME_LINKS = $(LIBC_LINK_TARGETS)
LIBC_TARGETS += $(addprefix $(RUNTIME_DIR)/, $(notdir $(LIBC_LINK_TARGETS))) $(MUSL_BUILD_DIR)/.built

.SECONDARY: $(MUSL_SRC)/.extracted
$(MUSL_SRC)/.extracted: $(MUSL_PATCHES) $(MUSL_SRC).tar.gz $(MUSL_DEPS)
	$(RM) -r $(MUSL_SRC)
	tar -mxzf $(MUSL_SRC).tar.gz
	cd $(MUSL_SRC) && \
	for p in $(MUSL_PATCHES); do \
		echo applying $$p; \
		patch -p1 -l < ../$$p || exit 255; \
	done
	touch $@

MUSL_CONFIGURE_FLAGS =
ifeq ($(DEBUG),1)
	MUSL_CONFIGURE_FLAGS += --enable-debug
endif

.SECONDARY: $(MUSL_BUILD_DIR)/.configured
$(MUSL_BUILD_DIR)/.configured: $(MUSL_SRC)/.extracted
	mkdir -p $(MUSL_BUILD_DIR)
	(cd $(MUSL_BUILD_DIR) || exit 1; \
	CC=gcc CXX=g++ AS=gcc \
	CFLAGS=$$MUSL_CFLAGS \
	../$(MUSL_SRC)/configure \
		--prefix=$(RUNTIME_DIR) \
		--enable-optimize \
		$(MUSL_CONFIGURE_FLAGS) \
		--disable-static \
	)
	touch $@

.SECONDARY: $(MUSL_BUILD_DIR)/.built
$(MUSL_BUILD_DIR)/.built: $(MUSL_BUILD_DIR)/.configured
	CC=gcc CXX=g++ AS=gcc \
	CFLAGS=$$MUSL_CFLAGS \
	$(MAKE) -C $(MUSL_BUILD_DIR) 2>&1 > build.log
	touch $@

$(MUSL_BUILD_DIR)/lib/libc.so.6: $(MUSL_BUILD_DIR)/lib/libc.so
	ln -sfr $< $@

$(MUSL_BUILD_DIR)/lib/ld-linux-x86-64.so.2: $(MUSL_BUILD_DIR)/lib/libc.so
	ln -sfr $< $@

$(MUSL_BUILD_DIR)/libresolv.so.2: $(MUSL_BUILD_DIR)/lib/libc.so
	ln -sfr $< $@

$(MUSL_TARGETS): $(MUSL_BUILD_DIR)/.built
	@:
