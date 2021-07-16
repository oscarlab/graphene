# ResNet50 sample for Tensorflow

GRAPHENEDIR ?= ../../..
SGX_SIGNER_KEY ?= $(GRAPHENEDIR)/Pal/src/host/Linux-SGX/signer/enclave-key.pem

include $(GRAPHENEDIR)/Scripts/Makefile.configs

ifeq ($(DEBUG),1)
GRAPHENE_LOG_LEVEL = debug
else
GRAPHENE_LOG_LEVEL = error
endif

.PHONY: all collateral
all: python.manifest
ifeq ($(SGX),1)
all: python.manifest.sgx python.sig python.token
endif

collateral:
	test -d models || git clone https://github.com/IntelAI/models.git
	test -f resnet50v1_5_int8_pretrained_model.pb || wget https://storage.googleapis.com/intel-optimized-tensorflow/models/v1_8/resnet50v1_5_int8_pretrained_model.pb

python.manifest: python.manifest.template collateral
	graphene-manifest \
		-Dlog_level=$(GRAPHENE_LOG_LEVEL) \
		-Darch_libdir=$(ARCH_LIBDIR) \
		-Dentrypoint=$(realpath $(shell sh -c "command -v python3")) \
		-Dpythondistpath=$(PYTHONDISTPATH) \
		$< >$@

python.manifest.sgx: python.manifest
	graphene-sgx-sign \
		--key $(SGX_SIGNER_KEY) \
		--manifest python.manifest \
		--output $@

python.sig: python.manifest.sgx

python.token: python.sig
	graphene-sgx-get-token -output $@ -sig $<

.PHONY: clean
clean:
	$(RM) *.manifest *.manifest.sgx *.token *.sig

.PHONY: distclean
distclean: clean
	$(RM) -r models/ resnet50v1_5_int8_pretrained_model.pb
