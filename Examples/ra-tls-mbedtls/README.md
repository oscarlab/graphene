# RA-TLS Minimal Example

This directory contains the Makefile, the template server manifest, and the minimal server and
client written against the mbedTLS 2.21.0 library.  This was tested on a machine with SGX v1 and
Ubuntu 18.04.

The server and client are based on `ssl_server.c` and `ssl_client.c` example programs shipped with
mbedTLS. We modified them to allow using RA-TLS flows if the programs are given the command-line
argument `epid`/`dcap`.  In particular, the server uses a self-signed RA-TLS cert with the SGX quote
embedded in it via `ra_tls_create_key_and_crt()`. The client uses an RA-TLS verification callback to
verify the server RA-TLS certificate via `ra_tls_verify_callback()`.

This example uses the RA-TLS libraries `ra_tls_attest.so` for server and `ra_tls_verify_epid.so`/
`ra_tls_verify_dcap.so` for client. These libraries are found under
`Pal/src/host/Linux-SGX/tools/ra-tls`. Additionally, mbedTLS libraries are required to correctly
run RA-TLS, the client, and the server. For ECDSA/DCAP attestation, the DCAP software
infrastructure must be installed and working correctly on the host.

The current example works with both EPID (IAS) and ECDSA (DCAP) remote attestation schemes. For
more documentation, refer to `Pal/src/host/Linux-SGX/tools/README.rst`.


## RA-TLS server

The server is supposed to run in the SGX enclave with Graphene and RA-TLS dlopen-loaded. If RA-TLS
library `ra_tls_attest.so` is not requested by user via `epid`/`dcap` command-line argument, the
server falls back to using normal X.509 PKI flows (specified as `native` command-line argument).

If server is run with more command-line arguments (the only important thing is to have at least one
additional argument), then the server will maliciously modify the SGX quote before sending to the
client. This is useful for testing purposes.

## RA-TLS client

The client is supposed to run on a trusted machine (*not* in an SGX enclave). If RA-TLS library
`ra_tls_verify_epid.so` or `ra_tls_verify_dcap.so` is not requested by user via `epid` or `dcap`
command-line arguments respectively, the client falls back to using normal X.509 PKI flows
(specified as `native` command-line argument).

It is also possible to run the client in an SGX enclave. This will create a secure channel between
two Graphene SGX processes, possibly running on different machines. It can be used as an example
of in-enclave remote attestation and verification.

If client is run without additional command-line arguments, it uses default RA-TLS verification
callback that compares `MRENCLAVE`, `MRSIGNER`, `ISV_PROD_ID` and `ISV_SVN` against the corresonding
`RA_TLS_*` environment variables. To run the client with its own verification callback, execute it
with four additional command-line arguments (see the source code for details).


# Quick Start

- Normal non-RA-TLS flows; without SGX and without Graphene:

```sh
make app
./server native &
./client native
# client will successfully connect to the server via normal x.509 PKI flows
kill %%
```

- RA-TLS flows with SGX and with Graphene, EPID-based (IAS) attestation:

```sh
# replace dummy values with your SPID, linkable setting, API key, MRENCLAVE, etc!
make clean
RA_CLIENT_SPID=12345678901234567890123456789012 RA_CLIENT_LINKABLE=0 make app epid

SGX=1 ./pal_loader ./server epid &

RA_TLS_MAA_JSON_FILE="maa.json" \
RA_TLS_EPID_API_KEY=12345678901234567890123456789012 \
RA_TLS_ALLOW_OUTDATED_TCB_INSECURE=1 \
RA_TLS_MRENCLAVE=1234567890123456789012345678901234567890123456789012345678901234 \
RA_TLS_MRSIGNER=1234567890123456789012345678901234567890123456789012345678901234 \
RA_TLS_ISV_PROD_ID=0 RA_TLS_ISV_SVN=0 \
./client epid

# client will successfully connect to the server via RA-TLS/EPID flows
kill %%

# you can open the generated file maa.json for Microsoft Azure Attestation (MAA)
# in your favorite text editor
```

- RA-TLS flows with SGX and with Graphene, ECDSA-based (DCAP) attestation:

```sh
# replace dummy values with your MRENCLAVE, MRSIGNER, etc!
make clean
make app dcap

SGX=1 ./pal_loader ./server dcap &

RA_TLS_MAA_JSON_FILE="maa.json" \
RA_TLS_ALLOW_OUTDATED_TCB_INSECURE=1 \
RA_TLS_MRENCLAVE=1234567890123456789012345678901234567890123456789012345678901234 \
RA_TLS_MRSIGNER=1234567890123456789012345678901234567890123456789012345678901234 \
RA_TLS_ISV_PROD_ID=0 RA_TLS_ISV_SVN=0 \
./client dcap

# client will successfully connect to the server via RA-TLS/DCAP flows
kill %%

# you can open the generated file maa.json for Microsoft Azure Attestation (MAA)
# in your favorite text editor
```

- RA-TLS flows with SGX and with Graphene, client with its own verification callback:

```sh
# replace dummy values with your MRENCLAVE, MRSIGNER, etc!
make clean
make app dcap

SGX=1 ./pal_loader ./server dcap &

# arguments are: MRENCLAVE in hex, MRSIGNER in hex, ISV_PROD_ID as dec, ISV_SVN as dec
RA_TLS_ALLOW_OUTDATED_TCB_INSECURE=1 ./client dcap \
    1234567890123456789012345678901234567890123456789012345678901234 \
    1234567890123456789012345678901234567890123456789012345678901234 \
    0 0

# client will successfully connect to the server via RA-TLS/DCAP flows
kill %%
```

- RA-TLS flows with SGX and with Graphene, server sends malicious SGX quote:

```sh
make clean
make app dcap

SGX=1 ./pal_loader ./server dcap dummy-option &
./client dcap

# client will fail to verify the malicious SGX quote and will *not* connect to the server
kill %%
```

- RA-TLS flows with SGX and with Graphene, running EPID client in SGX:

Note: you may also add environment variables to client.manifest.template, such as
`RA_TLS_ALLOW_OUTDATED_TCB_INSECURE`, `RA_TLS_MRENCLAVE`, `RA_TLS_MRSIGNER`, `RA_TLS_ISV_PROD_ID`
and `RA_TLS_ISV_SVN`.

```sh
make clean
RA_CLIENT_SPID=12345678901234567890123456789012 RA_CLIENT_LINKABLE=0 make app client_epid.manifest.sgx

SGX=1 ./pal_loader ./server epid &

RA_TLS_EPID_API_KEY=12345678901234567890123456789012 SGX=1 ./pal_loader client_epid.manifest.sgx epid

# client will successfully connect to the server via RA-TLS/EPID flows
kill %%
```

- RA-TLS flows with SGX and with Graphene, running DCAP client in SGX:

```sh
make clean
make app client_dcap.manifest.sgx

SGX=1 ./pal_loader ./server dcap &

SGX=1 ./pal_loader client_dcap.manifest.sgx dcap

# client will successfully connect to the server via RA-TLS/DCAP flows
kill %%
```
