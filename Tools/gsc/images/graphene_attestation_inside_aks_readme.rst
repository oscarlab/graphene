This guide demonstrates how Graphene DCAP attestation quote can be verified inside AKS cluster.


Create client and server images for graphene attestation samples
================================================================
This demonstration is created for graphene/Examples/ra-tls-secret-prov sample.

# Steps to create ra-tls-secret-prov-server image for AKS:
#
# STEP 1: Please refer graphene/Tools/gsc/images/aks-ra-tls-secret-prov-server.dockerfile


# Steps to create ra-tls-secret-prov-client gsc image for AKS:
#
# STEP 1: Prepare client to connect with remote ra-tls-secret-prov server hosted inside AKS cluster
#         1.1 Provide server dns name <AKS-DNS-NAME> to secret_provision_start() API call,
#             available at graphene/Examples/ra-tls-secret-prov/src/secret_prov_client.c.
#         1.2 For secret_prov_min_client and secret_prov_pf_client user can provide the server
#             dns name as loader.env.SECRET_PROVISION_SERVERS value inside
#             graphene/Tools/gsc/test/ubuntu18.04-ra-tls-secret-prov.manifest file.
#
# STEP 2: Create gsc image for ra-tls-secret-prov client
#         2.1 Gsc image creation steps for ra-tls-secret-prov-client image are described
#             inside graphene/Tools/gsc/test/ubuntu18.04-ra-tls-secret-prov.manifest.
#
# STEP 3: Push resulting image to Docker Hub or your preferred registry
#         $ docker tag <gsc-ra-tls-secret-prov-client-img> \
#           <dockerhubusername>/<aks-gsc-ra-tls-secret-prov-client-img>
#         $ docker push <dockerhubusername>/<aks-gsc-ra-tls-secret-prov-client-img>
#
# STEP 4: Deploy <aks-gsc-ra-tls-secret-prov-client-img> in confidential compute AKS cluster
#         Reference deployment file: graphene/Tools/gsc/images/aks-client-deployment.yaml

Deploy both client and server images inside AKS confidential compute cluster
============================================================================
**Prerequisites:** AKS confidential compute cluster with sgxquotehelper plugin and public ip address.

AKS confidential compute cluster can be created using the following link:
https://docs.microsoft.com/en-us/azure/confidential-computing/confidential-nodes-aks-get-started .

Graphene performs out-proc mode DCAP quote generation. Out-proc mode quote generation requires aesmd
service. To fulfill this requirement, AKS provides sgxquotehelper daemonset
[https://docs.microsoft.com/en-us/azure/confidential-computing/confidential-nodes-out-of-proc-attestation].
This feature exposes aesmd service for the container node. The service will internally connect with
az-dcap-client to fetch the platform collateral required for quote-generation.
In this demo the aks-client-deployment.yaml uses aesmd service exposed by AKS with the help of
sgxquotehelper plugin.

In the ra-tls-secret-prov example, the client will generate sgx quote and send the quote embedded in
RA-TLS certificate to the server. Internally the server will verify the quote using
libsgx-dcap-quote-verify library. The libsgx-dcap-quote-verify library will fetch platform
collateral from libsgx-dcap-default-qpl library. Microsoft provides az-dcap-client library as an
alternative for libsgx-dcap-default-qpl library and it fetches platform collateral from
Azure-internal caching service https://global.acccache.azure.net/sgx/certificates/.
The aks-server-deployment.yaml is utilizing az-dcap-client library instead of
libsgx-dcap-default-qpl.

The requirement of public-ip address is given so that the server is reachable by the client.

**Deployment**
$ kubectl apply -f aks-server-deployment.yaml

Once the server container is in running state,
$ kubectl apply -f aks-client-deployment.yaml

Ensure the quote generation and verification is successful inside AKS cluster
=============================================================================

Verify the client job is completed
$ kubectl get jobs -l app=gsc-ra-tls-secret-prov-client

Receive logs to verify the secret has been provisioned to the client
$ kubectl logs -l app=gsc-ra-tls-secret-prov-client --tail=50

**Expected Output**
--- Received secret1 = 'XXXXXXXXXXXXXXXXXXXXXXXXXXX', secret2 = 'XX'

Delete both client and server containers
$ kubectl apply -f aks-server-deployment.yaml
$ kubectl apply -f aks-client-deployment.yaml
