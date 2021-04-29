include ../../Scripts/Makefile.configs

MAKEFLAGS += --check-symlink-times

.PHONY: all
all:
	$(MAKE) -C src $@

.PHONY: test
test:
	$(MAKE) -C test

.PHONY: sgx-tokens
sgx-tokens:
	$(MAKE) -C test sgx-tokens

clean_targets = clean distclean
.PHONY: $(clean_targets)
$(clean_targets):
	$(MAKE) -C src $@
	$(MAKE) -C test $@

.PHONY: format
format:
	clang-format -i $(shell find . -path ./test/ltp -prune -o \( -name '*.h' -o -name '*.c' \) -print)
