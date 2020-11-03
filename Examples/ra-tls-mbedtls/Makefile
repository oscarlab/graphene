# Build mbedTLS client/server example with RA-TLS as follows:
#
# - make          -- build SGX version with apps built in release mode and no logs
# - make DEBUG=1  -- build SGX version with apps built in debug mode and with logs
#
# Any of these invocations clones mbedTLS' git repository (version 2.21.0) and
# builds it in default configuration. Also, server and client programs are
# built. See README for details.
#
# Use `make clean` to remove Graphene-generated files and `make distclean` to
# additionally remove the cloned mbedTLS git repository.

# Relative path to Graphene root
GRAPHENEDIR ?= ../..
GRAPHENEKEY ?= $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/enclave-key.pem

ARCH_LIBDIR ?= /lib/x86_64-linux-gnu

# for EPID attestation, specify your SPID and linkable/unlinkable attestation policy;
# for DCAP/ECDSA attestation, specify SPID as empty string (linkable value is ignored)
RA_CLIENT_SPID ?=
RA_CLIENT_LINKABLE ?= 0

ifeq ($(DEBUG),1)
GRAPHENEDEBUG=inline
CFLAGS += -O0 -ggdb3
else
GRAPHENEDEBUG=none
CFLAGS += -O2
endif

.PHONY: all
all: app epid  # by default, only build EPID because it doesn't rely on additional (DCAP) libs

.PHONY: app
app: mbedtls/CMakeLists.txt server.manifest.sgx server.token client pal_loader

.PHONY: epid
epid: libra_tls_attest.so libra_tls_verify_epid.so client_epid.sig

.PHONY: dcap
dcap: libra_tls_attest.so libra_tls_verify_dcap.so client_dcap.token

############################# MBEDTLS DEPENDENCY ##############################

MBEDTLS_VERSION ?= 2.21.0
MBEDTLS_SRC ?= mbedtls-$(MBEDTLS_VERSION).tar.gz
MBEDTLS_URI ?= https://github.com/ARMmbed/mbedtls/archive/
MBEDTLS_CHECKSUM ?= 320e930b7596ade650ae4fc9ba94b510d05e3a7d63520e121d8fdc7a21602db9

# mbedTLS uses a submodule mbedcrypto, need to download it and move under mbedtls/crypto
MBEDCRYPTO_VERSION ?= 3.1.0
MBEDCRYPTO_SRC ?= mbedcrypto-$(MBEDCRYPTO_VERSION).tar.gz
MBEDCRYPTO_URI ?= https://github.com/ARMmbed/mbed-crypto/archive/
MBEDCRYPTO_CHECKSUM ?= 7e171df03560031bc712489930831e70ae4b70ff521a609c6361f36bd5f8b76b

ifeq ($(DEBUG),1)
MBED_BUILD_TYPE=Debug
else
MBED_BUILD_TYPE=Release
endif

$(MBEDTLS_SRC):
	$(GRAPHENEDIR)/Scripts/download --output $@ --url $(MBEDTLS_URI)/$(MBEDTLS_SRC) \
		--sha256 $(MBEDTLS_CHECKSUM)

$(MBEDCRYPTO_SRC):
	$(GRAPHENEDIR)/Scripts/download --output $@ --url $(MBEDCRYPTO_URI)/$(MBEDCRYPTO_SRC) \
		--sha256 $(MBEDCRYPTO_CHECKSUM)

mbedtls/CMakeLists.txt: $(MBEDTLS_SRC) $(MBEDCRYPTO_SRC)
	tar --touch -xzf $(MBEDTLS_SRC)
	tar --touch -xzf $(MBEDCRYPTO_SRC)
	mv mbedtls-mbedtls-$(MBEDTLS_VERSION) mbedtls
	$(RM) -r mbedtls/crypto
	mv mbed-crypto-mbedcrypto-$(MBEDCRYPTO_VERSION) mbedtls
	mv mbedtls/mbed-crypto-mbedcrypto-$(MBEDCRYPTO_VERSION) mbedtls/crypto
	mkdir mbedtls/install
	cd mbedtls && ./scripts/config.pl set MBEDTLS_CMAC_C && make SHARED=1 DESTDIR=install install .

######################### CLIENT/SERVER EXECUTABLES ###########################

CFLAGS += -I./mbedtls/install/include -I./mbedtls/crypto/include
LFLAGS += -Wl,-rpath,. -L. -ldl -lmbedcrypto -lmbedtls -lmbedx509

server: src/server.c libmbedcrypto.so libmbedtls.so libmbedx509.so
	$(CC) $< $(CFLAGS) $(LFLAGS) -o $@

client: src/client.c libmbedcrypto.so libmbedtls.so libmbedx509.so
	$(CC) $< $(CFLAGS) $(LFLAGS) -o $@

########################### COPIES FOR CONVENIENCE ############################

pal_loader:
	ln -s $(GRAPHENEDIR)/Runtime/$@ .

libmbedcrypto.so: mbedtls/CMakeLists.txt
	cp mbedtls/install/lib/$@.* .
	ln -s $@.* $@

libmbedtls.so: mbedtls/CMakeLists.txt
	cp mbedtls/install/lib/$@.* .
	ln -s $@.* $@

libmbedx509.so: mbedtls/CMakeLists.txt
	cp mbedtls/install/lib/$@.* .
	ln -s $@.* $@

libsgx_util.so:
	cp $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/tools/common/$@ .

libra_tls_attest.so:
	cp $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/tools/ra-tls/$@ .

libra_tls_verify_epid.so: libmbedcrypto.so libmbedx509.so libsgx_util.so
	cp $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/tools/ra-tls/$@ .

libra_tls_verify_dcap.so: libmbedcrypto.so libmbedx509.so libsgx_util.so
	cp $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/tools/ra-tls/$@ .

libra_tls_verify_dcap_graphene.so: libmbedcrypto.so libmbedx509.so libsgx_util.so
	cp $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/tools/ra-tls/$@ .

############################### SERVER MANIFEST ###############################

server.manifest: server.manifest.template
	sed -e 's|$$(GRAPHENEDIR)|'"$(GRAPHENEDIR)"'|g' \
		-e 's|$$(GRAPHENEDEBUG)|'"$(GRAPHENEDEBUG)"'|g' \
		-e 's|$$(RA_CLIENT_SPID)|'"$(RA_CLIENT_SPID)"'|g' \
		-e 's|$$(RA_CLIENT_LINKABLE)|'"$(RA_CLIENT_LINKABLE)"'|g' \
		$< > $@

server.manifest.sgx: server.manifest server libra_tls_attest.so mbedtls/CMakeLists.txt
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-sign \
		-libpal $(GRAPHENEDIR)/Runtime/libpal-Linux-SGX.so \
		-key $(GRAPHENEKEY) \
		-manifest $< -output $@ \
		-exec server

server.sig: server.manifest.sgx

server.token: server.sig
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-get-token -output $@ -sig $<

########################### CLIENT (DCAP) MANIFEST ############################

# DCAP dependencies (generated from ldd). For SGX, the manifest needs to list all the libraries
# loaded during execution, so that the signer can include the file hashes.

# We need to replace Glibc dependencies with Graphene-specific Glibc. The Glibc binaries are
# already listed in the manifest template, so we can skip them from the ldd results.
GLIBC_DEPS = linux-vdso.so.1 /lib64/ld-linux-x86-64.so.2 libc.so.6 libm.so.6 librt.so.1 \
			 libdl.so.2 libpthread.so.0 libutil.so.1 libresolv.so.2 libnss_dns.so.2

# List all the dcap dependencies, besides Glibc libraries
.INTERMEDIATE: client_dcap-list
client_dcap-list: client libra_tls_verify_dcap_graphene.so
	@ldd client > $@
	@ldd libra_tls_verify_dcap_graphene.so >> $@
	@ldd /usr/$(ARCH_LIBDIR)/libdcap_quoteprov.so >> $@

.INTERMEDIATE: client_dcap-deps
client_dcap-deps: client_dcap-list
	@cat $< | \
		awk '{if ($$2 =="=>") {print $$3}}' | \
		sort | uniq | grep -v $(patsubst %,-e %,$(GLIBC_DEPS)) > $@
	@echo "/usr/$(ARCH_LIBDIR)/libdcap_quoteprov.so.1" >> $@

# Generate manifest rules for dcap dependencies
.INTERMEDIATE: client_dcap-trusted-libs
client_dcap-trusted-libs: client_dcap-deps
	@for F in `cat $<`; do \
		N=`basename $$F | tr --delete '.' | tr --delete '-' | tr --delete '+'`; \
		echo -n "sgx.trusted_files.$$N = file:$$F\\\\n"; \
	done > $@

client_dcap.manifest: client.manifest.template client_dcap-trusted-libs
	sed -e 's|$$(GRAPHENEDIR)|'"$(GRAPHENEDIR)"'|g' \
		-e 's|$$(GRAPHENEDEBUG)|'"$(GRAPHENEDEBUG)"'|g' \
		-e 's|$$(CLIENT_TRUSTED_LIBS)|'"`cat client_dcap-trusted-libs`"'|g' \
		-e 's|$$(RA_TLS_VERIFY_LIB)|'"libra_tls_verify_dcap_graphene.so"'|g' \
		-e 's|$$(ARCH_LIBDIR)|'"$(ARCH_LIBDIR)"'|g' \
		$< > $@

client_dcap.manifest.sgx: client_dcap.manifest mbedtls/CMakeLists.txt
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-sign \
		-libpal $(GRAPHENEDIR)/Runtime/libpal-Linux-SGX.so \
		-key $(GRAPHENEKEY) \
		-manifest $< -output $@ \
		-exec client

client_dcap.sig: client_dcap.manifest.sgx

client_dcap.token: client_dcap.sig
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-get-token -output $@ -sig $<

########################### CLIENT (EPID) MANIFEST ############################

# List all the epid dependencies, besides Glibc libraries
.INTERMEDIATE: client_epid-list
client_epid-list: client libra_tls_verify_epid.so
	@ldd client > $@
	@ldd libra_tls_verify_epid.so >> $@

.INTERMEDIATE: client_epid-deps
client_epid-deps: client_epid-list
	@cat $< | \
		awk '{if ($$2 =="=>") {print $$3}}' | \
		sort | uniq | grep -v $(patsubst %,-e %,$(GLIBC_DEPS)) > $@

# Generate manifest rules for epid dependencies
.INTERMEDIATE: client_epid-trusted-libs
client_epid-trusted-libs: client_epid-deps
	@for F in `cat $<`; do \
		N=`basename $$F | tr --delete '.' | tr --delete '-' | tr --delete '+'`; \
		echo -n "sgx.trusted_files.$$N = file:$$F\\\\n"; \
	done > $@

client_epid.manifest: client.manifest.template client_epid-trusted-libs
	sed -e 's|$$(GRAPHENEDIR)|'"$(GRAPHENEDIR)"'|g' \
		-e 's|$$(GRAPHENEDEBUG)|'"$(GRAPHENEDEBUG)"'|g' \
		-e 's|$$(CLIENT_TRUSTED_LIBS)|'"`cat client_epid-trusted-libs`"'|g' \
		-e 's|$$(RA_TLS_VERIFY_LIB)|'"libra_tls_verify_epid.so"'|g' \
		-e 's|$$(ARCH_LIBDIR)|'"$(ARCH_LIBDIR)"'|g' \
		$< > $@

client_epid.manifest.sgx: client_epid.manifest
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-sign \
		-libpal $(GRAPHENEDIR)/Runtime/libpal-Linux-SGX.so \
		-key $(GRAPHENEKEY) \
		-manifest $< -output $@ \
		-exec client

client_epid.sig: client_epid.manifest.sgx

client_epid.token: client_epid.sig
	$(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/pal-sgx-get-token -output $@ -sig $<

############################### SGX CHECKS FOR CI #############################

.PHONY: check_epid
check_epid: app epid
	SGX=1 ./pal_loader server epid >/dev/null & SERVER_ID=$$!; \
	sleep 30; \
	./client epid > OUTPUT; \
	./client epid 0 0 0 0 >> OUTPUT; \
	kill -9 $$SERVER_ID
	@grep -q "using default SGX-measurement verification callback" OUTPUT && echo "[ Success 1/4 ]"
	@grep -q "using our own SGX-measurement verification callback" OUTPUT && echo "[ Success 2/4 ]"
	@grep -q "Verifying peer X.509 certificate... ok" OUTPUT && echo "[ Success 3/4 ]"
	@(exit `grep -c "failed" "OUTPUT"`) && echo "[ Success 4/4 ]"
	@rm OUTPUT

.PHONY: check_epid_fail
check_epid_fail: app epid
	SGX=1 ./pal_loader server epid dummy-option >/dev/null & SERVER_ID=$$!; \
	sleep 30; \
	./client epid && exit 1 || echo "[ Success 1/1 ]"; \
	kill -9 $$SERVER_ID

.PHONY: check_dcap
check_dcap: app dcap
	SGX=1 ./pal_loader server dcap >/dev/null & SERVER_ID=$$!; \
	sleep 30; \
	./client dcap > OUTPUT; \
	./client dcap 0 0 0 0 >> OUTPUT; \
	kill -9 $$SERVER_ID
	@grep -q "using default SGX-measurement verification callback" OUTPUT && echo "[ Success 1/4 ]"
	@grep -q "using our own SGX-measurement verification callback" OUTPUT && echo "[ Success 2/4 ]"
	@grep -q "Verifying peer X.509 certificate... ok" OUTPUT && echo "[ Success 3/4 ]"
	@(exit `grep -c "failed" "OUTPUT"`) && echo "[ Success 4/4 ]"
	@rm OUTPUT

.PHONY: check_dcap_fail
check_dcap_fail: app dcap
	SGX=1 ./pal_loader server dcap dummy-option >/dev/null & SERVER_ID=$$!; \
	sleep 30; \
	./client dcap && exit 1 || echo "[ Success 1/1 ]"; \
	kill -9 $$SERVER_ID

################################## CLEANUP ####################################

.PHONY: clean
clean:
	$(RM) *.token *.sig *.manifest.sgx *.manifest pal_loader server client *.so *.so.* OUTPUT *.json

.PHONY: distclean
distclean: clean
	$(RM) -r mbedtls *.tar.gz
