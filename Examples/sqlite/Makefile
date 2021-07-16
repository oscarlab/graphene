# Use one of these commands to build the manifest for sqlite:
#
# - make
# - make DEBUG=1
# - make SGX=1
# - make SGX=1 DEBUG=1
#
# Use `make clean` to remove Graphene-generated files.

# Relative path to Graphene root and key for enclave signing
GRAPHENEDIR ?= ../..
SGX_SIGNER_KEY ?= $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/enclave-key.pem

ifeq ($(DEBUG),1)
GRAPHENE_LOG_LEVEL = debug
else
GRAPHENE_LOG_LEVEL = error
endif

.PHONY: all
all: sqlite3.manifest
ifeq ($(SGX),1)
all: sqlite3.manifest.sgx sqlite3.sig sqlite3.token
endif

include ../../Scripts/Makefile.configs

sqlite3.manifest: manifest.template
	graphene-manifest \
		-Dlog_level=$(GRAPHENE_LOG_LEVEL) \
		-Dexecdir=$(shell dirname $(shell which sqlite3)) \
		-Darch_libdir=$(ARCH_LIBDIR) \
		$< >$@

# Generating the SGX-specific manifest (*.manifest.sgx), the enclave signature,
# and the token for enclave initialization.
sqlite3.manifest.sgx: sqlite3.manifest
	graphene-sgx-sign \
		--key $(SGX_SIGNER_KEY) \
		--manifest sqlite3.manifest \
		--output $@

sqlite3.sig: sqlite3.manifest.sgx

sqlite3.token: sqlite3.sig
	graphene-sgx-get-token --output sqlite3.token --sig sqlite3.sig

ifeq ($(SGX),)
GRAPHENE = graphene-direct
else
GRAPHENE = graphene-sgx
endif

.PHONY: regression
regression: all
	@rm -f scripts/testdir/*

	$(GRAPHENE) sqlite3 scripts/testdir/test.db < scripts/create.sql
	@echo "[ Success 1/3 ]"

	$(GRAPHENE) sqlite3 scripts/testdir/test.db < scripts/update.sql
	@echo "[ Success 2/3 ]"

	$(GRAPHENE) sqlite3 scripts/testdir/test.db < scripts/select.sql > OUTPUT
	diff OUTPUT scripts/select.txt
	@echo "[ Success 3/3 ]"

.PHONY: clean
clean:
	$(RM) *.manifest *.manifest.sgx *.token *.sig OUTPUT scripts/testdir/*

.PHONY: distclean
distclean: clean
