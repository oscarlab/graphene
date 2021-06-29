Manifest syntax
===============

.. highlight:: text

A |~| manifest file is an application-specific configuration text file that
specifies the environment and resources for running an application inside
Graphene. A |~| manifest file contains key-value pairs (as well as more
complicated table and array objects) in the TOML syntax. For the details of the
TOML syntax, see `the official documentation <https://toml.io>`__.

A typical string entry looks like this::

   [Key][.Key][.Key] = "[Value]"

A typical integer entry looks similar to the above but without double quotes::

   [Key][.Key][.Key] = [Value]

Comments can be inlined in a |~| manifest by starting them with a |~| hash sign
(``# comment...``).

There is also a |~| preprocessor available: :ref:`graphene-manifest
<graphene-manifest>`, which renders manifests from Jinja templates.

Common syntax
-------------

Log level
^^^^^^^^^

::

    loader.log_level = "[none|error|warning|debug|trace|all]"
    (Default: "error")

    loader.log_file = "[PATH]"

This configures Graphene's debug log. The ``log_level`` option specifies what
messages to enable (e.g. ``loader.log_level = "debug"`` will enable all messages
of type ``error``, ``warning`` and ``debug``). By default, the messages are printed
to the standard error. If ``log_file`` is specified, the messages will be
appended to that file.

Graphene outputs log messages of the following types:

* ``error``: A serious error preventing Graphene from operating properly (for
  example, error initializing one of the components).

* ``warning``: A non-fatal issue. Might mean that application is requesting
  something unsupported or poorly emulated.

* ``debug``: Detailed information about Graphene's operation and internals.

* ``trace``: More detailed information, such as all system calls requested by
  the application. Might contain a lot of noise.

.. warning::
   Only ``error`` log level is suitable for production. Other levels may leak
   sensitive data.

Preloaded libraries
^^^^^^^^^^^^^^^^^^^

::

   loader.preload = "[URI][,URI]..."

This syntax specifies the libraries to be preloaded before loading the
executable. The URIs of the libraries must be separated by commas. The libraries
must be ELF binaries. This usually contains the LibOS library ``libsysdb.so``.

Entrypoint
^^^^^^^^^^

::

   libos.entrypoint = "[PATH]"

This specifies the first executable which is to be started when spawning a
Graphene instance from this manifest file. Needs to be a path inside Graphene
pointing to a mounted file. Relative paths will be interpreted as starting from
the current working directory (i.e. from ``/`` by default, or ``fs.start_dir``
if specified).

The recommended usage is to provide an absolute path, and mount the executable
at that path. For example::

   libos.entrypoint = "/usr/bin/python3.8"

   fs.mount.python.type = "chroot"
   fs.mount.python.path = "/usr/bin/python3.8"
   fs.mount.python.uri = "file:/usr/bin/python3.8"
   # Or, if using a binary from your local directory:
   # fs.mount.python.uri = "file:python3.8"

.. note ::
   Earlier, ``libos.entrypoint`` was a PAL URI. If you used it with a relative
   path, it's probably enough to remove ``file:`` prefix (convert
   ``"file:hello"`` to ``"hello"``).

Command-line arguments
^^^^^^^^^^^^^^^^^^^^^^

::

   loader.argv0_override = "[STRING]"

This syntax specifies an arbitrary string (typically the executable name) that
will be passed as the first argument (``argv[0]``) to the executable.

If the string is not specified in the manifest, the application will get
``argv[0]`` from :program:`graphene-direct` or :program:`graphene-sgx`
invocation.

::

   loader.insecure__use_cmdline_argv = true

or

::

   loader.argv_src_file = "file:file_with_serialized_argv"

If you want your application to use commandline arguments you need to either set
``loader.insecure__use_cmdline_argv`` (insecure in almost all cases) or point
``loader.argv_src_file`` to a file containing output of
:file:`Tools/argv_serializer`.

``loader.argv_src_file`` is intended to point to either a trusted file or a
protected file. The former allows to securely hardcode arguments (current
manifest syntax doesn't allow to include them inline), the latter allows the
arguments to be provided at runtime from an external (trusted) source.

.. note ::
   Pointing to a protected file is currently not supported, due to the fact that
   PF wrap key provisioning currently happens after setting up arguments.

Environment variables
^^^^^^^^^^^^^^^^^^^^^

::

   loader.insecure__use_host_env = [true|false]

By default, environment variables from the host will *not* be passed to the app.
This can be overridden by the option above, but most applications and runtime
libraries trust their environment variables and are completely insecure when
these are attacker-controlled. For example, an attacker can execute an
additional dynamic library by specifying ``LD_PRELOAD`` variable.

To securely set up the execution environment for an app you should use one or
both of the following options:

::

   loader.env.[ENVIRON] = "[VALUE]"
   loader.env_src_file = "file:file_with_serialized_envs"

``loader.env.[ENVIRON]`` adds/overwrites a single environment variable and can
be used multiple times to specify more than one variable.

``loader.env_src_file`` allows to specify a URI to a file containing serialized
environment, which can be generated using :file:`Tools/argv_serializer`. This
option is intended to point to either a trusted file or a protected file. The
former allows to securely hardcode environments (in a more flexible way than
``loader.env.[ENVIRON]`` option), the latter allows the environments to be
provided at runtime from an external (trusted) source.

.. note ::
   Pointing to a protected file is currently not supported, due to the fact that
   PF wrap key provisioning currently happens after setting up environment
   variables.

If the same variable is set in both, then ``loader.env.[ENVIRON]`` takes
precedence.

Disabling ASLR
^^^^^^^^^^^^^^

::

    loader.insecure__disable_aslr = [true|false]
    (Default: false)

This specifies whether to disable Address Space Layout Randomization (ASLR).
Since disabling ASLR worsens security of the application, ASLR is enabled by
default.

Check invalid pointers
^^^^^^^^^^^^^^^^^^^^^^

::

    libos.check_invalid_pointers = [true|false]
    (Default: true)

This specifies whether to enable checks of invalid pointers on syscall
invocations. In particular, when this manifest option is set to ``true``,
Graphene's LibOS will return an EFAULT error code if a user-supplied buffer
points to an invalid memory region. Setting this manifest option to ``false``
may improve performance for certain workloads but may also generate
``SIGSEGV/SIGBUS`` exceptions for some applications that specifically use
invalid pointers (though this is not expected for most real-world applications).

Graphene internal metadata size
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

    loader.pal_internal_mem_size = "[SIZE]"
    (default: "0")

This syntax specifies how much additional memory Graphene reserves for its
internal use (e.g., metadata for trusted/protected files, internal handles,
etc.). By default, Graphene pre-allocates 64MB of internal memory for this
metadata, but for huge workloads this limit may be not enough. In this case,
Graphene loudly fails with "out of PAL memory" error. To run huge workloads,
increase this limit by setting this option to e.g. ``64M`` (this would result in
a total of 128MB used by Graphene for internal metadata). Note that this limit
is included in ``sgx.enclave_size``, so if your enclave size is e.g. 512MB and
you specify ``loader.pal_internal_mem_size = "64MB"``, then your application is
left with 384MB of usable memory.

Stack size
^^^^^^^^^^

::

    sys.stack.size = "[SIZE]"
    (default: "256K")

This specifies the stack size of each thread in each Graphene process. The
default value is determined by the library OS. Units like ``K`` |~| (KiB),
``M`` |~| (MiB), and ``G`` |~| (GiB) can be appended to the values for
convenience. For example, ``sys.stack.size = "1M"`` indicates a 1 |~| MiB stack
size.

Program break (brk) size
^^^^^^^^^^^^^^^^^^^^^^^^

::

    sys.brk.max_size = "[SIZE]"
    (default: "256K")

This specifies the maximal program break (brk) size in each Graphene process.
The default value of the program break size is determined by the library OS.
Units like ``K`` (KiB), ``M`` (MiB), and ``G`` (GiB) can be appended to the
values for convenience. For example, ``sys.brk.max_size = "1M"`` indicates
a 1 |~| MiB brk size.

Allowing eventfd
^^^^^^^^^^^^^^^^

::

    sys.insecure__allow_eventfd = [true|false]
    (Default: false)

This specifies whether to allow system calls `eventfd()` and `eventfd2()`. Since
eventfd emulation currently relies on the host, these system calls are
disallowed by default due to security concerns.

External SIGTERM injection
^^^^^^^^^^^^^^^^^^^^^^^^^^

::

    sys.enable_sigterm_injection = [true|false]
    (Default: false)

This specifies whether to allow for a one-time injection of `SIGTERM` signal
into Graphene. Could be useful to handle graceful shutdown.
Be careful! In SGX environment, the untrusted host could inject that signal in
an arbitrary moment. Examine what your application's `SIGTERM` handler does and
whether it poses any security threat.

Root FS mount point
^^^^^^^^^^^^^^^^^^^

::

    fs.root.[identifier].type = "[chroot|...]"
    fs.root.[identifier].path = "[PATH]"
    fs.root.[identifier].uri  = "[URI]"

This syntax specifies the root file system to be mounted inside the library OS.
If not specified, then Graphene mounts the current working directory as the
root. There can be only one root FS mount point specified in the manifest.

FS mount points
^^^^^^^^^^^^^^^

::

    fs.mount.[identifier].type = "[chroot|tmpfs]"
    fs.mount.[identifier].path = "[PATH]"
    fs.mount.[identifier].uri  = "[URI]"

This syntax specifies how file systems are mounted inside the library OS. For
dynamically linked binaries, usually at least one `chroot` mount point is
required in the manifest (the mount point of the Glibc library).

Graphene currently supports two types of mount points:

* ``chroot``: Host-backed files. All host files and sub-directories found under
  ``[URI]`` are forwarded to the Graphene instance and placed under ``[PATH]``.
  For example, with a host-level path specified as
  ``fs.mount.lib.uri = "file:graphene/Runtime/"`` and forwarded to Graphene via
  ``fs.mount.lib.path = "/lib"``, a host-level file
  ``graphene/Runtime/libc.so.6`` is visible to graphenized application as
  ``/lib/libc.so.6``. This concept is similar to FreeBSD's chroot and to
  Docker's named volumes. Files under ``chroot`` mount points support mmap and
  fork/clone.

* ``tmpfs``: Temporary in-memory-only files. These files are *not* backed by
  host-level files. The tmpfs files are created under ``[PATH]`` (this path is
  empty on Graphene instance startup) and are destroyed when a Graphene
  instance terminates. The ``[URI]`` parameter is always ignored. ``tmpfs``
  is especially useful in trusted environments (like Intel SGX) for securely
  storing temporary files. This concept is similar to Linux's tmpfs. Files
  under ``tmpfs`` mount points currently do *not* support mmap and each process
  has its own, non-shared tmpfs (i.e. processes don't see each other's files).

Start (current working) directory
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

    fs.start_dir = "[URI]"

This syntax specifies the start (current working) directory. If not specified,
then Graphene sets the root directory as the start directory (see ``fs.root``).


SGX syntax
----------

If Graphene is *not* running with SGX, the SGX-specific syntax is ignored. All
keys in the SGX-specific syntax are optional.

Debug/production enclave
^^^^^^^^^^^^^^^^^^^^^^^^

::

    sgx.debug = [true|false]
    (Default: true)

This syntax specifies whether the enclave can be debugged. Set it to ``true``
for a |~| debug enclave and to ``false`` for a |~| production enclave.

Enclave size
^^^^^^^^^^^^

::

    sgx.enclave_size = "[SIZE]"
    (default: "256M")

This syntax specifies the size of the enclave set during enclave creation time
(recall that SGX |~| v1 requires a predetermined maximum size of the enclave).
The PAL and library OS code/data count towards this size value, as well as the
application memory itself: application's code, stack, heap, loaded application
libraries, etc. The application cannot allocate memory that exceeds this limit.

Non-PIE binaries
^^^^^^^^^^^^^^^^

::

    sgx.nonpie_binary = [true|false]
    (Default: false)

This setting tells Graphene whether to use a specially crafted memory layout,
which is required to support non-relocatable binaries (non-PIE).

Number of threads
^^^^^^^^^^^^^^^^^

::

    sgx.thread_num = [NUM]
    (Default: 4)

This syntax specifies the maximum number of threads that can be created inside
the enclave (recall that SGX |~| v1 requires a |~| predetermined maximum number
of thread slots). The application cannot have more threads than this limit *at
a time* (however, it is possible to create new threads after old threads are
destroyed).

Number of RPC threads (Exitless feature)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

    sgx.rpc_thread_num = [NUM]
    (Default: 0)

This syntax specifies the number of RPC threads that are created outside of
the enclave. RPC threads are helper threads that run in untrusted mode
alongside enclave threads. RPC threads issue system calls on behalf of enclave
threads. This allows "exitless" design when application threads never leave
the enclave (except for a few syscalls where there is no benefit, e.g.,
``nanosleep()``).

If user specifies ``0`` or omits this directive, then no RPC threads are
created and all system calls perform an enclave exit ("normal" execution).

Note that the number of created RPC threads must match the maximum number of
simultaneous enclave threads. If there are more RPC threads, then CPU time is
wasted. If there are less RPC threads, some enclave threads may starve,
especially if there are many blocking system calls by other enclave threads.

The Exitless feature *may be detrimental for performance*. It trades slow
OCALLs/ECALLs for fast shared-memory communication at the cost of occupying
more CPU cores and burning more CPU cycles. For example, a single-threaded
Redis instance on Linux becomes 5-threaded on Graphene with Exitless. Thus,
Exitless may negatively impact throughput but may improve latency.

Optional CPU features (AVX, AVX512, MPX, PKRU)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

    sgx.require_avx    = [true|false]
    sgx.require_avx512 = [true|false]
    sgx.require_mpx    = [true|false]
    sgx.require_pkru   = [true|false]
    (Default: false)

This syntax ensures that the CPU features are available and enabled for the
enclave. If the options are set in the manifest but the features are unavailable
on the platform, enclave initialization will fail. If the options are unset,
enclave initialization will succeed even if these features are unavailable on
the platform.

ISV Product ID and SVN
^^^^^^^^^^^^^^^^^^^^^^

::

    sgx.isvprodid = [NUM]
    sgx.isvsvn    = [NUM]
    (Default: 0)

This syntax specifies the ISV Product ID and SVN to be added to the enclave
signature.

Allowed files
^^^^^^^^^^^^^

::

    sgx.allowed_files.[identifier] = "[URI]"

This syntax specifies the files that are allowed to be created or loaded into
the enclave unconditionally. In other words, allowed files can be opened for
reading/writing and can be created if they do not exist already. Allowed files
are not cryptographically hashed and are thus not protected.

.. warning::
   It is insecure to allow files containing code or critical information;
   developers must not allow files blindly! Instead, use trusted or protected
   files.

Trusted files
^^^^^^^^^^^^^

::

    sgx.trusted_files.[identifier] = "[URI]"

This syntax specifies the files to be cryptographically hashed at build time,
and allowed to be accessed by the app in runtime only if their hashes match.
This implies that trusted files can be only opened for reading (not for writing)
and cannot be created if they do not exist already. The signer tool will
automatically generate hashes of these files and add them to the SGX-specific
manifest (``.manifest.sgx``). Marking files as trusted is especially useful for
shared libraries: a |~| trusted library cannot be silently replaced by a
malicious host because the hash verification will fail.

Protected files
^^^^^^^^^^^^^^^

::

    sgx.protected_files_key = "[16-byte hex value]"
    sgx.protected_files.[identifier]           = "[URI]"
    sgx.protected_mrenclave_files.[identifier] = "[URI]"
    sgx.protected_mrsigner_files.[identifier]  = "[URI]"

This syntax specifies the files that are encrypted on disk and transparently
decrypted when accessed by Graphene or by application running inside Graphene.
Protected files guarantee data confidentiality and integrity (tamper
resistance), as well as file swap protection (a protected file can only be
accessed when in a specific path).

URI can be a file or a directory. If a directory is specified, all existing
files/directories within it are registered as protected recursively (and are
expected to be encrypted in the PF format). New files created in a protected
directory are automatically treated as protected.

Note that path size of a protected file is limited to 512 bytes and filename
size is limited to 260 bytes.

``sgx.protected_files_key`` specifies the wrap (master) encryption key and must
be used only for debugging purposes. In production environments, this key must
be provisioned to the enclave using local/remote attestation.

``sgx.protected_files`` are encrypted using the wrap (master) encryption key;
they are well-suited for input files encrypted by the user and sent to the
deployment platform as well as for output files sent back to the user and
decrypted at the user side. ``sgx.protected_mrenclave_files`` are encrypted
using the SGX sealing key based on the MRENCLAVE identity of the enclave; they
are useful to allow only the same enclave (on the same platform) to unseal
files. ``sgx.protected_mrsigner_files`` are encrypted using the SGX sealing key
based on the MRSIGNER identity of the enclave; they are useful to allow all
enclaves from the same signer (but on the same platform) to unseal files.

File check policy
^^^^^^^^^^^^^^^^^

::

    sgx.file_check_policy = "[strict|allow_all_but_log]"
    (Default: "strict")

This syntax specifies the file check policy, determining the behavior of
authentication when opening files. By default, only files explicitly listed as
``trusted_files`` or ``allowed_files`` declared in the manifest are allowed for
access.

If the file check policy is ``allow_all_but_log``, all files other than trusted
and allowed are allowed for access, and Graphene-SGX emits a warning message for
every such file. Effectively, this policy operates on all unknown files as if
they were listed as ``allowed_files``. (However, this policy still does not
allow writing/creating files specified as trusted.) This policy is a convenient
way to determine the set of files that the ported application uses.

Attestation and quotes
^^^^^^^^^^^^^^^^^^^^^^

::

    sgx.remote_attestation = [true|false]
    (Default: false)

    sgx.ra_client_linkable = [true|false]
    sgx.ra_client_spid     = "[HEX]"

This syntax specifies the parameters for remote attestation. To enable it,
``remote_attestation`` must be set to ``true``.

For EPID based attestation, ``ra_client_linkable`` and ``ra_client_spid`` must
be filled with your registered Intel SGX EPID Attestation Service credentials
(linkable/unlinkable mode and SPID of the client respectively).

For DCAP/ECDSA based attestation, ``ra_client_spid`` must be an empty string
(this is a hint to Graphene to use DCAP instead of EPID) and
``ra_client_linkable`` is ignored.

Pre-heating enclave
^^^^^^^^^^^^^^^^^^^

::

    sgx.preheat_enclave = [true|false]
    (Default: false)

When enabled, this option instructs Graphene to pre-fault all heap pages during
initialization. This has a negative impact on the total run time, but shifts the
:term:`EPC` page faults cost to the initialization phase, which can be useful in
a scenario where a server starts and receives connections / work packages only
after some time. It also makes the later run time and latency much more
predictable.

Please note that using this option makes sense only when the :term:`EPC` is
large enough to hold the whole heap area.

Enabling per-thread and process-wide SGX stats
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

::

    sgx.enable_stats = [true|false]
    (Default: false)

This syntax specifies whether to enable SGX enclave-specific statistics:

#. ``TCS.FLAGS.DBGOPTIN`` flag. This flag is set in all enclave threads and
   enables certain debug and profiling features with enclaves, including
   breakpoints, performance counters, Intel PT, etc.

#. Printing the stats on SGX-specific events. Currently supported stats are:
   number of EENTERs (corresponds to ECALLs plus returns from OCALLs), number
   of EEXITs (corresponds to OCALLs plus returns from ECALLs) and number of
   AEXs (corresponds to interrupts/exceptions/signals during enclave
   execution). Prints per-thread and per-process stats.

#. Printing the SGX enclave loading time at startup. The enclave loading time
   includes creating the enclave, adding enclave pages, measuring them and
   initializing the enclave.

.. warning::
   This option is insecure and cannot be used with production enclaves
   (``sgx.debug = false``). If a production enclave is started with this option
   set, Graphene will fail initialization of the enclave.

SGX profiling
^^^^^^^^^^^^^

::

    sgx.profile.enable = ["none"|"main"|"all"]
    (Default: "none")

This syntax specifies whether to enable SGX profiling. Graphene must be compiled
with ``DEBUG=1`` or ``DEBUGOPT=1`` for this option to work (the latter is
advised).

If this option is set to ``main``, the main process will collect IP samples and
save them as ``sgx-perf.data``. If it is set to ``all``, all processes will
collect samples and save them to ``sgx-perf-<PID>.data``.

The saved files can be viewed with the ``perf`` tool, e.g. ``perf report -i
sgx-perf.data``.

See :ref:`sgx-profile` for more information.

.. warning::
   This option is insecure and cannot be used with production enclaves
   (``sgx.debug = false``). If a production enclave is started with this option
   set, Graphene will fail initialization of the enclave.

::

    sgx.profile.mode = ["aex"|"ocall_inner"|"ocall_outer"]
    (Default: "aex")

Specifies what events to record:

* ``aex``: Records enclave state during asynchronous enclave exit (AEX). Use
  this to check where the CPU time is spent in the enclave.

* ``ocall_inner``: Records enclave state during OCALL.

* ``ocall_outer``: Records the outer OCALL function, i.e. what OCALL handlers
  are going to be executed. Does not include stack information (cannot be used
  with ``sgx.profile.with_stack = true``).

See also :ref:`sgx-profile-ocall` for more detailed advice regarding the OCALL
modes.

::

    sgx.profile.with_stack = [true|false]
    (Default: false)

This syntax specifies whether to include stack information with the profiling
data. This will enable ``perf report`` to show call chains. However, it will
make the output file much bigger, and slow down the process.

::

    sgx.profile.frequency = [INTEGER]
    (Default: 50)

This syntax specifies approximate frequency at which profiling samples are taken
(in samples per second). Lower values will mean less accurate results, but also
lower overhead.

Note that the accuracy is limited by how often the process is interrupted by
Linux scheduler: the effective maximum is 250 samples per second.

.. note::
   This option applies only to ``aex`` mode. In the ``ocall_*`` modes, currently
   all samples are taken.
