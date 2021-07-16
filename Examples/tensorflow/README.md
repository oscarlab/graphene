## Inference on TensorFlow BERT and ResNet50 models
This directory contains steps and artifacts to run inference with TensorFlow BERT and ResNet50\
sample workloads on Graphene. Specifically, both these examples use pre-trained models to run inference.\
We tested this on Ubuntu 18.04 and uses the package version with Python 3.6.

### Bidirectional Encoder Representations from Transformers (BERT):
BERT is a method of pre-training language representations and then use that trained model for downstream\
NLP tasks like 'question answering'. BERT is an unsupervised, deeply birectional system for pre-training NLP.\
In this BERT sample, we use 'BERT-Large, Uncased (Whole Word Masking)' model and perform int8 inference.\
More details about BERT can be found at https://github.com/google-research/bert.

### Residual Network (ResNet):
ResNet50 is a convolutional neural network that is 50 layers deep.\
In this ResNet50(v1.5) sample, we use a pre-trained model and perform int8 inference.\
More details about ResNet50 can be found at https://github.com/IntelAI/models/tree/icx-launch-public/benchmarks/image_recognition/tensorflow/resnet50v1_5.


## Pre-System setting
Linux systems have CPU frequency scaling governor that helps the system to scale the CPU frequency\
to achieve best performance or to save power based on the requirement.
To achieve the best peformance, please set the CPU frequency scaling governor to performance mode.

``for ((i=0; i<$(nproc); i++)); do echo 'performance' > /sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor; done``

## Pre-requisites
- Install python3.6.
- Upgrade pip/pip3.
- Install tensorflow using ``pip install intel-tensorflow-avx512==2.4.0`` or by downloading whl\
package from https://pypi.org/project/intel-tensorflow-avx512/2.4.0/#files.

## Build BERT or ResNet50 samples
- To build BERT sample, do ``cd BERT`` or to build ResNet50 sample, do ``cd ResNet50``.
- To clean the sample, do ``make clean``
- To clean and remove downloaded models and datasets, do ``make distclean``
- To build the non-SGX version, do ``make PYTHONDISTPATH=path_to_python_dist_packages/``
- To build the SGX version, do ``make PYTHONDISTPATH=path_to_python_dist_packages/ SGX=1``
>**WARNING:** Building BERT sample downloads about 5GB of data.\
>**NOTE:** Typically, path_to_python_dist_packages is '/usr/local/lib/python3.6/dist-packages',\
but can change based on python's installation directory.

## Run inference on BERT model
- To run int8 inference on graphene-sgx(SGX version)
```
OMP_NUM_THREADS=36 KMP_AFFINITY=granularity=fine,verbose,compact,1,0 taskset -c 0-35 graphene-sgx \
./python models/models/language_modeling/tensorflow/bert_large/inference/run_squad.py \
--init_checkpoint=data/bert_large_checkpoints/model.ckpt-3649 \
--vocab_file=data/wwm_uncased_L-24_H-1024_A-16/vocab.txt \
--bert_config_file=data/wwm_uncased_L-24_H-1024_A-16/bert_config.json \
--predict_file=data/wwm_uncased_L-24_H-1024_A-16/dev-v1.1.json \
--precision=int8 \
--output_dir=output/bert-squad-output \
--predict_batch_size=32 \
--experimental_gelu=True \
--optimized_softmax=True \
--input_graph=data/asymmetric_per_channel_bert_int8.pb \
--do_predict=True --mode=benchmark \
--inter_op_parallelism_threads=1 \
--intra_op_parallelism_threads=36
```
- To run int8 inference on graphene-direct(non-SGX version)
```
OMP_NUM_THREADS=36 KMP_AFFINITY=granularity=fine,verbose,compact,1,0 taskset -c 0-35 \
graphene-direct ./python models/models/language_modeling/tensorflow/bert_large/inference/run_squad.py \
--init_checkpoint=data/bert_large_checkpoints/model.ckpt-3649 \
--vocab_file=data/wwm_uncased_L-24_H-1024_A-16/vocab.txt \
--bert_config_file=data/wwm_uncased_L-24_H-1024_A-16/bert_config.json \
--predict_file=data/wwm_uncased_L-24_H-1024_A-16/dev-v1.1.json \
--precision=int8 \
--output_dir=output/bert-squad-output \
--predict_batch_size=32 \
--experimental_gelu=True \
--optimized_softmax=True \
--input_graph=data/asymmetric_per_channel_bert_int8.pb \
--do_predict=True \
--mode=benchmark \
--inter_op_parallelism_threads=1 \
--intra_op_parallelism_threads=36
```
- To run int8 inference on native baremetal(outside graphene)
```
OMP_NUM_THREADS=36 KMP_AFFINITY=granularity=fine,verbose,compact,1,0 taskset -c 0-35 python3.6 \
models/models/language_modeling/tensorflow/bert_large/inference/run_squad.py \
--init_checkpoint=data/bert_large_checkpoints/model.ckpt-3649 \
--vocab_file=data/wwm_uncased_L-24_H-1024_A-16/vocab.txt \
--bert_config_file=data/wwm_uncased_L-24_H-1024_A-16/bert_config.json \
--predict_file=data/wwm_uncased_L-24_H-1024_A-16/dev-v1.1.json \
--precision=int8 --output_dir=output/bert-squad-output \
--predict_batch_size=32 \
--experimental_gelu=True \
--optimized_softmax=True \
--input_graph=data/asymmetric_per_channel_bert_int8.pb \
--do_predict=True \
--mode=benchmark \
--inter_op_parallelism_threads=1 \
--intra_op_parallelism_threads=36
```
- Above commands are for a 36 core system. Please set the following options accordingly for optimal performance.
    - OMP_NUM_THREADS='Core(s) per socket'
    - taskset to 'Core(s) per socket'
    - intra_op_parallelism_threads='Core(s) per socket'
    - If hyperthreading is enabled : use ``KMP_AFFINITY=granularity=fine,verbose,compact,1,0``
    - If hyperthreading is disabled : use ``KMP_AFFINITY=granularity=fine,verbose,compact``
>**NOTE:** To get 'Core(s) per socket', do ``lscpu | grep 'Core(s) per socket'``\
> OMP_NUM_THREADS sets the maximum number of threads to use for OpenMP parallel regions. \
> KMP_AFFINITY binds OpenMP threads to physical processing units.

## Run inference on ResNet50 model
- To run inference on graphene-sgx(SGX version)
```
OMP_NUM_THREADS=36 KMP_AFFINITY=granularity=fine,verbose,compact,1,0 taskset -c 0-35 graphene-sgx \
./python models/models/image_recognition/tensorflow/resnet50v1_5/inference/eval_image_classifier_inference.py \
--input-graph=resnet50v1_5_int8_pretrained_model.pb \
--num-inter-threads=1 \
--num-intra-threads=36 \
--batch-size=32 \
--warmup-steps=50 \
--steps=500
```
- To run inference on graphene-direct(non-SGX version)
```
OMP_NUM_THREADS=36 KMP_AFFINITY=granularity=fine,verbose,compact,1,0 taskset -c 0-35 graphene-direct \
./python models/models/image_recognition/tensorflow/resnet50v1_5/inference/eval_image_classifier_inference.py \
--input-graph=resnet50v1_5_int8_pretrained_model.pb \
--num-inter-threads=1 \
--num-intra-threads=36 \
--batch-size=32 \
--warmup-steps=50 \
--steps=500
```
- To run inference on native baremetal(outside graphene)
```
OMP_NUM_THREADS=36 KMP_AFFINITY=granularity=fine,verbose,compact,1,0 taskset -c 0-35 python3.6 \
models/models/image_recognition/tensorflow/resnet50v1_5/inference/eval_image_classifier_inference.py \
--input-graph=resnet50v1_5_int8_pretrained_model.pb \
--num-inter-threads=1 \
--num-intra-threads=36 \
--batch-size=32 \
--warmup-steps=50 \
--steps=500
```
- Above commands are for a 36 core system. Please set the following options accordingly for optimal performance.
    - OMP_NUM_THREADS='Core(s) per socket'
    - taskset to 'Core(s) per socket'
    - num-intra-threads='Core(s) per socket'
    - If hyperthreading is enabled : use ``KMP_AFFINITY=granularity=fine,verbose,compact,1,0``
    - If hyperthreading is disabled : use ``KMP_AFFINITY=granularity=fine,verbose,compact``
    - The options batch-size, warmup-steps and steps can be varied.
>**NOTE:** To get 'Core(s) per socket', do ``lscpu | grep 'Core(s) per socket'``\
> OMP_NUM_THREADS sets the maximum number of threads to use for OpenMP parallel regions.\
> KMP_AFFINITY binds OpenMP threads to physical processing units.

## Performance considerations
- Preheat manifest option pre-faults the enclave memory and moves the performance penalty to\
graphene-sgx invocation (before the workload starts execution).\
To use preheat option, add ``sgx.preheat_enclave = 1`` to the manifest template.
- TCMalloc and mimalloc are memory allocator libraries from Google and Microsoft that can help\
improve performance significantly based on the workloads. At any point, only one of these allocators can be used.
  - TCMalloc (Please update the binary location and name if different from default)
    - Install tcmalloc : ``sudo apt-get install google-perftools``
    - Add these in the manifest template
        ```loader.env.LD_PRELOAD = "/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4"```
        ```sgx.trusted_files.libtcmalloc = "file:/usr/lib/x86_64-linux-gnu/libtcmalloc.so.4"```
        ```sgx.trusted_files.libunwind = "file:/usr/lib/x86_64-linux-gnu/libunwind.so.8"```
    - Save the template and rebuild.
  - mimalloc (Please update the binary location and name if different from default)
    - Install mimalloc using the steps from https://github.com/microsoft/mimalloc
    - Add these in the manifest template
        ```loader.env.LD_PRELOAD = "/usr/local/lib/mimalloc-1.7/libmimalloc.so.1.7"```
        ```sgx.trusted_files.libmimalloc = "file:/usr/local/lib/mimalloc-1.7/libmimalloc.so.1.7"```
    - Save the template and rebuild.
