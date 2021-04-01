Building
========

.. highlight:: sh

.. todo::

   This page really belongs to :file:`devel/`, move it there after a |~| proper
   release. Instead, for all users, there should be documentation for installing
   without full compilation.

Graphene consists of several components:

- The Library OS itself (a shared library named ``libsysdb.so``, called the
  "shim" in our source code)
- The Platform Adaptation Layer, or PAL (a shared library named ``libpal.so``)
- A patched glibc or musl library (a set of shared libraries ``libc.so``,
  ``libpthread.so``, ``libm.so``, etc.)

The build of Graphene implies building at least the first two components. The
build of the patched C library is optional but highly recommended for
performance reasons. Patched glibc is built by default.

Graphene currently only works on the x86_64 architecture. Graphene is currently
tested on Ubuntu 16.04 and 18.04 (both server and desktop version), along with
Linux kernel versions 4.x/5.x. We recommend building and installing Graphene on
the same host platform. If you find problems with Graphene on other Linux
distributions, please contact us with a |~| detailed `bug report
<https://github.com/oscarlab/graphene/issues/new>`__.

Building without SGX support
----------------------------

Run the following command on Ubuntu to install dependencies::

    sudo apt-get install -y build-essential autoconf gawk bison

You also need Python packages for GDB support and to run tests locally::

    sudo apt-get install -y python3-pyelftools python3-pytest

To build Graphene, in the root directory of Graphene repo, run the following
command::

   make

Building with SGX support
-------------------------

The build of Graphene with SGX support requires the corresponding SGX software
infrastructure to be installed on the system. In particular, the FSGSBASE
functionality must be enabled in the Linux kernel, the Intel SGX driver must be
running, and Intel SGX SDK/PSW/DCAP must be installed. In the future, when all
required SGX infrastructure is upstreamed in Linux and popular Linux
distributions, the prerequisite steps will be significantly simplified.

Prerequisites
^^^^^^^^^^^^^

1. Required packages
""""""""""""""""""""

Run the following commands on Ubuntu to install SGX-related dependencies::

    sudo apt-get install -y libprotobuf-c-dev protobuf-c-compiler \
       libcurl4-openssl-dev python3-pip
    python3 -m pip install toml>=0.10

    # For Ubuntu 18.04
    sudo apt-get install -y python3-protobuf

    # For Ubuntu 16.04
    sudo /usr/bin/pip3 install protobuf

2a. Install the Linux kernel patched with FSGSBASE
""""""""""""""""""""""""""""""""""""""""""""""""""

FSGSBASE is a feature in recent processors which allows direct access to the FS
and GS segment base addresses. For more information about FSGSBASE and its
benefits, see `this discussion <https://lwn.net/Articles/821719>`__.
FSGSBASE patchset was merged in 5.9. For older kernels it is available as
`separate patches <https://github.com/oscarlab/graphene-sgx-driver/tree/master/fsgsbase_patches>`__.

The following instructions to patch and compile a Linux kernel with FSGSBASE
support below are written around Ubuntu 18.04 LTS (Bionic Beaver) with a Linux
5.4 LTS stable kernel but can be adapted for other distros as necessary. These
instructions ensure that the resulting kernel has FSGSBASE support and up to
date security mitigations.

#. Clone the repository with patches::

       git clone https://github.com/oscarlab/graphene-sgx-driver

#. Setup a build environment for kernel development following `the instructions
   in the Ubuntu wiki <https://wiki.ubuntu.com/KernelTeam/GitKernelBuild>`__.
   Clone Linux version 5.4 via::

       git clone --single-branch --branch linux-5.4.y \
           https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
       cd linux

#. Apply the provided FSGSBASE patches to the kernel source tree::

       git am <graphene-sgx-driver>/fsgsbase_patches/*.patch

   The conversation regarding this patchset can be found in the kernel mailing
   list archives `here
   <https://lore.kernel.org/lkml/20200528201402.1708239-1-sashal@kernel.org>`__.

#. Build and install the kernel following `the instructions in the Ubuntu wiki
   <https://wiki.ubuntu.com/KernelTeam/GitKernelBuild>`__.

#. After rebooting, verify the patched kernel is the one that has been booted
   and is running::

       uname -r

#. Also verify that the patched kernel supports FSGSBASE (the below command
   must return that bit 2 is set)::

       LD_SHOW_AUXV=1 /bin/true | grep AT_HWCAP2

After the patched Linux kernel is installed, you may proceed with installations
of other SGX software infrastructure: the Intel SGX Linux driver, the Intel SGX
SDK/PSW, and Graphene itself (see next steps). Note that older versions of
these software packages may not work with recent Linux kernels like 5.4. We
recommend to use commit ``b7ccf6f`` of the Intel SGX Linux Driver for Intel SGX
DCAP and commit ``0e71c22`` of the Intel SGX SDK/PSW.

2b. Install the Graphene FSGSBASE driver (not for production)
"""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""

If you followed step 2a and installed the patched Linux kernel, skip this step.
Otherwise, you will need a Graphene-specific Linux driver that enables the
FSGSBASE feature available in recent processors.

.. warning::

   This module is a |~| quick-and-dirty hack with dangerous security hole
   (allows unauthorized local privilege escalation). "Do not use for production"
   is not a |~| joke. We use it only for testing on very old kernels where the
   patchset does not apply cleanly.

To install the Graphene FSGSBASE driver, run the following commands::

   git clone https://github.com/oscarlab/graphene-sgx-driver
   cd graphene-sgx-driver
   make
   sudo insmod gsgx.ko

3. Generate signing keys
""""""""""""""""""""""""

A 3072-bit RSA private key (PEM format) is required for signing the manifest.
If you don't have a private key, create it with the following command::

   openssl genrsa -3 -out enclave-key.pem 3072

You can either place the generated enclave key in the default path,
:file:`Pal/src/host/Linux-SGX/signer/enclave-key.pem`, or specify the key's
location through the environment variable ``SGX_SIGNER_KEY``.

After signing the application's manifest, users may ship the application and
Graphene binaries, along with an SGX-specific manifest (``.manifest.sgx``
extension), the signature (``.sig`` extension), and the aesmd init token
(``.token`` extension) to execute on another SGX-enabled host.

4. Install the Intel SGX driver and SDK/PSW
"""""""""""""""""""""""""""""""""""""""""""

The Intel SGX Linux SDK and the Intel SGX driver are required to compile and
run Graphene on SGX. Download and install them from the official Intel
GitHub repositories:

- https://github.com/01org/linux-sgx
- https://github.com/01org/linux-sgx-driver

Alternatively, if you want to use the DCAP versions of the SDK and driver,
download and install it from:

- https://github.com/intel/SGXDataCenterAttestationPrimitives

Building
^^^^^^^^

To build Graphene with Intel SGX support, in the root directory of Graphene
repo, run the following command::

   ISGX_DRIVER_PATH=<path-to-sgx-driver-sources> make SGX=1

The path to the SGX driver sources must point to the absolute path where the SGX
driver was downloaded or installed in the previous step. For example, for the
DCAP version 33 of the SGX driver, you must specify
``ISGX_DRIVER_PATH="/usr/src/sgx-1.33/"``. You can define
``ISGX_DRIVER_PATH=""`` to use the default in-kernel driver's C header.

Running :command:`make SGX=1 sgx-tokens` in the test or regression directory
will automatically generate the required manifest signatures (``.sig`` files)
and EINITTOKENs (``.token`` files).

Building with musl instead of glibc
-----------------------------------

To build with a patched musl, add ``LIBC=MUSL`` to the ``make`` invocation.
You'll need to run :command:`make clean` in ``Runtime/`` directory if you
already have Graphene built with glibc.

Additional build options
------------------------

- To create a debug build, run :command:`make DEBUG=1`. This adds debug symbols
  in all Graphene components, builds them without optimizations, and enables
  detailed debug logs in Graphene.

- To build with ``-Werror``, run :command:`make WERROR=1`.

- To specify custom mirrors for downloading the glibc source, use :command:`make
  GLIBC_MIRRORS=...` (and MUSL_MIRRORS for musl).

- Each part of Graphene can be built separately in the subdirectories. For
  example, to build only the Pal component, use :command:`make -c Pal`.
