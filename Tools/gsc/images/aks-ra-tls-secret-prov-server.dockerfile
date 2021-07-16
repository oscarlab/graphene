# Steps to create ra-tls-secret-prov-server image for AKS:
#
# STEP 1: Prepare Server certificate
#         1.1 Create server certificate signed by your trusted root CA. Ensure Common Name
#	      field in the server certificate corresponds to <AKS-DNS-NAME> used in STEP 5.
#	  1.2 Put trusted root CA certificate, server certificate, and server key in
#	      graphene/Examples/ra-tls-secret-prov/certs directory with existing naming convention.
#         1.3 Provide password for your server key to mbedtls_pk_parse_keyfile(,,pwd) API call,
#	      available at graphene/Pal/src/host/Linux-SGX/tools/ra-tls/tools/secret_prov_verify.c.
#
# STEP 2: Make sure RA-TLS DCAP libraries are built in Graphene via:
#         $ cd graphene/Pal/src/host/Linux-SGX/tools/ra-tls && make dcap
#
# STEP 3: Create base ra-tls-secret-prov server image
#         $ cd graphene
#         $ docker build -t <aks-ra-tls-secret-prov-server-img> \
#           -f Tools/gsc/images/aks-ra-tls-secret-prov-server.dockerfile .
#
# STEP 4: Push resulting image to Docker Hub or your preferred registry
#         $ docker tag <aks-ra-tls-secret-prov-server-img> \
#           <dockerhubusername>/<aks-ra-tls-secret-prov-server-img>
#         $ docker push <dockerhubusername>/<aks-ra-tls-secret-prov-server-img>
#
# STEP 5: Deploy <aks-ra-tls-secret-prov-server-img> in AKS confidential compute cluster
#         Reference deployment file: graphene/Tools/gsc/images/aks-server-deployment.yaml
#
# NOTE:  Server can be deployed at non-confidential compute node as well. However, in that
#        QVE-based dcap verification will fail.

FROM ubuntu:18.04

RUN apt-get update \
    && env DEBIAN_FRONTEND=noninteractive apt-get install -y wget \
    build-essential \
    python3 \
    libcurl3-gnutls \
    gnupg2 \
    libcurl4-openssl-dev

# Installing Azure DCAP Quote Provider Library (az-dcap-client)

RUN wget https://github.com/microsoft/Azure-DCAP-Client/releases/download/1.8/az-dcap-client_1.8_amd64_18.04.deb \
    && chmod u+x az-dcap-client_1.8_amd64_18.04.deb \
    && dpkg -i az-dcap-client_1.8_amd64_18.04.deb

# Installing DCAP Quote Verification Library

RUN echo 'deb [arch=amd64] https://download.01.org/intel-sgx/sgx_repo/ubuntu bionic main' \
    > /etc/apt/sources.list.d/intel-sgx.list \
    && wget https://download.01.org/intel-sgx/sgx_repo/ubuntu/intel-sgx-deb.key \
    && apt-key add intel-sgx-deb.key

RUN apt-get update && apt-get install -y libsgx-dcap-quote-verify

# Build environment of this Dockerfile should point to the root of Graphene directory

RUN mkdir -p /graphene/Scripts \
    && mkdir -p /graphene/Pal/src/host/Linux-SGX/tools/pf_crypt \
    && mkdir -p /graphene/Pal/src/host/Linux-SGX/tools/common \
    && mkdir -p /graphene/Pal/src/host/Linux-SGX/tools/ra-tls \
    && mkdir -p /graphene/Examples/ra-tls-secret-prov

# The below files are copied to satisfy Makefile dependencies of graphene/Examples/ra-tls-secret-prov

COPY Scripts/Makefile.configs  /graphene/Scripts/
COPY Scripts/Makefile.Host  /graphene/Scripts/
COPY Scripts/download  /graphene/Scripts/

COPY Pal/src/host/Linux-SGX/tools/pf_crypt/pf_crypt /graphene/Pal/src/host/Linux-SGX/tools/pf_crypt/
COPY Pal/src/host/Linux-SGX/tools/common/libsgx_util.so /graphene/Pal/src/host/Linux-SGX/tools/common/

# make sure RA-TLS DCAP libraries are built in host Graphene via:
# cd graphene/Pal/src/host/Linux-SGX/tools/ra-tls && make dcap

COPY Pal/src/host/Linux-SGX/tools/ra-tls/libsecret_prov_attest.so /graphene/Pal/src/host/Linux-SGX/tools/ra-tls/
COPY Pal/src/host/Linux-SGX/tools/ra-tls/libsecret_prov_verify_dcap.so /graphene/Pal/src/host/Linux-SGX/tools/ra-tls/
COPY Pal/src/host/Linux-SGX/tools/ra-tls/secret_prov.h /graphene/Pal/src/host/Linux-SGX/tools/ra-tls/

# If user doesn't want to copy above files, then she can build the ra-tls-secret-prov sample locally
# and copy the entire directory with executables

COPY Examples/ra-tls-secret-prov /graphene/Examples/ra-tls-secret-prov

WORKDIR /graphene/Examples/ra-tls-secret-prov

RUN make clean \
    && make dcap files/input.txt

ENV LD_LIBRARY_PATH = "${LD_LIBRARY_PATH}:./libs"

ENV PATH = "${PATH}:/graphene/Examples/ra-tls-secret-prov"

ENTRYPOINT ["/graphene/Examples/ra-tls-secret-prov/secret_prov_server_dcap","&"]
