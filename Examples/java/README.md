# Java example

This directory contains an example for running a HelloWorld Java example in Graphene,
including the Makefile and a template for generating the manifest.
The application is tested on Ubuntu 18.04 with both normal Linux and SGX platforms.
This example is tested with openjdk11.

# Generating the manifest

## Installing prerequisites

For generating the manifest and running the HelloWorld Java example, run the
following command to install the required packages (Ubuntu-specific):

    sudo apt-get install openjdk-11-jdk
    sudo apt-get install google-perftools

## Building hello.jar

    javac hello.java
    jar cvfm hello.jar hello.mf hello.class

## Building for graphene-direct

Run `make` (non-debug) or `make DEBUG=1` (debug) in the directory.

## Building for graphene-sgx

Run `make SGX=1` (non-debug) or `make SGX=1 DEBUG=1` (debug) in the directory.

## Run HelloWorld Java example with Graphene

Without SGX:

    graphene-direct java -jar hello.jar

With SGX:

    graphene-sgx java -Xmx4G -jar hello.jar

Note: If using 64G or greater enclave sizes, the JVM flag `-Xmx4G` can be omitted in graphene-sgx.
