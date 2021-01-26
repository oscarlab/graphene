include ../../Scripts/Makefile.configs
include ../../Scripts/Makefile.rules
include ../src/host/$(PAL_HOST)/Makefile.am

# FIXME: currently disable GCC's stack protector in the common libs because they are linked with
#        different libraries/executables including those that do not provide __stack_chk_fail(),
#        e.g. PAL regression tests (in particular, avl_tree_test.c)
CFLAGS += -fno-stack-protector

CFLAGS += \
	-I../include \
	-I../include/arch/$(ARCH) \
	-I../include/arch/$(ARCH)/$(PAL_HOST) \
	-I../include/host/$(PAL_HOST) \
	-I../include/lib \
	-I../include/pal \
	-I../src/host/$(PAL_HOST) \
	-Icrypto/mbedtls/include \
	-Icrypto/mbedtls/crypto/include

CRYPTO_PROVIDER ?= mbedtls

# Select which crypto adpater you want to use here. This has to match
# the #define in pal_crypto.h.
#
# Unfortunately, we cannot use just one .c file for the adapter. The LibOS
# shim links against the crypto library, but it doesn't use Diffie-Hellman.
# If the Diffie-Hellman stubs are in the same .o file as the SHA1 stubs,
# this pulls Diffie-Hellman code into LibOS shim, resulting in unsatisfied
# symbols.
ifeq ($(CRYPTO_PROVIDER),mbedtls)
crypto_mbedtls_library_objs = \
	crypto/mbedtls/crypto/library/aes.o \
	crypto/mbedtls/crypto/library/base64.o \
	crypto/mbedtls/crypto/library/bignum.o \
	crypto/mbedtls/crypto/library/cipher.o \
	crypto/mbedtls/crypto/library/cipher_wrap.o \
	crypto/mbedtls/crypto/library/cmac.o \
	crypto/mbedtls/crypto/library/ctr_drbg.o \
	crypto/mbedtls/crypto/library/dhm.o \
	crypto/mbedtls/crypto/library/entropy.o \
	crypto/mbedtls/crypto/library/gcm.o \
	crypto/mbedtls/crypto/library/md.o \
	crypto/mbedtls/crypto/library/oid.o \
	crypto/mbedtls/crypto/library/platform_util.o \
	crypto/mbedtls/crypto/library/rsa.o \
	crypto/mbedtls/crypto/library/rsa_internal.o \
	crypto/mbedtls/crypto/library/sha256.o \
	crypto/mbedtls/library/ssl_ciphersuites.o \
	crypto/mbedtls/library/ssl_cli.o \
	crypto/mbedtls/library/ssl_msg.o \
	crypto/mbedtls/library/ssl_srv.o \
	crypto/mbedtls/library/ssl_tls.o
ifeq ($(ARCH),x86_64)
crypto_mbedtls_library_objs += \
	crypto/mbedtls/crypto/library/aesni.o
endif

objs += $(crypto_mbedtls_library_objs)
endif

MBEDTLS_VERSION ?= 2.21.0
MBEDTLS_SRC ?= mbedtls-$(MBEDTLS_VERSION).tar.gz
MBEDTLS_URI ?= https://github.com/ARMmbed/mbedtls/archive/
MBEDTLS_CHECKSUM ?= 320e930b7596ade650ae4fc9ba94b510d05e3a7d63520e121d8fdc7a21602db9

# mbedTLS uses a submodule mbedcrypto, need to download it and move under mbedtls/crypto
MBEDCRYPTO_VERSION ?= 3.1.0
MBEDCRYPTO_SRC ?= mbedcrypto-$(MBEDCRYPTO_VERSION).tar.gz
MBEDCRYPTO_URI ?= https://github.com/ARMmbed/mbed-crypto/archive/
MBEDCRYPTO_CHECKSUM ?= 7e171df03560031bc712489930831e70ae4b70ff521a609c6361f36bd5f8b76b

crypto/$(MBEDTLS_SRC):
	../../Scripts/download --output $@ --url $(MBEDTLS_URI)/$(MBEDTLS_SRC) --sha256 $(MBEDTLS_CHECKSUM)

crypto/$(MBEDCRYPTO_SRC):
	../../Scripts/download --output $@ --url $(MBEDCRYPTO_URI)/$(MBEDCRYPTO_SRC) --sha256 $(MBEDCRYPTO_CHECKSUM)

ifeq ($(DEBUG),1)
MBED_BUILD_TYPE=Debug
else
MBED_BUILD_TYPE=Release
endif

# First, build mbedtls library against system's glibc and install in ../install. This library is
# used by, for example, LibOS test cases. Second, prepare mbedtls directory to be used during PAL
# build. A custom config.h header replaces libc dependencies with PAL-specific alternatives.
crypto/mbedtls/CMakeLists.txt: crypto/$(MBEDTLS_SRC) crypto/$(MBEDCRYPTO_SRC) crypto/mbedtls-$(MBEDTLS_VERSION).diff
	$(RM) -r crypto/mbedtls
	cd crypto && tar -mxzf $(MBEDTLS_SRC)
	cd crypto && tar -mxzf $(MBEDCRYPTO_SRC)
	mv crypto/mbedtls-mbedtls-$(MBEDTLS_VERSION) crypto/mbedtls
	$(RM) -r crypto/mbedtls/crypto
	mv crypto/mbed-crypto-mbedcrypto-$(MBEDCRYPTO_VERSION) crypto/mbedtls
	mv crypto/mbedtls/mbed-crypto-mbedcrypto-3.1.0 crypto/mbedtls/crypto
	cd crypto/mbedtls && patch -p1 < ../mbedtls-$(MBEDTLS_VERSION).diff || exit 255
	mkdir crypto/mbedtls/install
	cd crypto/mbedtls && perl ./scripts/config.pl set MBEDTLS_CMAC_C && $(MAKE) CFLAGS="" SHARED=1 DESTDIR=install install .
	$(RM) crypto/mbedtls/include/mbedtls/config.h
	$(RM) crypto/mbedtls/crypto/include/mbedtls/config.h

crypto/mbedtls/include/mbedtls/config.h: crypto/config.h crypto/mbedtls/CMakeLists.txt
	cp crypto/config.h crypto/mbedtls/crypto/include/mbedtls
	cp crypto/config.h crypto/mbedtls/include/mbedtls

crypto/mbedtls/crypto/library/aes.c: crypto/mbedtls/CMakeLists.txt crypto/mbedtls/include/mbedtls/config.h
$(filter-out crypto/mbedtls/crypto/library/aes.c,$(patsubst %.o,%.c,$(crypto_mbedtls_library_objs))): crypto/mbedtls/crypto/library/aes.c

objs += \
	avl_tree.o \
	crypto/udivmodti4.o \
	debug_map.o \
	graphene/path.o \
	network/hton.o \
	network/inet_pton.o \
	stdlib/printfmt.o \
	string/atoi.o \
	string/ctype.o \
	string/memcmp.o \
	string/memcpy.o \
	string/memset.o \
	string/strchr.o \
	string/strcmp.o \
	string/strlen.o \
	string/strspn.o \
	string/strstr.o \
	string/toml_utils.o \
	string/utils.o \
	toml.o

$(addprefix $(target),crypto/adapters/mbedtls_adapter.o crypto/adapters/mbedtls_dh.o crypto/adapters/mbedtls_encoding.o): crypto/mbedtls/crypto/library/aes.c

ifeq ($(CRYPTO_PROVIDER),mbedtls)
CFLAGS += -DCRYPTO_USE_MBEDTLS
ifeq ($(ARCH),x86_64)
CFLAGS += -mrdrnd
endif
objs += crypto/adapters/mbedtls_adapter.o
objs += crypto/adapters/mbedtls_dh.o
objs += crypto/adapters/mbedtls_encoding.o
endif

.PHONY: all
all: ../include/lib/uthash.h $(target)graphene-lib.a

$(target)graphene-lib.a: $(addprefix $(target),$(objs))
	@mkdir -p $(dir $@)
	$(call cmd,ar_a_o)

$(target)%.o: %.c toml.h
	@mkdir -p $(dir $@)
	$(call cmd,cc_o_c)

ifeq ($(filter %clean,$(MAKECMDGOALS)),)
-include $(patsubst %.o,%.d,$(addprefix $(target),$(objs)))
endif

UTHASH_URI ?= https://raw.githubusercontent.com/troydhanson/uthash/8b214aefcb81df86a7e5e0d4fa20e59a6c18bc02/src/uthash.h
UTHASH_CHECKSUM ?= ba9af0e8c902108cc40be8e742ff4fcbb0e93062d91aefd6070b70d4e067c2ac

../include/lib/uthash.h:
	../../Scripts/download --output $@ --url $(UTHASH_URI) --sha256 $(UTHASH_CHECKSUM)

TOML_H_URI ?= https://raw.githubusercontent.com/cktan/tomlc99/5be06807ad5f2230cad99e15380c4f4076c9dd83/toml.h
TOML_H_CHECKSUM ?= e79d6d272576561e1b46ee001b0dd6b554330843e6fc7c658a548c659f282ac5
TOML_C_URI ?= https://raw.githubusercontent.com/cktan/tomlc99/5be06807ad5f2230cad99e15380c4f4076c9dd83/toml.c
TOML_C_CHECKSUM ?= c21a546ab767a7e40d4f65df179d70357059c15e4439e1980d791625655fbbc6

toml.h.$(TOML_H_CHECKSUM):
	../../Scripts/download --output $@ --url $(TOML_H_URI) --sha256 $(TOML_H_CHECKSUM)

toml.c.$(TOML_C_CHECKSUM):
	../../Scripts/download --output $@ --url $(TOML_C_URI) --sha256 $(TOML_C_CHECKSUM)

.PHONY: .toml_sentinel
toml.c toml.h: .toml_sentinel ;

.toml_sentinel: toml.h.$(TOML_H_CHECKSUM) toml.c.$(TOML_C_CHECKSUM)
	cp toml.h.$(TOML_H_CHECKSUM) toml.h
	cp toml.c.$(TOML_C_CHECKSUM) toml.c
	patch -p1 -l < toml.patch || exit 255
	cp toml.h ../include/lib/toml.h

.PHONY: clean
clean:
	$(RM) $(objs) graphene-lib.a

.PHONY: distclean
distclean: clean
	$(RM) -r crypto/$(MBEDTLS_SRC) crypto/$(MBEDCRYPTO_SRC) crypto/mbedtls
	$(RM) ../include/lib/uthash.h
	$(RM) ../include/lib/toml.h toml.h* toml.c*
