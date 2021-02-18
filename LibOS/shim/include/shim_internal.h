/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

#ifndef _SHIM_INTERNAL_H_
#define _SHIM_INTERNAL_H_

#include <stdbool.h>
#include <stdnoreturn.h>

#include "api.h"
#include "assert.h"
#include "atomic.h"
#include "shim_defs.h"
#include "shim_internal-arch.h"
#include "shim_tcb.h"
#include "shim_types.h"

void* shim_init(int argc, void* args);

/* important macros and static inline functions */

#define PAL_NATIVE_ERRNO() SHIM_TCB_GET(pal_errno)

#define INTERNAL_TID_BASE ((IDTYPE)1 << (sizeof(IDTYPE) * 8 - 1))

static inline bool is_internal_tid(unsigned int tid) {
    return tid >= INTERNAL_TID_BASE;
}

struct debug_buf {
    int start;
    int end;
    char buf[DEBUGBUF_SIZE];
};

#include "pal.h"
#include "pal_debug.h"
#include "pal_error.h"

extern int g_log_level;

#include <stdarg.h>

void debug_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void debug_puts(const char* str);
void debug_putch(int ch);
void debug_vprintf(const char* fmt, va_list ap) __attribute__((format(printf, 1, 0)));

void _log(int level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#define log_error(fmt...)    _log(PAL_LOG_ERROR, fmt)
#define log_warning(fmt...)  _log(PAL_LOG_WARNING, fmt)
#define log_debug(fmt...)    _log(PAL_LOG_DEBUG, fmt)
#define log_trace(fmt...)    _log(PAL_LOG_TRACE, fmt)

/* TODO: Replace debug() calls with log_*() at the appropriate levels, and remove this macro. */
#define debug(fmt...)        _log(PAL_LOG_WARNING, fmt)

#if 0
#define DEBUG_BREAK_ON_FAILURE() DEBUG_BREAK()
#else
#define DEBUG_BREAK_ON_FAILURE() do {} while (0)
#endif

#define BUG()                                       \
    do {                                            \
        warn("BUG() " __FILE__ ":%d\n", __LINE__);  \
        DEBUG_BREAK_ON_FAILURE();                   \
        die_or_inf_loop();                          \
    } while (0)

#define DEBUG_HERE()                                         \
    do {                                                     \
        debug("%s (" __FILE__ ":%d)\n", __func__, __LINE__); \
    } while (0)

/*!
 * \brief High-level syscall emulation entrypoint.
 *
 * \param context CPU context at syscall entry.
 *
 * Emulates the syscall given the entry \p context.
 */
noreturn void shim_emulate_syscall(PAL_CONTEXT* context);
/*!
 * \brief Restore the CPU context.
 *
 * \param context CPU context to restore.
 *
 * This function restores the given \p context. It is only called on returning from a syscall, so
 * it does not need to be reentrant (there is no such thing as nested syscalls), but it cannot
 * assume that the CPU context is the same as at the entry to the syscall (e.g. sigreturn, or signal
 * handling may change it).
 */
noreturn void return_from_syscall(PAL_CONTEXT* context);
/*!
 * \brief Restore the context after clone/fork.
 *
 * \param context LibOS context to restore.
 *
 * Restores LibOS \p context after a successful clone or fork.
 */
noreturn void restore_child_context_after_clone(struct shim_context* context);
/*!
 * \brief Creates a signal frame
 *
 * \param context CPU context
 * \param siginfo signal to be delivered
 * \param handler pointer to the user app signal handler
 * \param restorer pointer to the restorer function
 * \param should_use_altstack `true` - use alternative stack if possible, `false` - never use it
 * \param old_mask old signal mask (to be stored in the signal frame)
 *
 * Creates a signal frame on the user app stack (either normal or alternative stack, depending on
 * \p use_altstack and the currently used stack). Arranges \p context so that restoring it jumps
 * to \p handler with appropriate arguments and returning from \p handler will jump to \p restorer,
 * (which usually just calls `sigreturn` syscall). On most (all?) architectures old \p context,
 * \p siginfo and \p old_mask are saved into the signal frame.
 */
void prepare_sigframe(PAL_CONTEXT* context, siginfo_t* siginfo, void* handler, void* restorer,
                      bool should_use_altstack, __sigset_t* old_mask);
/*!
 * \brief Restart a syscall
 *
 * \param context CPU context
 * \param syscall_nr syscall number
 *
 * Arranges \p context so that upon return to it redoes the \p syscall_nr syscall.
 */
void restart_syscall(PAL_CONTEXT* context, uint64_t syscall_nr);
/*!
 * \brief Restores a sigreturn context
 *
 * \param context original CPU context
 * \param[out] new_mask new signal mask
 *
 * Restores CPU context in an architecture-specific way. On entry to this function \p context holds
 * initial CPU context and this function extracts signal frame (generated by `prepare_sigframe`)
 * and restores it into \p context. The signal mask extracted from the signal frame is written into
 * \p new_mask.
 */
void restore_sigreturn_context(PAL_CONTEXT* context, __sigset_t* new_mask);
/*!
 * \brief Emulate a syscall
 *
 * \param context CPU context
 *
 * If the current instruction pointer in \p context points to a syscall instruction, arrange
 * \p context so that the syscall is emulated.
 * Returns `true` if it was a syscall instruction (hence a syscall will be emulated).
 * Note that this function merely changes context, so the actual emulation is done upon returning
 * to that context.
 * Used e.g. in Linux-SGX Pal to handle `syscall` instruction.
 */
bool maybe_emulate_syscall(PAL_CONTEXT* context);
/*!
 * \brief Handle a signal
 *
 * \param context CPU context
 * \param old_mask_ptr pointer to the old signal mask
 *
 * If there is a signal to be handled, this function arranges its delivery using `prepare_sigframe`.
 * If \p old_mask_ptr is not `NULL`, it is stored into the signal frame, otherwise the current
 * signal mask is used.
 * Returns `true` if a not-ignored signal was handled (hence \p context was changed), `false`
 * otherwise.
 *
 * XXX: Signals are delivered only during transition from LibOS to the user app, so a situation is
 * possible when a signal is queued, but is not delivered for an arbitrary amount of time. There are
 * two distinct situations when this can happen:
 * 1) A blocking host-level syscall was issued and the signal arrived at any point before it and
 *    after the thread entered LibOS code. In such case the host syscall can block indefinitely.
 * 2) The signal arrived in the middle of or after `handle_signal`. In such case delivery of this
 *    signal is delayed until the next syscall is issued or another signal arrives.
 */
bool handle_signal(PAL_CONTEXT* context, __sigset_t* old_mask_ptr);

long convert_pal_errno(long err);

#define PAL_ERRNO() convert_pal_errno(PAL_NATIVE_ERRNO())

void debug_print_syscall_before(int sysno, ...);
void debug_print_syscall_after(int sysno, ...);

#define PAL_CB(member) (pal_control.member)

/*
 * These events have counting semaphore semantics:
 * - `set_event(e, n)` increases value of the semaphore by `n`,
 * - `wait_event(e)` decreases value by 1 (blocking if it's 0),
 * - `clear_event(e)` decreases value to 0, without blocking - this operation is not atomic.
 * Note that using `clear_event` probably requires external locking to avoid races.
 */
static inline int create_event(AEVENTTYPE* e) {
    e->event = DkStreamOpen(URI_PREFIX_PIPE, PAL_ACCESS_RDWR, 0, 0, 0);
    if (!e->event) {
        return -PAL_ERRNO();
    }
    return 0;
}

static inline PAL_HANDLE event_handle(AEVENTTYPE* e) {
    return e->event;
}

static inline void destroy_event(AEVENTTYPE* e) {
    if (e->event) {
        DkObjectClose(e->event);
        e->event = NULL;
    }
}

static inline int set_event(AEVENTTYPE* e, size_t n) {
    /* TODO: this should be changed into an assert, once we make sure it does not happen (old
     * version handled it). */
    if (!e->event) {
        return -EINVAL;
    }

    char bytes[n];
    memset(bytes, '\0', n);
    while (n > 0) {
        PAL_NUM ret = DkStreamWrite(e->event, 0, n, bytes, NULL);
        if (ret == PAL_STREAM_ERROR) {
            int err = PAL_ERRNO();
            if (err == EINTR || err == EAGAIN || err == EWOULDBLOCK) {
                continue;
            }
            return -err;
        }
        n -= ret;
    }

    return 0;
}

static inline int wait_event(AEVENTTYPE* e) {
    /* TODO: this should be changed into an assert, once we make sure it does not happen (old
     * version handled it). */
    if (!e->event) {
        return -EINVAL;
    }

    int err = 0;
    do {
        char byte;
        PAL_NUM ret = DkStreamRead(e->event, 0, 1, &byte, NULL, 0);
        err = ret == PAL_STREAM_ERROR ? PAL_ERRNO() : 0;
    } while (err == EINTR || err == EAGAIN || err == EWOULDBLOCK);

    return -err;
}

static inline int clear_event(AEVENTTYPE* e) {
    /* TODO: this should be changed into an assert, once we make sure it does not happen (old
     * version handled it). */
    if (!e->event) {
        return -EINVAL;
    }

    while (1) {
        PAL_HANDLE handle = e->event;
        PAL_FLG ievent = PAL_WAIT_READ;
        PAL_FLG revent = 0;

        shim_get_tcb()->pal_errno = PAL_ERROR_SUCCESS;
        PAL_BOL ret = DkStreamsWaitEvents(1, &handle, &ievent, &revent, /*timeout=*/0);
        if (!ret) {
            int err = PAL_ERRNO();
            if (err == EINTR) {
                continue;
            } else if (!err || err == EAGAIN || err == EWOULDBLOCK) {
                break;
            }
            return -err;
        }

        /* Even if `revent` has `PAL_WAIT_ERROR` marked, let `DkSitreamRead()` report the error
         * below. */
        assert(revent);

        char bytes[100];
        PAL_NUM n = DkStreamRead(e->event, 0, sizeof(bytes), bytes, NULL, 0);
        if (n == PAL_STREAM_ERROR) {
            int err = PAL_ERRNO();
            if (err == EINTR) {
                continue;
            } else if (err == EAGAIN || err == EWOULDBLOCK) {
                /* This should not happen, since we polled above  ... */
                break;
            }
            return -err;
        }
    }

    return 0;
}

/* reference counter APIs */
#define REF_GET(ref)        __atomic_load_n(&(ref).counter, __ATOMIC_SEQ_CST)
#define REF_SET(ref, count) __atomic_store_n(&(ref).counter, count, __ATOMIC_SEQ_CST);

static inline int __ref_inc(REFTYPE* ref) {
    int64_t _c;
    do {
        _c = __atomic_load_n(&ref->counter, __ATOMIC_SEQ_CST);
        assert(_c >= 0);
    } while (!__atomic_compare_exchange_n(&ref->counter, &_c, _c + 1, /*weak=*/false,
                                          __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));
    return _c + 1;
}

#define REF_INC(ref) __ref_inc(&(ref))

static inline int __ref_dec(REFTYPE* ref) {
    int64_t _c;
    do {
        _c = __atomic_load_n(&ref->counter, __ATOMIC_SEQ_CST);
        if (!_c) {
            debug("Fail: Trying to drop reference count below 0\n");
            BUG();
            return 0;
        }
    } while (!__atomic_compare_exchange_n(&ref->counter, &_c, _c - 1, /*weak=*/false,
                                          __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));
    return _c - 1;
}

#define REF_DEC(ref) __ref_dec(&(ref))

#ifndef __alloca
#define __alloca __builtin_alloca
#endif

extern size_t g_pal_alloc_align;
#define ALLOC_ALIGNMENT         g_pal_alloc_align
#define IS_ALLOC_ALIGNED(x)     IS_ALIGNED_POW2(x, g_pal_alloc_align)
#define IS_ALLOC_ALIGNED_PTR(x) IS_ALIGNED_PTR_POW2(x, g_pal_alloc_align)
#define ALLOC_ALIGN_DOWN(x)     ALIGN_DOWN_POW2(x, g_pal_alloc_align)
#define ALLOC_ALIGN_UP(x)       ALIGN_UP_POW2(x, g_pal_alloc_align)
#define ALLOC_ALIGN_DOWN_PTR(x) ALIGN_DOWN_PTR_POW2(x, g_pal_alloc_align)
#define ALLOC_ALIGN_UP_PTR(x)   ALIGN_UP_PTR_POW2(x, g_pal_alloc_align)

void* __system_malloc(size_t size);
void __system_free(void* addr, size_t size);

#define system_malloc __system_malloc
#define system_free   __system_free

extern void* migrated_memory_start;
extern void* migrated_memory_end;

static inline bool memory_migrated(void* mem) {
    return mem >= migrated_memory_start && mem < migrated_memory_end;
}

extern void* __load_address;
extern void* __load_address_end;

extern const char** migrated_envp;

struct shim_handle;
int init_brk_from_executable(struct shim_handle* exec);
int init_brk_region(void* brk_region, size_t data_segment_size);
void reset_brk(void);
int init_internal_map(void);
int init_loader(void);
int init_rlimit(void);

bool test_user_memory(void* addr, size_t size, bool write);
bool test_user_string(const char* addr);

uint64_t get_rlimit_cur(int resource);
void set_rlimit_cur(int resource, uint64_t rlim);

int object_wait_with_retry(PAL_HANDLE handle);

void _update_epolls(struct shim_handle* handle);
void delete_from_epoll_handles(struct shim_handle* handle);

void* allocate_stack(size_t size, size_t protect_size, bool user);
int init_stack(const char** argv, const char** envp, const char*** out_argp, elf_auxv_t** out_auxv);

#endif /* _SHIM_INTERNAL_H_ */
