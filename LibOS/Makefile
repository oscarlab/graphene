include ../Scripts/Makefile.configs
include ../Scripts/Makefile.rules

RUNTIME_DIR = $(CURDIR)/../Runtime
SHIM_DIR = shim

define LN_SF_TO_RUNTIME_DIR_template =
$(RUNTIME_DIR)/$(notdir $(1)): $(1)
	$$(call cmd,ln_sfr)
endef

ifeq ($(LIBC),)
	# default
	LIBC = GLIBC
endif

# These are set in libc-specific makefiles included below.
LIBC_TARGETS =
LIBC_LINK_TARGETS =
CLEAN_FILES =
DISTCLEAN_FILES =

ifeq ($(LIBC),GLIBC)
	include glibc.mk
else ifeq ($(LIBC),MUSL)
	include musl.mk
else
	$(error Error: LIBC must be either GLIBC or MUSL)
endif

$(foreach lib,$(LIBC_LINK_TARGETS),$(eval $(call LN_SF_TO_RUNTIME_DIR_template,$(lib))))

.PHONY: all
all: $(LIBC_TARGETS)
	$(MAKE) -C $(SHIM_DIR) all

.PHONY: format
format:
	$(MAKE) -C $(SHIM_DIR) format

.PHONY: test
test:
	$(MAKE) -C $(SHIM_DIR) test

.PHONY: sgx-tokens
sgx-tokens:
	$(MAKE) -C $(SHIM_DIR) sgx-tokens

.PHONY: clean
clean:
	$(MAKE) -C $(SHIM_DIR) clean
	$(RM) -r $(CLEAN_FILES)

.PHONY: distclean
distclean:
	$(MAKE) -C $(SHIM_DIR) distclean
	$(RM) -r $(CLEAN_FILES) $(DISTCLEAN_FILES)
