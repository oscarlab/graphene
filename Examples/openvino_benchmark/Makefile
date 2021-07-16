# Building the manifest for OpenVINO:
#
# - make                Build for Linux, OpenVINO with Release build
# - make DEBUG=1        Build for Linux, OpenVINO with Debug build, Graphene with debug output
# - make SGX=1          Build for SGX, OpenVINO with Release build
# - make SGX=1 DEBUG=1  Build for SGX, OpenVINO with Debug build, Graphene with debug output
#
# Use `make clean` to remove Graphene-generated files.
# Use `make distclean` to further remove model directory and venv directory
#
OPENVINO_DIR ?= $(INTEL_OPENVINO_DIR)

THIS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

MODEL_DIR ?= $(THIS_DIR)model
MODEL_RESNET ?= resnet-50-tf
MODEL_BERT ?= bert-large-uncased-whole-word-masking-squad-0001
MODEL_BERT_INT8 ?= bert-large-uncased-whole-word-masking-squad-int8-0001
MODEL_BRATS0001 ?= brain-tumor-segmentation-0001
MODEL_BRATS0002 ?= brain-tumor-segmentation-0002
MODEL_SSD_COCO ?= ssd_mobilenet_v1_coco

VENV_DIR ?= $(THIS_DIR)/openvino

# Relative path to Graphene root and key for enclave signing
GRAPHENE_DIR ?= ../..
SGX_SIGNER_KEY ?= $(GRAPHENE_DIR)/Pal/src/host/Linux-SGX/signer/enclave-key.pem

ifeq ($(DEBUG),1)
GRAPHENE_LOG_LEVEL = debug
else
GRAPHENE_LOG_LEVEL = error
endif

.PHONY: all
all: benchmark_app benchmark_app.manifest pal_loader $(VENV_DIR) \
	$(MODEL_DIR)/public/$(MODEL_RESNET) \
	$(MODEL_DIR)/intel/$(MODEL_BERT) \
	$(MODEL_DIR)/intel/$(MODEL_BERT_INT8) \
	$(MODEL_DIR)/public/$(MODEL_BRATS0001) \
	$(MODEL_DIR)/public/$(MODEL_BRATS0002) \
	$(MODEL_DIR)/public/$(MODEL_SSD_COCO)
ifeq ($(SGX),1)
all: benchmark_app.manifest.sgx benchmark_app.sig \
     benchmark_app.token
endif

include ../../Scripts/Makefile.configs

$(VENV_DIR):
	mkdir -p model
	python3 -m venv $(VENV_DIR) \
	&& . $(VENV_DIR)/bin/activate \
	&& pip install --upgrade pip \
	&& cd $(OPENVINO_DIR)/deployment_tools/open_model_zoo/tools/downloader \
	&& python3 -mpip install -r ./requirements.in \
	&& cd $(OPENVINO_DIR)/deployment_tools/model_optimizer \
	&& pip3 install -r requirements.txt \
	&& deactivate

$(MODEL_DIR)/public/$(MODEL_RESNET):
	. $(VENV_DIR)/bin/activate \
	&& cd $(OPENVINO_DIR)/deployment_tools/open_model_zoo/tools/downloader \
	&& python3 ./downloader.py --name $(MODEL_RESNET) -o $(abspath $(MODEL_DIR)) \
	&& python3 ./converter.py --name $(MODEL_RESNET) -d $(abspath $(MODEL_DIR)) -o $(abspath $(MODEL_DIR)) \
	&& deactivate

$(MODEL_DIR)/intel/$(MODEL_BERT):
	. $(VENV_DIR)/bin/activate \
	&& cd $(OPENVINO_DIR)/deployment_tools/open_model_zoo/tools/downloader \
	&& python3 ./downloader.py --name $(MODEL_BERT) -o $(abspath $(MODEL_DIR)) \
	&& python3 ./converter.py --name $(MODEL_BERT) -d $(abspath $(MODEL_DIR)) -o $(abspath $(MODEL_DIR)) \
	&& deactivate

$(MODEL_DIR)/intel/$(MODEL_BERT_INT8):
	. $(VENV_DIR)/bin/activate \
	&& cd $(OPENVINO_DIR)/deployment_tools/open_model_zoo/tools/downloader \
	&& python3 ./downloader.py --name $(MODEL_BERT_INT8) -o $(abspath $(MODEL_DIR)) \
	&& python3 ./converter.py --name $(MODEL_BERT_INT8) -d $(abspath $(MODEL_DIR)) -o $(abspath $(MODEL_DIR)) \
	&& deactivate

$(MODEL_DIR)/public/$(MODEL_BRATS0001):
	. $(VENV_DIR)/bin/activate \
	&& cd $(OPENVINO_DIR)/deployment_tools/open_model_zoo/tools/downloader \
	&& python3 ./downloader.py --name $(MODEL_BRATS0001) -o $(abspath $(MODEL_DIR)) \
	&& python3 ./converter.py --name $(MODEL_BRATS0001) -d $(abspath $(MODEL_DIR)) -o $(abspath $(MODEL_DIR)) \
	&& deactivate

$(MODEL_DIR)/public/$(MODEL_BRATS0002):
	. $(VENV_DIR)/bin/activate \
	&& cd $(OPENVINO_DIR)/deployment_tools/open_model_zoo/tools/downloader \
	&& python3 ./downloader.py --name $(MODEL_BRATS0002) -o $(abspath $(MODEL_DIR)) \
	&& python3 ./converter.py --name $(MODEL_BRATS0002) -d $(abspath $(MODEL_DIR)) -o $(abspath $(MODEL_DIR)) \
	&& deactivate

$(MODEL_DIR)/public/$(MODEL_SSD_COCO):
	. $(VENV_DIR)/bin/activate \
	&& cd $(OPENVINO_DIR)/deployment_tools/open_model_zoo/tools/downloader \
	&& python3 ./downloader.py --name $(MODEL_SSD_COCO) -o $(abspath $(MODEL_DIR)) \
	&& python3 ./converter.py --name $(MODEL_SSD_COCO) -d $(abspath $(MODEL_DIR)) -o $(abspath $(MODEL_DIR)) \
	&& deactivate

benchmark_app.manifest: benchmark_app.manifest.template
	graphene-manifest \
		-Dlog_level=$(GRAPHENE_LOG_LEVEL) \
		-Darch_libdir=$(ARCH_LIBDIR) \
		-Dopenvino_dir=$(abspath $(OPENVINO_DIR)) \
		-DHOME=$(HOME) \
		$< > $@

# Generating the SGX-specific manifest (benchmark_app.manifest.sgx), the enclave signature,
# and the token for enclave initialization.
benchmark_app.manifest.sgx: benchmark_app.manifest \
		| benchmark_app
		graphene-sgx-sign \
		-key $(SGX_SIGNER_KEY) \
		-manifest $< -output $@

benchmark_app.sig: benchmark_app.manifest.sgx

benchmark_app.token: benchmark_app.sig
	graphene-sgx-get-token --sig $< --output $@

benchmark_app:
	cd $(OPENVINO_DIR)/deployment_tools/inference_engine/samples/cpp && ./build_samples.sh
	ln -s $(HOME)/inference_engine_cpp_samples_build/intel64/Release/benchmark_app $@

pal_loader:
	ln -s $(GRAPHENE_DIR)/Runtime/pal_loader $@

.PHONY: clean
clean:
	$(RM) *.manifest *.manifest.sgx *.token *.sig benchmark_app pal_loader

.PHONY: distclean
distclean: clean
	$(RM) -r $(MODEL_DIR) $(VENV_DIR)
