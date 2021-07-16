From ubuntu:18.04

ARG DEBIAN_FRONTEND=noninteractive

COPY openvino_gsc .

# Install prerequisites
RUN apt-get update \
    && apt-get install -y git wget python3.6 python3-pip numactl build-essential cmake

# Install OpenVINO
RUN tar -xvzf l_openvino_toolkit_p_2021.4.582.tgz \
    && cd l_openvino_toolkit_p_2021.4.582 \
    && ./install_openvino_dependencies.sh -y \
    && sed -i 's/decline/accept/g' silent.cfg \
    && ./install.sh -s silent.cfg

# Build apps
RUN cd /opt/intel/openvino_2021/inference_engine/samples/cpp \
    && ./build_samples.sh \
    && cd / \
    && ln -sf ~/inference_engine_cpp_samples_build/intel64/Release/benchmark_app benchmark_app

# Download models benchmark app
RUN cd /opt/intel/openvino_2021/deployment_tools/open_model_zoo/tools/downloader \
    && pip3 install -r ./requirements.in \
    && cd /opt/intel/openvino_2021/deployment_tools/model_optimizer \
    && python3 -m pip install --upgrade pip setuptools \
    && pip3 install -r requirements.txt \
    && cd /opt/intel/openvino_2021/deployment_tools/open_model_zoo/tools/downloader \
    && python3 ./downloader.py --name resnet-50-tf -o /model \
    && python3 ./converter.py --mo /opt/intel/openvino_2021/deployment_tools/model_optimizer/mo.py --name resnet-50-tf -d /model -o /model \
    && python3 ./downloader.py --name bert-large-uncased-whole-word-masking-squad-0001 -o /model \
    && python3 ./converter.py --mo /opt/intel/openvino_2021/deployment_tools/model_optimizer/mo.py --name bert-large-uncased-whole-word-masking-squad-0001 -d /model -o /model \
    && python3 ./downloader.py --name bert-large-uncased-whole-word-masking-squad-int8-0001 -o /model \
    && python3 ./converter.py --mo /opt/intel/openvino_2021/deployment_tools/model_optimizer/mo.py --name bert-large-uncased-whole-word-masking-squad-int8-0001 -d /model -o /model \
    && python3 ./downloader.py --name brain-tumor-segmentation-0001 -o /model \
    && python3 ./converter.py --mo /opt/intel/openvino_2021/deployment_tools/model_optimizer/mo.py --name brain-tumor-segmentation-0001 -d /model -o /model \
    && python3 ./downloader.py --name brain-tumor-segmentation-0002 -o /model \
    && python3 ./converter.py --mo /opt/intel/openvino_2021/deployment_tools/model_optimizer/mo.py --name brain-tumor-segmentation-0002 -d /model -o /model \
    && python3 ./downloader.py --name ssd_mobilenet_v1_coco  -o /model \
    && python3 ./converter.py --mo /opt/intel/openvino_2021/deployment_tools/model_optimizer/mo.py --name ssd_mobilenet_v1_coco -d /model -o /model

CMD ["/bin/bash"]
