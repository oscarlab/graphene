#ifndef _SHIM_TYPES_H_
#define _SHIM_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <asm/poll.h>
#include <asm/posix_types.h>
#include <asm/siginfo.h>
#include <asm/signal.h>
#include <asm/stat.h>
#include <asm/statfs.h>
#include <linux/aio_abi.h>
#include <linux/futex.h>
#include <linux/kernel.h>
#include <linux/msg.h>
#include <linux/perf_event.h>
#include <linux/sem.h>
#include <linux/shm.h>
#include <linux/times.h>
#include <linux/timex.h>
#include <linux/types.h>
#include <linux/utime.h>
#include <linux/utsname.h>
#include <linux/version.h>

#include "elf.h"
#include "pal.h"
#include "shim_types-arch.h"

typedef unsigned int __u32;

typedef unsigned long int nfds_t;
typedef unsigned long int nlink_t;

typedef __kernel_uid_t     uid_t;
typedef __kernel_gid_t     gid_t;
typedef __kernel_pid_t     pid_t;
typedef __kernel_caddr_t   caddr_t;
typedef __kernel_mode_t    mode_t;
typedef __kernel_off_t     off_t;
typedef __kernel_loff_t    loff_t;
typedef __kernel_time_t    time_t;
typedef __kernel_old_dev_t dev_t;
typedef __kernel_ino_t     ino_t;
typedef __kernel_clockid_t clockid_t;
typedef __kernel_key_t     key_t;
typedef __kernel_timer_t   timer_t;
typedef __kernel_fd_set    fd_set;

/* linux/time.h */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
struct __kernel_timespec {
    __kernel_time_t tv_sec; /* seconds */
    long tv_nsec;           /* nanoseconds */
};
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0)
struct __kernel_itimerspec {
    struct __kernel_timespec it_interval; /* timer period */
    struct __kernel_timespec it_value;    /* timer expiration */
};
#endif

struct __kernel_timeval {
    __kernel_time_t tv_sec;       /* seconds */
    __kernel_suseconds_t tv_usec; /* microsecond */
};

struct __kernel_itimerval {
    struct __kernel_timeval it_interval; /* time interval */
    struct __kernel_timeval it_value;    /* current value */
};

struct __kernel_timezone {
    int tz_minuteswest; /* minutes west of Greenwich */
    int tz_dsttime;     /* type of dst correction */
};

/* linux/time.h
 * syscall interface - used (mainly by NTP daemon)
 * to discipline kernel clock oscillator
 */
struct ____kernel_timex {
    unsigned int modes;           /* mode selector */
    long offset;                  /* time offset (usec) */
    long freq;                    /* frequency offset (scaled ppm) */
    long maxerror;                /* maximum error (usec) */
    long esterror;                /* estimated error (usec) */
    int status;                   /* clock command/status */
    long constant;                /* pll time constant */
    long precision;               /* clock precision (usec) (read only) */
    long tolerance;               /* clock frequency tolerance (ppm) (read only) */
    struct __kernel_timeval time; /* (read only) */
    long tick;                    /* (modified) usecs between clock ticks */

    long ppsfreq; /* pps frequency (scaled ppm) (ro) */
    long jitter;  /* pps jitter (us) (ro) */
    int shift;    /* interval duration (s) (shift) (ro) */
    long stabil;  /* pps stability (scaled ppm) (ro) */
    long jitcnt;  /* jitter limit exceeded (ro) */
    long calcnt;  /* calibration intervals (ro) */
    long errcnt;  /* calibration errors (ro) */
    long stbcnt;  /* stability limit exceeded (ro) */

    int tai; /* TAI offset (ro) */

    int : 32;
    int : 32;
    int : 32;
    int : 32;
    int : 32;
    int : 32;
    int : 32;
    int : 32;
    int : 32;
    int : 32;
    int : 32;
};

/* /arch/x86/include/asm/posix_types_64.h */
typedef unsigned int __kernel_uid_t;
typedef __kernel_uid_t __kernel_uid32_t;

/* quota.h */
typedef __kernel_uid32_t qid_t; /* Type in which we store ids in memory */

/* capability.h */
typedef struct __user_cap_header_struct {
    __u32 version;
    int pid;
}* cap_user_header_t;

typedef struct __user_cap_data_struct {
    __u32 effective;
    __u32 permitted;
    __u32 inheritable;
}* cap_user_data_t;

/* defined in function in sysdeps/unix/sysv/linux/sysctl.c */
struct __kernel_sysctl_args {
    int* name;       /* integer vector describing variable */
    int nlen;        /* length of this vector */
    void* oldval;    /* 0 or address where to store old value */
    size_t* oldlenp; /* available room for old value,
                        overwritten by actual size of old value */
    void* newval;    /* 0 or address of new value */
    size_t newlen;   /* size of new value */
};

struct __kernel_sched_param {
    int __sched_priority;
};

struct __kernel_sigaction {
    __sighandler_t k_sa_handler;
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    __sigset_t sa_mask;
};

/* linux/aio_abi.h (for io_setup which has no libc wrapper) */
typedef unsigned long aio_context_t;

/* linux/rlimit.h */
struct __kernel_rusage {
    struct __kernel_timeval ru_utime; /* user time used */
    struct __kernel_timeval ru_stime; /* system time used */
    long ru_maxrss;                   /* maximum resident set size */
    long ru_ixrss;                    /* integral shared memory size */
    long ru_idrss;                    /* integral unshared data size */
    long ru_isrss;                    /* integral unshared stack size */
    long ru_minflt;                   /* page reclaims */
    long ru_majflt;                   /* page faults */
    long ru_nswap;                    /* swaps */
    long ru_inblock;                  /* block input operations */
    long ru_oublock;                  /* block output operations */
    long ru_msgsnd;                   /* messages sent */
    long ru_msgrcv;                   /* messages received */
    long ru_nsignals;                 /* signals received */
    long ru_nvcsw;                    /* voluntary context switches */
    long ru_nivcsw;                   /* involuntary " */
};

struct __kernel_rlimit {
    unsigned long rlim_cur, rlim_max;
};

struct __kernel_rlimit64 {
    uint64_t rlim_cur, rlim_max;
};

/* linux/eventpoll.h
 * On x86-64 make the 64bit structure have the same alignment as the
 * 32bit structure. This makes 32bit emulation easier.
 *
 * UML/x86_64 needs the same packing as x86_64
 */
struct __kernel_epoll_event {
    __u32 events;
    __u64 data;
}
#ifdef __x86_64__
__attribute__((packed));
#else
;
#endif

/* bits/ustat.h */
struct __kernel_ustat {
    __daddr_t f_tfree; /* Number of free blocks. */
    __ino_t f_tinode;  /* Number of free inodes. */
    char f_fname[6];
    char f_fpack[6];
};

/* bits/socket.h */
enum {
    MSG_OOB      = 0x01,   /* Process out-of-band data. */
    MSG_PEEK     = 0x02,   /* Peek at incoming messages. */
    MSG_DONTWAIT = 0x40,   /* Nonblocking IO. */
    MSG_WAITALL  = 0x100,  /* Wait for full request or error */
    MSG_NOSIGNAL = 0x4000, /* Do not generate SIGPIPE. */
#define MSG_OOB      MSG_OOB
#define MSG_PEEK     MSG_PEEK
#define MSG_DONTWAIT MSG_DONTWAIT
#define MSG_WAITALL  MSG_WAITALL
#define MSG_NOSIGNAL MSG_NOSIGNAL
};

struct msghdr {
    void* msg_name;  /* Address to send to/receive from.  */
    int msg_namelen; /* Length of address data.  */

    struct iovec* msg_iov; /* Vector of data to send/receive into.  */
    size_t msg_iovlen;     /* Number of elements in the vector.  */

    void* msg_control;     /* Ancillary data (eg BSD filedesc passing). */
    size_t msg_controllen; /* Ancillary data buffer length. */

    unsigned int msg_flags; /* Flags on received message.  */
};

/* For `recvmmsg'.  */
struct mmsghdr {
    struct msghdr msg_hdr; /* Actual message header. */
    unsigned int msg_len;  /* Number of received bytes for the entry. */
};

/* POSIX.1g specifies this type name for the `sa_family' member.  */
typedef unsigned short int sa_family_t;

/* This macro is used to declare the initial common members
   of the data types used for socket addresses, `struct sockaddr',
   `struct sockaddr_in', `struct sockaddr_un', etc.  */

#define __SOCKADDR_COMMON(sa_prefix) \
    sa_family_t sa_prefix##family

/* Structure describing a generic socket address.  */
struct sockaddr {
    __SOCKADDR_COMMON(sa_); /* Common data: address family and length. */
    char sa_data[14];       /* Address data. */
};

/* From bits/socket.h */
/* Structure large enough to hold any socket address (with the historical
   exception of AF_UNIX).  */
struct sockaddr_storage {
    __SOCKADDR_COMMON(ss_); /* Address family, etc. */
    char __ss_padding[128 - sizeof(sa_family_t)];
};

/* linux/mqueue.h */
struct __kernel_mq_attr {
    long mq_flags;      /* message queue flags */
    long mq_maxmsg;     /* maximum number of messages */
    long mq_msgsize;    /* maximum message size */
    long mq_curmsgs;    /* number of messages currently queued */
    long __reserved[4]; /* ignored for input, zeroed for output */
};

/* bits/uio.h */
/* Structure for scatter/gather I/O.  */
struct iovec {
    void* iov_base; /* Pointer to data. */
    size_t iov_len; /* Length of data. */
};

struct getcpu_cache {
    unsigned long blob[128 / sizeof(long)];
};

#undef __CPU_SETSIZE
#undef __NCPUBITS

#define LINUX_DT_UNKNOWN 0
#define LINUX_DT_FIFO    1
#define LINUX_DT_CHR     2
#define LINUX_DT_DIR     4
#define LINUX_DT_BLK     6
#define LINUX_DT_REG     8
#define LINUX_DT_LNK     10
#define LINUX_DT_SOCK    12
#define LINUX_DT_WHT     14

struct linux_dirent64 {
    uint64_t d_ino;              /* Inode number */
    uint64_t d_off;              /* Offset to next linux_dirent */
    unsigned short int d_reclen; /* Length of this linux_dirent */
    unsigned char d_type;
    char d_name[]; /* File name (null-terminated) */
};

struct linux_dirent {
    unsigned long d_ino;         /* Inode number */
    unsigned long d_off;         /* Offset to next linux_dirent */
    unsigned short int d_reclen; /* Length of this linux_dirent */
    char d_name[];               /* File name (null-terminated) */
};

struct linux_dirent_tail {
    char pad;
    unsigned char d_type;
};

struct linux_file_handle {
    unsigned int handle_bytes;
    int handle_type;
    unsigned char f_handle[0];
};

#ifdef __x86_64__
typedef Elf64_auxv_t elf_auxv_t;
#else
typedef Elf64_auxv_t elf_auxv_t;
#endif

/* typedef for shim internal types */
typedef unsigned int IDTYPE;
typedef uint16_t FDTYPE;
typedef uint64_t HASHTYPE;

typedef struct atomic_int REFTYPE;

struct shim_lock {
    PAL_HANDLE lock;
    IDTYPE owner;
};

typedef struct shim_aevent {
    PAL_HANDLE event;
} AEVENTTYPE;

#define STR_SIZE 4096

struct shim_str {
    char str[STR_SIZE];
};

#define QSTR_SIZE 32

/* Use qstr for names. This has fixed size string + string object
 * if len > SHIM_QSTR_SIZE then use overflow string */
struct shim_qstr {
    HASHTYPE hash;
    size_t len;
    char name[QSTR_SIZE];
    struct shim_str* oflow;
};

/* maximum length of pipe/FIFO name (should be less than Linux sockaddr_un.sun_path = 108) */
#define PIPE_URI_SIZE 96

#endif /* _SHIM_TYPES_H_ */
