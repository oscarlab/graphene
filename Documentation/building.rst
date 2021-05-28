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
- A patched GNU C Library (a set of shared libraries ``libc.so``,
  ``libpthread.so``, ``libm.so``, etc.)

The build of Graphene implies building at least the first two components. The
build of the patched C library is optional but highly recommended for
performance reasons. The patched C library is built by default.

Graphene currently only works on the x86_64 architecture. Graphene is currently
tested on Ubuntu 20.04 and 18.04, along with Linux kernel version 5.x. We
recommend building and installing Graphene on the same host platform. If you
find problems with Graphene on other Linux distributions, please contact us with
a |~| detailed `bug report <https://github.com/oscarlab/graphene/issues/new>`__.

Installing dependencies
-----------------------

Common dependencies
^^^^^^^^^^^^^^^^^^^

Run the following command on Ubuntu to install dependencies::

    sudo apt-get install -y \
        build-essential \
        autoconf \
        bison \
        gawk \
        meson \
        python3-click \
        python3-jinja2

For GDB support and to run all tests locally you also need to install::

    sudo apt-get install -y python3-pyelftools python3-pytest libunwind8

Dependencies for SGX
^^^^^^^^^^^^^^^^^^^^

The build of Graphene with SGX support requires the corresponding SGX software
infrastructure to be installed on the system. In particular, the FSGSBASE
functionality must be enabled in the Linux kernel, the Intel SGX driver must be
running, and Intel SGX SDK/PSW/DCAP must be installed. In the future, when all
required SGX infrastructure is upstreamed in Linux and popular Linux
distributions, the prerequisite steps will be significantly simplified.

1. Required packages
""""""""""""""""""""
Run the following commands on Ubuntu to install SGX-related dependencies::

    sudo apt-get install -y \
        libcurl4-openssl-dev \
        libprotobuf-c-dev \
        protobuf-c-compiler \
        python3-pip \
        python3-protobuf
    python3 -m pip install toml>=0.10

    # For Debian
    sudo apt-get install -y python3-protobuf protobuf-compiler

2. Install the Linux kernel patched with FSGSBASE
"""""""""""""""""""""""""""""""""""""""""""""""""
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

3. Install the Intel SGX driver and SDK/PSW
"""""""""""""""""""""""""""""""""""""""""""

This step depends on your hardware and kernel version.

If your CPU supports :term:`FLC`, we recommend you install kernel 5.11 or later.
The SGX driver is part of the upstream kernel. You only need to install SDK/PSW
from one of the following choices.

If you have an older CPU without :term:`FLC` support, you need to install the
Intel SGX Linux SDK and the Intel SGX driver. Download and install them from the
official Intel GitHub repositories:

- https://github.com/intel/linux-sgx
- https://github.com/intel/linux-sgx-driver

Alternatively, if your CPU supports :term:`FLC`, you can choose to install DCAP
versions of the SDK and driver, download and install them from:

- https://github.com/intel/SGXDataCenterAttestationPrimitives

4. Generate signing keys
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

Building
--------

.. note::

   We're in the middle of the migration from Make to Meson. In the meantime you
   need to run **both** buildchains, first :command:`make` then
   :command:`meson`.

To build Graphene, in the root directory of Graphene repo, run the following
commands::

   # if you build graphene-direct (note that "direct" means non-SGX version)
   make

   # if you build graphene-sgx
   make SGX=1 ISGX_DRIVER_PATH=<path-to-sgx-driver-sources>

The path to the SGX driver sources must point to the absolute path where the SGX
driver was downloaded or installed in the previous step. For example, for the
DCAP version 33 of the SGX driver, you must specify
``ISGX_DRIVER_PATH="/usr/src/sgx-1.33/"``. You can define
``ISGX_DRIVER_PATH=""`` to use the default in-kernel driver's C header.

Running :command:`make SGX=1 sgx-tokens` in the test or regression directory
will automatically generate the required manifest signatures (``.sig`` files)
and EINITTOKENs (``.token`` files).

Then install graphene (recall that "direct" means non-SGX version)::

   meson build -Ddirect=enabled -Dsgx=enabled
   ninja -C build
   sudo ninja -C build install

Set ``-Ddirect=`` and ``-Dsgx=`` options to ``enabled`` or ``disabled``
according to whether you built the corresponding PAL (the snippet assumes you
built both).

Additional build options
^^^^^^^^^^^^^^^^^^^^^^^^

- To create a debug build, run :command:`make DEBUG=1`. This adds debug symbols
  in all Graphene components, builds them without optimizations, and enables
  detailed debug logs in Graphene.

- To create a debug build that does not disable optimizations, run
  :command:`make DEBUGOPT=1`.

  *Note:* this is generally *not* recommended, because optimized builds lose
  some debugging information, and may cause GDB to display confusing tracebacks
  or garbage data. You should use ``DEBUGOPT=1`` only if you have a good reason
  (e.g. for profiling).

- To build with ``-Werror``, run :command:`make WERROR=1` and
  :command:`meson build --werror`.

- To specify custom mirrors for downloading the Glibc source, use :command:`make
  GLIBC_MIRRORS=...`.

- To install into some other place than :file:`/usr/local`, use
  :command:`meson build --prefix=<prefix>`. Note that you then need to include
  the :file:`<prefix>/bin` directory in ``$PATH`` and
  :file:`<prefix>/lib/python<version>/site-packages` in ``$PYTHONPATH``.
