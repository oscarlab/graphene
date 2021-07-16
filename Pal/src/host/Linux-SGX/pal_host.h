/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * This file contains definition of PAL host ABI.
 */

#ifndef PAL_HOST_H
#define PAL_HOST_H

#ifndef IN_PAL
#error "cannot be included outside PAL"
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atomic.h"
#include "list.h"
#include "spinlock.h"

void* malloc_untrusted(size_t size);
void free_untrusted(void* mem);

DEFINE_LIST(pal_handle_thread);
struct pal_handle_thread {
    PAL_HDR reserved;
    PAL_IDX tid;
    PAL_PTR tcs;
    LIST_TYPE(pal_handle_thread) list;
    void* param;
};

typedef struct {
    char str[PIPE_NAME_MAX];
} PAL_PIPE_NAME;

/* RPC streams are encrypted with 256-bit AES keys */
typedef uint8_t PAL_SESSION_KEY[32];

typedef struct pal_handle {
    /*
     * Here we define the internal structure of PAL_HANDLE.
     * user has no access to the content inside these handles.
     */

    PAL_HDR hdr;
    union {
        struct {
            PAL_IDX fds[MAX_FDS];
        } generic;

        struct {
            PAL_IDX fd;
            PAL_STR realpath;
            PAL_NUM total;
            /* below fields are used only for trusted files */
            PAL_PTR chunk_hashes; /* array of hashes of file chunks */
            PAL_PTR umem;         /* valid only when chunk_hashes != NULL */
            PAL_BOL seekable;     /* regular files are seekable, FIFO pipes are not */
        } file;

        struct {
            PAL_IDX fd;
            PAL_PIPE_NAME name;
            PAL_BOL nonblocking;
            PAL_BOL is_server;
            PAL_SESSION_KEY session_key;
            PAL_NUM handshake_done;
            void* ssl_ctx;
        } pipe;

        struct {
            PAL_IDX fds[MAX_FDS];
            PAL_BOL nonblocking;
        } pipeprv;

        struct {
            PAL_IDX fd;
            /* TODO: add other flags in future, if needed (e.g., semaphore) */
            PAL_BOL nonblocking;
        } eventfd;

        struct {
            PAL_IDX fd;
            PAL_BOL nonblocking;
        } dev;

        struct {
            PAL_IDX fd;
            PAL_STR realpath;
            PAL_PTR buf;
            PAL_PTR ptr;
            PAL_PTR end;
            PAL_BOL endofstream;
        } dir;

        struct {
            PAL_IDX fd;
            PAL_PTR bind;
            PAL_PTR conn;
            PAL_BOL nonblocking;
            PAL_NUM linger;
            PAL_NUM receivebuf;
            PAL_NUM sendbuf;
            PAL_NUM receivetimeout;
            PAL_NUM sendtimeout;
            PAL_BOL tcp_cork;
            PAL_BOL tcp_keepalive;
            PAL_BOL tcp_nodelay;
            PAL_BOL ipv6_v6only;
        } sock;

        struct {
            PAL_IDX stream;
            PAL_BOL nonblocking;
            PAL_BOL is_server;
            PAL_SESSION_KEY session_key;
            void* ssl_ctx;
        } process;

        struct pal_handle_thread thread;

        struct {
            /* Guards accesses to the rest of the fields.
             * We need to be able to set `signaled` and `signaled_untrusted` atomically, which is
             * impossible without a lock. They are essentialy the same field, but we need two
             * separate copies, because we need to guard against malicious host modifications yet
             * still be able to call futex on it. */
            spinlock_t lock;
            /* Current number of waiters - used solely as an optimization. `uint32_t` because futex
             * syscall does not allow for more than `INT_MAX` waiters anyway. */
            uint32_t waiters_cnt;
            bool signaled;
            bool auto_clear;
            /* Access to the *content* of this field should be atomic, because it's used as futex
             * word on the untrusted host. */
            uint32_t* signaled_untrusted;
        } event;
    };
}* PAL_HANDLE;

#define RFD(n)   (1 << (MAX_FDS * 0 + (n)))
#define WFD(n)   (1 << (MAX_FDS * 1 + (n)))
#define ERROR(n) (1 << (MAX_FDS * 2 + (n)))

#define HANDLE_TYPE(handle) ((handle)->hdr.type)

#endif /* PAL_HOST_H */
