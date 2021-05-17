/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

#ifndef PAL_LINUX_H
#define PAL_LINUX_H

#include <asm/fcntl.h>
#include <asm/stat.h>
#include <linux/mman.h>
#include <sigset.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "list.h"
#include "pal.h"
#include "pal_defs.h"
#include "pal_internal.h"
#include "pal_linux_defs.h"
#include "pal_linux_error.h"
#include "stat.h"
#include "sysdep-arch.h"
#include "sysdeps/generic/ldsodefs.h"

#define IS_ERR_P INTERNAL_SYSCALL_ERROR_P
#define ERRNO_P  INTERNAL_SYSCALL_ERRNO_P

struct timespec;
struct timeval;

extern struct pal_linux_state {
    PAL_NUM         parent_process_id;
    PAL_NUM         process_id;

#ifdef DEBUG
    bool            in_gdb;
#endif

    const char**    host_environ;

    /* credentails */
    unsigned int    pid;
    unsigned int    uid, gid;

    /* currently enabled signals */
    __sigset_t      set_signals;
    __sigset_t      blocked_signals;

    unsigned long   memory_quota;

    long int (*vdso_clock_gettime)(long int clk, struct timespec* tp);
} g_linux_state;

#ifdef INLINE_SYSCALL
#ifdef __i386__
#define ARCH_MMAP(addr, len, prot, flags, fd, offset) \
    ({                                                \
        struct mmap_arg_struct {                      \
            unsigned long addr;                       \
            unsigned long len;                        \
            unsigned long prot;                       \
            unsigned long flags;                      \
            unsigned long fd;                         \
            unsigned long offset;                     \
        } args = {                                    \
            .addr   = (unsigned long)(addr),          \
            .len    = (unsigned long)(len),           \
            .prot   = (unsigned long)(prot),          \
            .flags  = (unsigned long)(flags),         \
            .fd     = (unsigned long)(fd),            \
            .offset = (unsigned long)(offset),        \
        };                                            \
        INLINE_SYSCALL(mmap, 1, &args);               \
    })
#else
#define ARCH_MMAP(addr, len, prot, flags, fd, offset) \
    INLINE_SYSCALL(mmap, 6, addr, len, prot, flags, fd, offset)
#endif
#else
#error "INLINE_SYSCALL not supported"
#endif

#ifndef SIGCHLD
#define SIGCHLD 17
#endif

int clone(int (*__fn)(void* __arg), void* __child_stack, int __flags, const void* __arg, ...);

/* PAL main function */
noreturn void pal_linux_main(void* initial_rsp, void* fini_callback);

extern const size_t g_page_size;
extern char* g_pal_internal_mem_addr;
extern size_t g_pal_internal_mem_size;

extern uintptr_t g_vdso_start;
extern uintptr_t g_vdso_end;
bool is_in_vdso(uintptr_t addr);
/* Parse "/proc/self/maps" and return address ranges for "vdso" and "vvar". */
int get_vdso_and_vvar_ranges(uintptr_t* vdso_start, uintptr_t* vdso_end, uintptr_t* vvar_start,
                             uintptr_t* vvar_end);

struct link_map;
void setup_pal_map(struct link_map* map);
void setup_vdso_map(ElfW(Addr) addr);

/* set/unset CLOEXEC flags of all fds in a handle */
int handle_set_cloexec(PAL_HANDLE handle, bool enable);

/* serialize/deserialize a handle into/from a malloc'ed buffer */
int handle_serialize(PAL_HANDLE handle, void** data);
int handle_deserialize(PAL_HANDLE* handle, const void* data, size_t size);

#define ACCESS_R 4
#define ACCESS_W 2
#define ACCESS_X 1

bool stataccess(struct stat* stats, int acc);

void init_child_process(int parent_pipe_fd, PAL_HANDLE* parent, char** manifest_out);

void cpuid(unsigned int leaf, unsigned int subleaf, unsigned int words[]);
int block_async_signals(bool block);
void signal_setup(void);

extern char __text_start, __text_end, __data_start, __data_end;
#define TEXT_START ((void*)(&__text_start))
#define TEXT_END   ((void*)(&__text_end))
#define DATA_START ((void*)(&__data_start))
#define DATA_END   ((void*)(&__data_end))

#define ADDR_IN_PAL_OR_VDSO(addr) \
        (((void*)(addr) > TEXT_START && (void*)(addr) < TEXT_END) || is_in_vdso(addr))

typedef struct pal_tcb_linux {
    PAL_TCB common;
    struct {
        /* private to Linux PAL */
        PAL_HANDLE handle;
        void*      alt_stack;
        int        (*callback)(void*);
        void*      param;
    };
} PAL_TCB_LINUX;

int pal_thread_init(void* tcbptr);

static inline void pal_tcb_linux_init(PAL_TCB_LINUX* tcb, PAL_HANDLE handle, void* alt_stack,
                                      int (*callback)(void*), void* param) {
    tcb->common.self = &tcb->common;
    tcb->handle      = handle;
    tcb->alt_stack   = alt_stack; // Stack bottom
    tcb->callback    = callback;
    tcb->param       = param;
}

static inline PAL_TCB_LINUX* get_tcb_linux(void) {
    return (PAL_TCB_LINUX*)pal_get_tcb();
}

__attribute__((__optimize__("-fno-stack-protector")))
static inline void pal_tcb_set_stack_canary(PAL_TCB* tcb, uint64_t canary) {
    ((char*)&canary)[0] = 0; /* prevent C-string-based stack leaks from exposing the cookie */
    pal_tcb_arch_set_stack_canary(tcb, canary);
}
struct linux_dirent64 {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[];
};

#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK     10
#define DT_SOCK    12
#define DT_WHT     14

#define DIRBUF_SIZE 1024

#endif /* PAL_LINUX_H */
