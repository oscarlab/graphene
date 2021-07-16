Enabling OpenVINO benchmark runs with Graphene-SGX
=====================================================
This directory contains a Makefile and a template manifest for the most recent version of OpenVINO toolkit (as of this writing, version 2021.4). We use the "Benchmark C++ Tool" (benchmark_app) from the OpenVINO distribution as a concrete application running under Graphene-SGX to estimate deep learning inference performance. We test only the CPU backend (i.e., no GPU or FPGA). This was tested on a machine with Ubuntu 18.04 and package version of Python 3.6.

The Makefile and the template manifest contain extensive comments. Please review them to understand the requirements for "Benchmark C++ Tool" running under Graphene-SGX.

Note: the models require ~3GB of disk space.

# Pre-system setting
Linux systems have CPU frequency scaling governor that helps the system to scale the CPU frequency to achieve best performance or to save power based on the requirement. To achieve the best performance, please set the CPU frequency scaling governor to `performance` mode.

```console
for ((i=0; i<$(nproc); i++)); do echo 'performance' > /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor ; done
```

# Bare-metal

## Software requirements
- OpenVINO: Please download latest OpenVINO toolkit (as of this writing, version 2021.4) for Linux from https://software.intel.com/content/www/us/en/develop/tools/openvino-toolkit/download.html. For OpenVINO installation step-by-step instructions please refer to this [link](https://docs.openvinotoolkit.org/latest/openvino_docs_install_guides_installing_openvino_linux.html).
- Python (version 3.6 or higher).
- Python virtual environment: `sudo apt-get install python3-venv`

## Supported models for Graphene-SGX
The following models have been enabled and tested with Graphene-SGX.

- resnet-50-tf(FP16/FP32)
- ssd_mobilenet_v1_coco(FP16/FP32)
- bert-large-uncased-whole-word-masking-squad-0001(FP16/FP32)
- bert-large-uncased-whole-word-masking-squad-int8-0001(INT8)
- brain-tumor-segmentation-0001(FP16/FP32)
- brain-tumor-segmentation-0002(FP16/FP32)

## Preparing the source:
1. Clone and build graphene from https://github.com/oscarlab/graphene
2. ``cd $(GRAPHENE_DIR)/Examples/openvino_benchmark``
3.Set up OpenVINO environment variables for a root user by running ``source /opt/intel/openvino_2021/bin/setupvars.sh`` or you can permanently set it by appending ``source /opt/intel/openvino_2021/bin/setupvars.sh`` to ``~/.bashrc``. For regular users run ``source /home/<USER>/intel/openvino_2021/bin/setupvars.sh``.
4. Build: ``make SGX=1``
> **NOTE**: After setting up OpenVINO environment variables if you want to build graphene after cleaning you need to unset LD_LIBRARY_PATH. Please make sure to set up OpenVINO environment variables after building graphene again.

## Running the benchmark
Performance benchmark on Xeon servers (Silver/Gold/Platinum) must be launched with increased number of inference requests. For them -nireq -nstreams -nthreads options should be set to the ``number of physical cores * 2`` (take into account hyperthreading) for achieving maximal performance.
>**NOTE** To get 'number of physical cores', do ``lscpu | grep 'Core(s) per socket'``

### Throughput runs

#### Graphene-SGX

```console
$ export OPTIMAL_VALUE=number of physical cores * 2
$ sudo KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 graphene-sgx benchmark_app -i <image files> -m <model path XML file> -d CPU -b 1 -t 20 -nstreams OPTIMAL_VALUE -nthreads OPTIMAL_VALUE -nireq OPTIMAL_VALUE
```
For example, in a system with 36 physical cores, the following commands will execute the OpenVINO benchmark for obtaining throughput measurements for the corresponding model.
```console
$ sudo KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 graphene-sgx benchmark_app -m model/<public | intel>/<model_dir>/<INT8 | FP16 | FP32>/<model_xml_file> -d CPU -b 1 -t 20 -nstreams 72 -nthreads 72 -nireq 72
```

#### Bare-metal

```console
$ export OPTIMAL_VALUE=number of physical cores * 2
$ KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 ./benchmark_app -i <image files> -m <model path XML file> -d CPU -b 1 -t 20 -nstreams OPTIMAL_VALUE -nthreads OPTIMAL_VALUE -nireq OPTIMAL_VALUE
```
For example, in a system with 36 physical cores, the following commands will execute the OpenVINO benchmark for obtaining throughput measurements for the corresponding model.
```console
$ KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 ./benchmark_app -m model/<public | intel>/<model_dir>/<INT8 | FP16 | FP32>/<model_xml_file> -d CPU -b 1 -t 20 -nstreams 72 -nthreads 72 -nireq 72
```

> **NOTE 1**: Option ``-i \<image files\>`` is optional. A user may use this option as required.  
> **NOTE 2**: Please tune batch size to get best performance in your system.  
> **NOTE 3**: Model files for bert-large can be found in ``model/intel`` directory and for rest of the models these are stored in ``model/public`` directory.  
> **NOTE 4**: Based on the precision for bert-large and brain-tumor-segmentation models the enclave size must be set to 64/128 GB.  
> **NOTE 5**: In multi-socket systems for bert-large-uncased-whole-word-masking-squad-0001 and brain-tumor-segmentation-0001 FP32/FP16 models if allocation of memory fails when there is not enough memory available please expand memory nodes usage with numactl --membind option.


### Latency runs

#### Graphene-SGX

```console
$ sudo KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 graphene-sgx benchmark_app -i <image files> -m <model path XML file> -d CPU -t 20 -b 1 -api sync
```

For example, in a system with 36 physical cores, the following commands will execute the OpenVINO benchmark for obtaining latency measurements for the corresponding model.
```console
$ sudo KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 graphene-sgx benchmark_app -m model/<public | intel>/<model_dir>/<INT8 | FP16 | FP32>/<model_xml_file> -d CPU -b 1 -t 20 -api sync
```

#### Bare-metal

```console
$ KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 ./benchmark_app -i <image files> -m <model path XML file> -d CPU -t 20 -b 1 -api sync
```

For example, in a system with 36 physical cores, the following commands will execute the OpenVINO benchmark for obtaining latency measurements for the corresponding model.
```console
$ KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 ./benchmark_app -m model/<public | intel>/<model_dir>/<INT8 | FP16 | FP32>/<model_xml_file> -d CPU -b 1 -t 20 -api sync
```

> **NOTE**: Option ``-i \<image files\>`` is optional. A user may use this option as required.

# GSC

## Software requirements
Check Prerequisites section from https://graphene.readthedocs.io/en/latest/manpages/gsc.html#prerequisites to install all software packages required for GSC.

## Supported models for Graphene-SGX
The following models have been enabled and tested with Graphene-SGX.

- resnet-50-tf(FP16/FP32)
- ssd_mobilenet_v1_coco(FP16/FP32)
- bert-large-uncased-whole-word-masking-squad-0001(FP16/FP32)
- bert-large-uncased-whole-word-masking-squad-int8-0001(INT8)
- brain-tumor-segmentation-0001(FP16/FP32)
- brain-tumor-segmentation-0002(FP16/FP32)

## Preparing the source:
1. Clone graphene from https://github.com/oscarlab/graphene
2. ``cd $(GRAPHENE_DIR)/Examples/``
3. ``mkdir -p openvino_gsc``
4. ``cd openvino_gsc``. Please download OpenVINO toolkit version 2021.4 for Linux from https://software.intel.com/content/www/us/en/develop/tools/openvino-toolkit/download.html.
5. ``cd ../../Tools/gsc``
6. Modify ``loader.env.LD_LIBRARY_PATH`` option in `templates/entrypoint.manifest.template` by appending OpenVINO related paths with existing paths ``:/opt/intel/openvino_2021/deployment_tools/inference_engine/external/tbb/lib:/opt/intel/openvino_2021/deployment_tools/inference_engine/lib/intel64:/opt/intel/openvino_2021/deployment_tools/ngraph/lib``

## Build graphenize docker image:
1. Create a configuration file : ``cp config.yaml.template config.yaml``
Manually adopt config.yaml to the installed Intel SGX driver and desired Graphene repository/version

2. Generate the signing key : ``openssl genrsa -3 -out enclave-key.pem 3072``

3. Build docker image :
    - ``cd test``
    - ``docker build --shm-size=4g  --rm -t ubuntu18.04-openvino -f ubuntu18.04-openvino.dockerfile ../../../Examples``

4. Graphenize the docker image using gsc build :
    - ``cd ..``
    - ``./gsc build --insecure-args ubuntu18.04-openvino test/ubuntu18.04-openvino.manifest``

5. Sign the graphenized Docker image using gsc sign-image : ``./gsc sign-image ubuntu18.04-openvino enclave-key.pem``

## Running the benchmark
Performance benchmark on Xeon servers (Silver/Gold/Platinum) must be launched with increased number of inference requests. For them -nireq -nstreams -nthreads options should be set to the ``number of physical cores * 2`` (take into account hyperthreading) for achieving maximal performance.
>**NOTE** To get 'number of physical cores', do ``lscpu | grep 'Core(s) per socket'``

### Throughput runs

#### GSC

```console
$ export OPTIMAL_VALUE=number of physical cores * 2
$ sudo docker run --cpuset-cpus=<value> --cpuset-mems=<value> --env KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 --device /dev/sgx_enclave gsc-ubuntu18.04-openvino -c './benchmark_app -i <image files> -m <model path XML file> -d CPU -b 1 -t 20 -nstreams OPTIMAL_VALUE -nthreads OPTIMAL_VALUE -nireq OPTIMAL_VALUE'
```
change --device=/dev/sgx_enclave to your version of the Intel SGX driver if needed.

For example, in a system with 36 physical cores, the following commands will execute the OpenVINO benchmark for obtaining throughput measurements for the corresponding model.
```console
$ sudo docker run --cpuset-cpus="0-35,72-107" --cpuset-mems="0" --env KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 --device /dev/sgx_enclave gsc-ubuntu18.04-openvino -c './bechmark_app -m model/<public | intel>/<model_dir>/<INT8 | FP16 | FP32>/<model_xml_file> -d CPU -b 1 -t 20 -nstreams 72 -nthreads 72 -nireq 72'
```

#### Container

```console
$ docker run --privileged -it ubuntu18.04-openvino bash
$ source /opt/intel/openvino_2021/bin/setupvars.sh
$ export OPTIMAL_VALUE=number of physical cores * 2
$ KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 ./benchmark_app -i <image files> -m <model path XML file> -d CPU -b 1 -t 20 -nstreams OPTIMAL_VALUE -nthreads OPTIMAL_VALUE -nireq OPTIMAL_VALUE
```

For example, in a system with 36 physical cores, the following commands will execute the OpenVINO benchmark for obtaining throughput measurements for the corresponding model.
```console
$ docker run --privileged -it ubuntu18.04-openvino bash
$ source /opt/intel/openvino_2021/bin/setupvars.sh
$ KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 ./benchmark_app -m model/<public | intel>/<model_dir>/<INT8 | FP16 | FP32>/<model_xml_file> -d CPU -b 1 -t 20 -nstreams 72 -nthreads 72 -nireq 72
```
> **NOTE 1**: Option ``-i \<image files\>`` is optional. A user may use this option as required.  
> **NOTE 2**: Please tune batch size to get best performance in your system.  
> **NOTE 3**: Model files for bert-large can be found in ``model/intel`` directory and for rest of the models these are stored in ``model/public`` directory.  
> **NOTE 4**: Based on the precision for bert-large and brain-tumor-segmentation models the enclave size must be set to 64/128 GB.  
> **NOTE 5**: In multi-socket systems for bert-large-uncased-whole-word-masking-squad-0001 and brain-tumor-segmentation-0001 FP32/FP16 models if allocation of memory fails when there is not enough memory available please expand memory nodes usage with numactl --membind option.

### Latency runs

#### GSC

```console
$ sudo docker run --cpuset-cpus=<value> --cpuset-mems=<value> --env KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 --device /dev/sgx_enclave gsc-ubuntu18.04-openvino -c './benchmark_app -i <image files> -m <model path XML file> -d CPU -t 20 -b 1 -api sync'
```

For example, in a system with 36 physical cores, the following commands will execute the OpenVINO benchmark for obtaining latency measurements for the corresponding model.
```console
$ sudo docker run --cpuset-cpus="0-35,72-107" --cpuset-mems="0" --env KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 --device /dev/sgx_enclave gsc-ubuntu18.04-openvino -c './benchmark_app -m model/<public | intel>/<model_dir>/<INT8 | FP16 | FP32>/<model_xml_file> -d CPU -b 1 -t 20 -api sync'
```
#### Container

```console
$ docker run --privileged -it ubuntu18.04-openvino bash
$ source /opt/intel/openvino_2021/bin/setupvars.sh
$ KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 ./benchmark_app -i <image files> -m <model path XML file> -d CPU -b 1 -t 20
```

For example, in a system with 36 physical cores, the following commands will execute the OpenVINO benchmark for obtaining throughput measurements for the corresponding model.
```console
$ docker run --privileged -it ubuntu18.04-openvino bash
$ source /opt/intel/openvino_2021/bin/setupvars.sh
$ KMP_AFFINITY=granularity=fine,noverbose,compact,1,0 numactl --cpubind=0 --membind=0 ./benchmark_app -m model/<public | intel>/<model_dir>/<INT8 | FP16 | FP32>/<model_xml_file> -d CPU -b 1 -t 20
```
> **NOTE**: Option ``-i \<image files\>`` is optional. A user may use this option as required.

# Performance considerations
- Preheat manifest option pre-faults the enclave memory and moves the performance penalty to graphene-sgx invocation (before the workload starts executing). To use preheat option, add ``sgx.preheat_enclave = 1`` to the manifest template.
- Skipping invalid user pointer checks when the application does not pass any invalid pointers can help improve performance. To use this option, add  ``libos.check_invalid_pointers = 0`` to the manifest template.
- TCMalloc and mimalloc are memory allocator libraries from Google and Microsoft that can help improve performance significantly based on the workloads. At any point, only one of these allocators can be used.
  - TCMalloc (Please update the binary location and name if different from default)
	- Install tcmalloc : sudo apt-get install google-perftools
	- Add these in the manifest template<br>
		``loader.env.LD_PRELOAD = "/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4"``<br>
		``sgx.trusted_files.libtcmalloc = "file:/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4"``<br>
		``sgx.trusted_files.libunwind = "file:/usr/lib/x86_64-linux-gnu/libunwind.so.8"``
	- Save the template and rebuild.
  - mimalloc (Please update the binary location and name if different from default)
	- Install mimalloc using the steps from https://github.com/microsoft/mimalloc
	- Add these in the manifest template<br>
		``loader.env.LD_PRELOAD = "/usr/local/lib/mimalloc-1.7/libmimalloc.so.1.7"``<br>
		``sgx.trusted_files.libmimalloc = "file:/usr/local/lib/mimalloc-1.7/libmimalloc.so.1.7"``
	- Save the template and rebuild.
