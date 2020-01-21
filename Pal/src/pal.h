/* Copyright (C) 2014 Stony Brook University
   This file is part of Graphene Library OS.

   Graphene Library OS is free software: you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   Graphene Library OS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/*
 * pal.h
 *
 * This file contains definition of PAL host ABI.
 */

#ifndef PAL_H
#define PAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

typedef uint64_t      PAL_NUM;
typedef const char *  PAL_STR;
typedef void *        PAL_PTR;
typedef uint32_t      PAL_FLG;
typedef uint32_t      PAL_IDX;
typedef bool          PAL_BOL;

/* Moved MAX_FDS from <host_kernel>/pal_host.h to here,
 * since it is 3, across all host kernels. */
#define MAX_FDS 3

#ifdef IN_PAL
#include <atomic.h>
typedef struct atomic_int PAL_REF;

typedef struct {
    PAL_IDX type;
    PAL_FLG flags;
} PAL_HDR;

# include "pal_host.h"

# ifndef HANDLE_HDR
#  define HANDLE_HDR(handle) (&((handle)->hdr))
# endif

static inline void init_handle_hdr(PAL_HDR *hdr, int pal_type) {
    hdr->type = pal_type;
    hdr->flags = 0;
}

# define SET_HANDLE_TYPE(handle, t) init_handle_hdr(HANDLE_HDR(handle), pal_type_##t)
# define IS_HANDLE_TYPE(handle, t) (HANDLE_HDR(handle)->type == pal_type_##t)

#else
typedef union pal_handle
{
    struct {
        PAL_IDX type;
        /* the PAL-level reference counting is deprecated */
    } hdr;
} * PAL_HANDLE;

# ifndef HANDLE_HDR
#  define HANDLE_HDR(handle) (&((handle)->hdr))
# endif

#endif /* !IN_PAL */

#define PAL_LIBOS_TCB_SIZE  256

typedef struct pal_tcb {
    struct pal_tcb * self;
    /* uint64_t for alignment */
    uint64_t libos_tcb[(PAL_LIBOS_TCB_SIZE + sizeof(uint64_t) - 1) / sizeof(uint64_t)];
    /* data private to PAL implementation follows this struct. */
} PAL_TCB;

static inline PAL_TCB * pal_get_tcb (void)
{
    PAL_TCB * tcb;
    __asm__ ("movq %%gs:%c1,%q0"
             : "=r" (tcb)
             : "i" (offsetof(struct pal_tcb, self)));
    return tcb;
}

typedef struct {
#ifdef __x86_64__
    PAL_NUM r8, r9, r10, r11, r12, r13, r14, r15;
    PAL_NUM rdi, rsi, rbp, rbx, rdx, rax, rcx;
    PAL_NUM rsp, rip;
    PAL_NUM efl, csgsfs, err, trapno, oldmask, cr2;
#else
# error "Unsupported architecture"
#endif
} PAL_CONTEXT;

#define PAL_TRUE  true
#define PAL_FALSE false

/********** PAL TYPE DEFINITIONS **********/
enum {
    pal_type_file,
    pal_type_pipe,
    pal_type_pipesrv,
    pal_type_pipecli,
    pal_type_pipeprv,
    pal_type_dev,
    pal_type_dir,
    pal_type_tcp,
    pal_type_tcpsrv,
    pal_type_udp,
    pal_type_udpsrv,
    pal_type_process,
    pal_type_mcast,
    pal_type_thread,
    pal_type_mutex,
    pal_type_event,
    pal_type_eventfd,
    PAL_HANDLE_TYPE_BOUND,
};

#define PAL_IDX_POISON          ((PAL_IDX)-1) /* PAL identifier poison value */
#define PAL_GET_TYPE(h)         (HANDLE_HDR(h)->type)
#define PAL_CHECK_TYPE(h, t)    (PAL_GET_TYPE(h) == pal_type_##t)
#define UNKNOWN_HANDLE(handle)  (PAL_GET_TYPE(handle) >= PAL_HANDLE_TYPE_BOUND)

typedef struct { PAL_PTR start, end; }  PAL_PTR_RANGE;

typedef struct {
    PAL_NUM cpu_num;
    PAL_STR cpu_vendor;
    PAL_STR cpu_brand;
    PAL_NUM cpu_family;
    PAL_NUM cpu_model;
    PAL_NUM cpu_stepping;
    PAL_STR cpu_flags;
} PAL_CPU_INFO;

typedef struct {
    PAL_NUM mem_total;
} PAL_MEM_INFO;

/********** PAL APIs **********/
typedef struct {
    PAL_STR host_type;
    /* An identifier of current picoprocess */
    PAL_NUM process_id;
    PAL_NUM host_id;

    /***** Handles and executables *****/
    /* program manifest */
    PAL_HANDLE manifest_handle;
    /* executable name */
    PAL_STR executable;
    /* handle of parent process */
    PAL_HANDLE parent_process;
    /* handle of first thread */
    PAL_HANDLE first_thread;
    /* debug stream */
    PAL_HANDLE debug_stream;
    /* broadcast RPC stream */
    PAL_HANDLE broadcast_stream;

    /***** Memory layout ******/
    /* The range of user address */
    PAL_PTR_RANGE user_address;
    /* Reserved memory range inside of user address.
     * Used for example by SGX for exec area (including memory gap) in the
     * middle of the heap. If unused set start == end. */
    PAL_PTR_RANGE user_address_hole;
    /* address where executable is loaded */
    PAL_PTR_RANGE executable_range;
    /* manifest preloaded here */
    PAL_PTR_RANGE manifest_preload;

    /***** Host information *****/
    /* Host allocation alignment.
     * This currently is (and most likely will always be) indistinguishable from the page size,
     * looking from the LibOS perspective. The two values can be different on the PAL level though,
     * see e.g. SYSTEM_INFO::dwAllocationGranularity on Windows.
     */
    PAL_NUM alloc_align;
    /* CPU information (only required ones) */
    PAL_CPU_INFO cpu_info;
    /* Memory information (only required ones) */
    PAL_MEM_INFO mem_info;

    /* Attestation information */
    PAL_STR attestation_status;
    PAL_STR attestation_timestamp;

    /* Purely for profiling */
    PAL_NUM startup_time;
    PAL_NUM host_specific_startup_time;
    PAL_NUM relocation_time;
    PAL_NUM linking_time;
    PAL_NUM manifest_loading_time;
    PAL_NUM allocation_time;
    PAL_NUM tail_startup_time;
    PAL_NUM child_creation_time;
} PAL_CONTROL;

#define pal_control (*pal_control_addr())
PAL_CONTROL * pal_control_addr (void);

/* The ABI includes three calls to allocate, free, and modify the
 * permission bits on page-base virtual memory. Permissions in-
 * clude read, write, execute, and guard. Memory regions can be
 * unallocated, reserved, or backed by committed memory
 */

/* Memory Allocation Types */
/*!
 * \brief Actually allocate the memory.
 */
#define PAL_ALLOC_COMMIT      0x0000
/*!
 * \brief Only reserve the memory
 */
#define PAL_ALLOC_RESERVE     0x0001
/*!
 * \brief For PAL-internal use.
 */
#ifdef IN_PAL
#define PAL_ALLOC_INTERNAL    0x8000
#endif

/* Memory Protection Flags */
/*! \brief Page can not be accessed. */
#define PAL_PROT_NONE       0x0
/*! \brief Page can be read. */
#define PAL_PROT_READ       0x1
/*! \brief Page can be written. */
#define PAL_PROT_WRITE      0x2
/*! \brief Page can be executed. */
#define PAL_PROT_EXEC       0x4
/*! \brief Copy on write. */
#define PAL_PROT_WRITECOPY  0x8

#define PAL_PROT_MASK       0xF

/*!
 * \brief Allocate pages of virtual memory from the host.
 *
 * \param addr The virtual address at which to map the memory.
 *
 * \param size The number of bytes to map.  Must be a multiple of the page size (4KiB).
 *
 * \param alloc_type The type of allocation.  This can be PAL_ALLOC_COMMIT,
 *        PAL_ALLOC_RESERVE, and PAL_ALLOC_INTERNAL.
 *
 * \param prot The memory protection flags.  This may be PAL_PROT_NONE, PAL_PROT_READ,
 *        PAL_PROT_WRITE, PAL_PROT_EXEC, and PAL_PROT_WRITECOPY.
 *
 * If addr is NULL, the OS will pick an unmapped address at which to map the memory.  If non-NULL,
 * the memory will be mapped exactly at addr, potentially unmapping other contents.
 */
PAL_PTR
DkVirtualMemoryAlloc (PAL_PTR addr, PAL_NUM size, PAL_FLG alloc_type,
                      PAL_FLG prot);

/*!
 * \brief Unmap pages of virtual memory from the host.
 *
 * \param addr The virtual address at which to unmap the memory.
 *
 * \param size The number of bytes to unmap.  Must be a multiple of the page size (4KiB).
 */
void
DkVirtualMemoryFree (PAL_PTR addr, PAL_NUM size);

/*!
 * \brief Change the memory protection on a region of virtual memory.
 *
 * \param addr The virtual address at which to change the protection.
 *
 * \param size The number of bytes in the region to change.  Must be a multiple of the page size
 * (4KiB).
 *
 * \param prot The memory protection flags.  This may be PAL_PROT_NONE, PAL_PROT_READ,
 *        PAL_PROT_WRITE, PAL_PROT_EXEC, and PAL_PROT_WRITECOPY.
 */
PAL_BOL
DkVirtualMemoryProtect (PAL_PTR addr, PAL_NUM size, PAL_FLG prot);


/* The ABI includes one call to create a child process and one call to
 * terminate the running process. A child process does not inherit
 * any objects or memory from its parent process and the parent
 * process may not modify the execution of its children. A parent can
 * wait for a child to exit using its handle. Parent and child may
 * communicate through I/O streams provided by the parent to the
 * child at creation
 */

#define PAL_PROCESS_MASK         0x0

/*!
 * \brief Create a new Process.  Returns a stream handle to communicate
 *         with the new child.
 *
 * \param uri The URI of the executable to load.
 *
 * \param args The input arguments (argv-style).
 */
PAL_HANDLE
DkProcessCreate (PAL_STR uri, PAL_STR * args);

/*!
 * \brief Terminate the current process.  Does not return.
 *
 * \param exitCode The exit code of the process.
 */
noreturn void
DkProcessExit (PAL_NUM exitCode);

/* The stream ABI includes nine calls to open, read, write, map, unmap,
 * truncate, flush, delete and wait for I/O streams and three calls to
 * access metadata about an I/O stream. The ABI purposefully does not
 * provide an ioctl call. Supported URI schemes include file:, pipe:,
 * http:, https:, tcp:, udp:, pipe.srv:, http.srv, tcp.srv:, and udp.srv:.
 * The latter four schemes are used to open inbound I/O streams for
 * server applications.
 */


/* Stream Access Flags */
/*! \brief Create this stream read-only. */
#define PAL_ACCESS_RDONLY   00
/*! \brief Create this stream write-only. */
#define PAL_ACCESS_WRONLY   01
/*! \brief Create this stream read-write. */
#define PAL_ACCESS_RDWR     02
/*! \brief Open this stream in append mode. */
#define PAL_ACCESS_APPEND   04
#define PAL_ACCESS_MASK     07

/* Stream Sharing Flags */
/*! \brief Globally executable.  Simlar to Unix o+x */
#define PAL_SHARE_GLOBAL_X    01
/*! \brief Globally writable.  Simlar to Unix o+w */
#define PAL_SHARE_GLOBAL_W    02
/*! \brief Globally readable.  Simlar to Unix o+r */
#define PAL_SHARE_GLOBAL_R    04
/*! \brief Group executable.  Simlar to Unix g+x */
#define PAL_SHARE_GROUP_X    010
/*! \brief Group writeable.  Simlar to Unix g+w */
#define PAL_SHARE_GROUP_W    020
/*! \brief Group readable.  Simlar to Unix g+r */
#define PAL_SHARE_GROUP_R    040
/*! \brief Owner executable.  Simlar to Unix u+x */
#define PAL_SHARE_OWNER_X   0100
/*! \brief Owner writeable.  Simlar to Unix u+w */
#define PAL_SHARE_OWNER_W   0200
/*! \brief Owner readable.  Simlar to Unix u+r */
#define PAL_SHARE_OWNER_R   0400
#define PAL_SHARE_MASK      0777

/* Stream Creation Flags */
/*! \brief Create file if file does not exist.  Similar to O_CREAT */
#define PAL_CREATE_TRY        0100
/*! \brief Create file and fail if the file does not exist.  Similar to (O_CREAT|O_EXCL) */
#define PAL_CREATE_ALWAYS     0200
#define PAL_CREATE_MASK       0300

/* Stream Option Flags */
/*! \brief Make this stream non-blocking */
#define PAL_OPTION_NONBLOCK     04000
#define PAL_OPTION_MASK         04000

/* CLOEXEC is generic for any stream.
 * SEMAPHORE is specific to eventfd syscall. */
#define PAL_OPTION_CLOEXEC       01000
#define PAL_OPTION_EFD_SEMAPHORE 02000

/* error value of read/write */
#define PAL_STREAM_ERROR        ((PAL_NUM)-1L)

#define WITHIN_MASK(val, mask)  (((val)|(mask)) == (mask))

/* \brief Create and open a stream.
 *
 * \param access Set the permission on this stream handle, using the PAL Stream Access Flags.
 *
 * \param share_flags If the stream is backed by a file, and that file is created by this call, set
 *        the permissions on that file.
 *
 * \param create Controls whether the call should create a non-existent file, or fail if the file exists.
 *
 * \param options Other options; currently the only option is setting the handle to non-blocking.
 */
PAL_HANDLE
DkStreamOpen (PAL_STR uri, PAL_FLG access, PAL_FLG share_flags,
              PAL_FLG create, PAL_FLG options);

PAL_HANDLE
DkStreamWaitForClient (PAL_HANDLE handle);

PAL_NUM
DkStreamRead (PAL_HANDLE handle, PAL_NUM offset, PAL_NUM count,
              PAL_PTR buffer, PAL_PTR source, PAL_NUM size);

PAL_NUM
DkStreamWrite (PAL_HANDLE handle, PAL_NUM offset, PAL_NUM count,
               PAL_PTR buffer, PAL_STR dest);

#define PAL_DELETE_RD       01
#define PAL_DELETE_WR       02

void
DkStreamDelete (PAL_HANDLE handle, PAL_FLG access);

PAL_PTR
DkStreamMap (PAL_HANDLE handle, PAL_PTR address, PAL_FLG prot,
             PAL_NUM offset, PAL_NUM size);

void
DkStreamUnmap (PAL_PTR addr, PAL_NUM size);

/* Sets the length of the file referenced by handle to length.  Returns the 0
 * on success, a _positive_ errno on failure.
 */
PAL_NUM
DkStreamSetLength (PAL_HANDLE handle, PAL_NUM length);

PAL_BOL
DkStreamFlush (PAL_HANDLE handle);

PAL_BOL
DkSendHandle (PAL_HANDLE handle, PAL_HANDLE cargo);

PAL_HANDLE
DkReceiveHandle (PAL_HANDLE handle);

/* stream attribute structure */
typedef struct {
    PAL_IDX handle_type;
    PAL_BOL disconnected;
    PAL_BOL nonblocking;
    PAL_BOL readable, writable, runnable;
    PAL_FLG share_flags;
    PAL_NUM pending_size;
    PAL_IDX no_of_fds;
    PAL_IDX fds[MAX_FDS];
    union {
        struct {
            PAL_NUM linger;
            PAL_NUM receivebuf, sendbuf;
            PAL_NUM receivetimeout, sendtimeout;
            PAL_BOL tcp_cork;
            PAL_BOL tcp_keepalive;
            PAL_BOL tcp_nodelay;
        } socket;
    };
} PAL_STREAM_ATTR;

PAL_BOL
DkStreamAttributesQuery (PAL_STR uri, PAL_STREAM_ATTR * attr);

PAL_BOL
DkStreamAttributesQueryByHandle (PAL_HANDLE handle,
                                 PAL_STREAM_ATTR * attr);

PAL_BOL
DkStreamAttributesSetByHandle (PAL_HANDLE handle, PAL_STREAM_ATTR * attr);

PAL_NUM
DkStreamGetName (PAL_HANDLE handle, PAL_PTR buffer, PAL_NUM size);

PAL_BOL
DkStreamChangeName (PAL_HANDLE handle, PAL_STR uri);

/* The ABI supports multithreading through five calls to create,
 * sleep, yield the scheduler quantum for, resume execution of, and
 * terminate threads, as well as seven calls to create, signal, and
 * block on synchronization objects
 */

#define PAL_THREAD_MASK         0

PAL_HANDLE
DkThreadCreate (PAL_PTR addr, PAL_PTR param);

// assuming duration to be in microseconds
PAL_NUM
DkThreadDelayExecution (PAL_NUM duration);

void
DkThreadYieldExecution (void);

noreturn void DkThreadExit(PAL_PTR clear_child_tid);

PAL_BOL
DkThreadResume (PAL_HANDLE thread);

/* Exception Handling */
/* arithmetic error (div-by-zero, floating point exception, etc.) */
#define PAL_EVENT_ARITHMETIC_ERROR 1
/* segmentation fault, protection fault, bus fault */
#define PAL_EVENT_MEMFAULT         2
/* illegal instructions */
#define PAL_EVENT_ILLEGAL          3
/* terminated by external program */
#define PAL_EVENT_QUIT             4
/* suspended by external program */
#define PAL_EVENT_SUSPEND          5
/* continued by external program */
#define PAL_EVENT_RESUME           6
/* failure within PAL calls */
#define PAL_EVENT_FAILURE          7

#define PAL_EVENT_NUM_BOUND        8

#define PAL_EVENT_PRIVATE      0x0001       /* upcall specific to thread */
#define PAL_EVENT_RESET        0x0002       /* reset the event upcall */

typedef void (*PAL_EVENT_HANDLER) (PAL_PTR event, PAL_NUM arg, PAL_CONTEXT *);

PAL_BOL
DkSetExceptionHandler (PAL_EVENT_HANDLER handler, PAL_NUM event);

void DkExceptionReturn (PAL_PTR event);


/* parameter: keeping int threadHandle for now (to be in sync with the paper).
 * We may want to replace it with a PAL_HANDLE. Ideally, either use PAL_HANDLE
 * or threadHandle.
 */
/* Create a Mutex.
 * initialCount of 0 is totally unlocked; an initialCount of 1
 * is initialized to locked. */
PAL_HANDLE
DkMutexCreate (PAL_NUM initialCount);

/* Destroy a mutex using DkObjectClose */

void
DkMutexRelease (PAL_HANDLE mutexHandle);

PAL_HANDLE
DkNotificationEventCreate (PAL_BOL initialState);

PAL_HANDLE
DkSynchronizationEventCreate (PAL_BOL initialState);

void
DkEventSet (PAL_HANDLE eventHandle);

void
DkEventClear (PAL_HANDLE eventHandle);

#define NO_TIMEOUT ((PAL_NUM)-1)

PAL_BOL DkSynchronizationObjectWait(PAL_HANDLE handle, PAL_NUM timeout_us);

#define PAL_WAIT_SIGNAL     1   /* ignored in events */
#define PAL_WAIT_READ       2
#define PAL_WAIT_WRITE      4
#define PAL_WAIT_ERROR      8   /* ignored in events */

PAL_BOL DkStreamsWaitEvents(PAL_NUM count, PAL_HANDLE* handle_array, PAL_FLG* events,
                            PAL_FLG* ret_events, PAL_NUM timeout_us);

/* Deprecate DkObjectReference */

void DkObjectClose (PAL_HANDLE objectHandle);

/* the ABI includes seven assorted calls to get wall clock
 * time, generate cryptographically-strong random bits, flush por-
 * tions of instruction caches, increment and decrement the reference
 * counts on objects shared between threads, and to coordinate
 * threads with the security monitor during process serialization
 */

/* assuming the time to be in microseconds */
PAL_NUM
DkSystemTimeQuery (void);

/*
 * Cryptographically secure random.
 * 0 on success, negative on failure.
 */
PAL_NUM
DkRandomBitsRead (PAL_PTR buffer, PAL_NUM size);

PAL_BOL
DkInstructionCacheFlush (PAL_PTR addr, PAL_NUM size);

#define PAL_SEGMENT_FS          0x1
#define PAL_SEGMENT_GS          0x2

PAL_PTR DkSegmentRegister (PAL_FLG reg, PAL_PTR addr);

PAL_NUM DkMemoryAvailableQuota (void);

#define PAL_CPUID_WORD_EAX  0
#define PAL_CPUID_WORD_EBX  1
#define PAL_CPUID_WORD_ECX  2
#define PAL_CPUID_WORD_EDX  3
#define PAL_CPUID_WORD_NUM  4

PAL_BOL
DkCpuIdRetrieve (PAL_IDX leaf, PAL_IDX subleaf, PAL_IDX values[4]);

#ifdef __GNUC__
# define symbol_version_default(real, name, version) \
    __asm__ (".symver " #real "," #name "@@" #version "\n")
#else
# define symbol_version_default(real, name, version)
#endif

#endif /* PAL_H */
