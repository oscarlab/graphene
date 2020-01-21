PAL ABI Documentation
=====================

PAL Types
#########

XXX: Do we really want to keep PAL_STR and friends?  Or just switch to a C99 type?

Virtual Memory Management
#########################

.. doxygenfunction:: DkVirtualMemoryAlloc(PAL_PTR, PAL_NUM, PAL_FLG, PAL_FLG)

This function accepts the `PAL Allocation Flags`_ described below as the alloc_type argument,
and the `PAL Protection Flags`_ described below for the prot argument.

.. doxygenfunction:: DkVirtualMemoryFree(PAL_PTR, PAL_NUM)

.. doxygenfunction:: DkVirtualMemoryProtect(PAL_PTR, PAL_NUM, PAL_FLG)

This function uses the same `PAL Protection Flags`_, described below, as DkVirtualMemoryAlloc uses.

PAL Allocation Flags
********************

.. doxygendefine:: PAL_ALLOC_COMMIT

.. doxygendefine:: PAL_ALLOC_RESERVE

.. doxygendefine:: PAL_ALLOC_INTERNAL

PAL Protection Flags
********************

.. doxygendefine:: PAL_PROT_NONE

.. doxygendefine:: PAL_PROT_READ

.. doxygendefine:: PAL_PROT_WRITE

.. doxygendefine:: PAL_PROT_EXEC

.. doxygendefine:: PAL_PROT_WRITECOPY


Process Management
##################

The ABI includes one call to create a child process and one call to
terminate the running process. A child process does not inherit
any objects or memory from its parent process and the parent
process may not modify the execution of its children. A parent can
wait for a child to exit using its handle. Parent and child may
communicate through I/O streams provided by the parent to the
child at creation.

.. doxygenfunction:: DkProcessCreate(PAL_STR, PAL_STR *)

.. doxygenfunction:: DkProcessExit(PAL_NUM)

Streams
#######

The PAL abstracts a general notion of I/O to the abstraction of a
stream.  A stream is like a Unix file handle: it can represent
a file, pipe, process, or socket.

The stream ABI includes nine calls to open, read, write, map, unmap,
truncate, flush, delete and wait for I/O streams and three calls to
access metadata about an I/O stream. The ABI purposefully does not
provide an ioctl call. Supported URI schemes include file:, pipe:,
http:, https:, tcp:, udp:, pipe.srv:, http.srv, tcp.srv:, and udp.srv:.
The latter four schemes are used to open inbound I/O streams for
server applications.

.. doxygenfunction:: DkStreamOpen(PAL_STR, PAL_FLG, PAL_FLG, PAL_FLG, PAL_FLG)

Note that the access parameter can be one of the options listed in `PAL Stream Access Flags`_ below.
Similarly, share_flags is one of the options below under `PAL Stream Sharing Flags`_, create should
be one of the options under `PAL Stream Creation Options`_, and options is defined under `PAL Stream
Options`_.



PAL Stream Access Flags
***********************

.. doxygendefine:: PAL_ACCESS_RDONLY

.. doxygendefine:: PAL_ACCESS_WRONLY

.. doxygendefine:: PAL_ACCESS_RDWR

.. doxygendefine:: PAL_ACCESS_APPEND

PAL Stream Sharing Flags
************************

.. doxygendefine:: PAL_SHARE_GLOBAL_X

.. doxygendefine:: PAL_SHARE_GLOBAL_W

.. doxygendefine:: PAL_SHARE_GLOBAL_R

.. doxygendefine:: PAL_SHARE_GROUP_X

.. doxygendefine:: PAL_SHARE_GROUP_W

.. doxygendefine:: PAL_SHARE_GROUP_R

.. doxygendefine:: PAL_SHARE_OWNER_X

.. doxygendefine:: PAL_SHARE_OWNER_W

.. doxygendefine:: PAL_SHARE_OWNER_R

PAL Stream Creation Options
***************************

.. doxygendefine:: PAL_CREATE_TRY

.. doxygendefine:: PAL_CREATE_ALWAYS

PAL Stream Options
******************

.. doxygendefine:: PAL_OPTION_NONBLOCK
