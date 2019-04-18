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
 * db_signal.c
 *
 * This file contains APIs to set up handlers of exceptions issued by the
 * host, and the methods to pass the exceptions to the upcalls.
 */

#include "pal_defs.h"
#include "pal_linux_defs.h"
#include "pal.h"
#include "pal_internal.h"
#include "pal_linux.h"
#include "pal_error.h"
#include "pal_security.h"
#include "api.h"
#include "ecall_types.h"

#include <atomic.h>
#include <sigset.h>
#include <linux/signal.h>
#include <ucontext.h>

typedef struct exception_event {
    PAL_IDX             event_num;
    PAL_CONTEXT *       context;
} PAL_EVENT;

static void _DkGenericEventTrigger (PAL_IDX event_num, PAL_EVENT_HANDLER upcall,
                                    PAL_NUM arg, PAL_CONTEXT * context)
{
    struct exception_event event;

    event.event_num = event_num;
    event.context = context;

    (*upcall) ((PAL_PTR) &event, arg, context);
}

static bool
_DkGenericSignalHandle (int event_num, PAL_NUM arg, PAL_CONTEXT * context)
{
    PAL_EVENT_HANDLER upcall = _DkGetExceptionHandler(event_num);

    if (upcall) {
        _DkGenericEventTrigger(event_num, upcall, arg, context);
        return true;
    }

    return false;
}

#define ADDR_IN_PAL(addr)  \
        ((void*)(addr) > TEXT_START && (void*)(addr) < TEXT_END)

/*
 * Restore an sgx_cpu_context_t as generated by .Lhandle_exception. Execution will
 * continue as specified by the rip in the context.
 */
noreturn static void restore_sgx_context(sgx_cpu_context_t* ctx,
                                         PAL_XREGS_STATE* xregs_state) {
    SGX_DBG(DBG_E, "ctx %p rsp 0x%08lx &rsp: %p rip 0x%08lx +0x%08lx &rip: %p\n",
            ctx, ctx->rsp, &ctx->rsp, ctx->rip, ctx->rip - (uintptr_t) TEXT_START, &ctx->rip);

    if (xregs_state == NULL)
        xregs_state = (PAL_XREGS_STATE*)SYNTHETIC_STATE;
    _restore_sgx_context(ctx, xregs_state);
}

noreturn static void restore_pal_context(sgx_cpu_context_t* uc, PAL_CONTEXT* ctx) {
    uc->rax = ctx->rax;
    uc->rbx = ctx->rbx;
    uc->rcx = ctx->rcx;
    uc->rdx = ctx->rdx;
    uc->rsp = ctx->rsp;
    uc->rbp = ctx->rbp;
    uc->rsi = ctx->rsi;
    uc->rdi = ctx->rdi;
    uc->r8  = ctx->r8;
    uc->r9  = ctx->r9;
    uc->r10 = ctx->r10;
    uc->r11 = ctx->r11;
    uc->r12 = ctx->r12;
    uc->r13 = ctx->r13;
    uc->r14 = ctx->r14;
    uc->r15 = ctx->r15;
    uc->rflags = ctx->efl;
    uc->rip = ctx->rip;

    restore_sgx_context(uc, ctx->fpregs);
}

static void save_pal_context(PAL_CONTEXT* ctx, sgx_cpu_context_t* uc,
                             PAL_XREGS_STATE* xregs_state) {
    memset(ctx, 0, sizeof(*ctx));

    ctx->rax = uc->rax;
    ctx->rbx = uc->rbx;
    ctx->rcx = uc->rcx;
    ctx->rdx = uc->rdx;
    ctx->rsp = uc->rsp;
    ctx->rbp = uc->rbp;
    ctx->rsi = uc->rsi;
    ctx->rdi = uc->rdi;
    ctx->r8  = uc->r8;
    ctx->r9  = uc->r9;
    ctx->r10 = uc->r10;
    ctx->r11 = uc->r11;
    ctx->r12 = uc->r12;
    ctx->r13 = uc->r13;
    ctx->r14 = uc->r14;
    ctx->r15 = uc->r15;
    ctx->efl = uc->rflags;
    ctx->rip = uc->rip;
    union pal_csgsfs csgsfs = {
        .cs = 0x33, // __USER_CS(5) | 0(GDT) | 3(RPL)
        .fs = 0,
        .gs = 0,
        .ss = 0x2b, // __USER_DS(6) | 0(GDT) | 3(RPL)
    };
    ctx->csgsfs = csgsfs.csgsfs;

    ctx->fpregs = xregs_state;
    PAL_FPX_SW_BYTES * fpx_sw = &xregs_state->fpstate.sw_reserved;
    fpx_sw->magic1 = PAL_FP_XSTATE_MAGIC1;
    fpx_sw->extended_size = xsave_size;
    fpx_sw->xfeatures = xsave_features;
    fpx_sw->xstate_size = xsave_size + PAL_FP_XSTATE_MAGIC2_SIZE;
    memset(fpx_sw->padding, 0, sizeof(fpx_sw->padding));
    *(__typeof__(PAL_FP_XSTATE_MAGIC2)*)((void*)xregs_state + xsave_size) =
        PAL_FP_XSTATE_MAGIC2;
}

/*
 * return value:
 *  true:  #UD is handled.
 *         the execution can be continued without propagating #UD.
 *  false: #UD is not handled.
 *         the exception needs to be raised up to LibOS or user application.
 */
static bool handle_ud(sgx_cpu_context_t * uc)
{
    uint8_t * instr = (uint8_t *) uc->rip;
    if (instr[0] == 0xcc) { /* skip int 3 */
        uc->rip++;
        return true;
    } else if (instr[0] == 0x0f && instr[1] == 0xa2) {
        /* cpuid */
        unsigned int values[4];
        if (!_DkCpuIdRetrieve(uc->rax & 0xffffffff,
                              uc->rcx & 0xffffffff, values)) {
            uc->rip += 2;
            uc->rax = values[0];
            uc->rbx = values[1];
            uc->rcx = values[2];
            uc->rdx = values[3];
            return true;
        }
    } else if (instr[0] == 0x0f && instr[1] == 0x31) {
        /* rdtsc */
        uc->rip += 2;
        uc->rdx = 0;
        uc->rax = 0;
        return true;
    } else if (instr[0] == 0x0f && instr[1] == 0x05) {
        /* syscall: LibOS may know how to handle this */
        return false;
    }
    SGX_DBG(DBG_E, "Unknown or illegal instruction at RIP 0x%016lx\n", uc->rip);
    return false;
}

void _DkExceptionHandler (unsigned int exit_info, sgx_cpu_context_t * uc)
{
    PAL_XREGS_STATE * xregs_state = (PAL_XREGS_STATE *)(uc + 1);
    assert((((uintptr_t)xregs_state) % PAL_XSTATE_ALIGN) == 0);

    SGX_DBG(DBG_E, "exit_info 0x%08x\n", exit_info);
    union {
        sgx_arch_exit_info_t info;
        unsigned int intval;
    } ei = { .intval = exit_info };

    int event_num;

    if (!ei.info.valid) {
        event_num = exit_info;
    } else {
        switch (ei.info.vector) {
        case SGX_EXCEPTION_VECTOR_BR:
            event_num = PAL_EVENT_NUM_BOUND;
            break;
        case SGX_EXCEPTION_VECTOR_UD:
            if (handle_ud(uc)) {
                restore_sgx_context(uc, xregs_state);
                /* NOTREACHED */
            }
            event_num = PAL_EVENT_ILLEGAL;
            break;
        case SGX_EXCEPTION_VECTOR_DE:
        case SGX_EXCEPTION_VECTOR_MF:
        case SGX_EXCEPTION_VECTOR_XM:
            event_num = PAL_EVENT_ARITHMETIC_ERROR;
            break;
        case SGX_EXCEPTION_VECTOR_AC:
            event_num = PAL_EVENT_MEMFAULT;
            break;
        case SGX_EXCEPTION_VECTOR_DB:
        case SGX_EXCEPTION_VECTOR_BP:
        default:
            restore_sgx_context(uc, xregs_state);
            return;
        }
    }

    if (ADDR_IN_PAL(uc->rip) &&
        /* event isn't asynchronous */
        (event_num != PAL_EVENT_QUIT &&
         event_num != PAL_EVENT_SUSPEND &&
         event_num != PAL_EVENT_RESUME)) {
        printf("*** An unexpected AEX vector occurred inside PAL. "
               "Exiting the thread. *** \n"
               "(vector = 0x%x, type = 0x%x valid = %d, RIP = +0x%08lx)\n"
               "rax: 0x%08lx rcx: 0x%08lx rdx: 0x%08lx rbx: 0x%08lx\n"
               "rsp: 0x%08lx rbp: 0x%08lx rsi: 0x%08lx rdi: 0x%08lx\n"
               "r8 : 0x%08lx r9 : 0x%08lx r10: 0x%08lx r11: 0x%08lx\n"
               "r12: 0x%08lx r13: 0x%08lx r14: 0x%08lx r15: 0x%08lx\n"
               "rflags: 0x%08lx rip: 0x%08lx\n",
               ei.info.vector, ei.info.exit_type, ei.info.valid,
               uc->rip - (uintptr_t) TEXT_START,
               uc->rax, uc->rcx, uc->rdx, uc->rbx,
               uc->rsp, uc->rbp, uc->rsi, uc->rdi,
               uc->r8, uc->r9, uc->r10, uc->r11,
               uc->r12, uc->r13, uc->r14, uc->r15,
               uc->rflags, uc->rip);
#ifdef DEBUG
        printf("pausing for debug\n");
        while (true)
            __asm__ volatile("pause");
#endif
        _DkThreadExit(/*clear_child_tid=*/NULL);
    }

    PAL_CONTEXT ctx;
    save_pal_context(&ctx, uc, xregs_state);

    /* TODO: save EXINFO in MISC regsion and populate those */
    ctx.err = 0;
    ctx.trapno = ei.info.valid? ei.info.vector: event_num;
    ctx.oldmask = 0;
    ctx.cr2 = 0;

    PAL_NUM arg = 0;
    switch (event_num) {
    case PAL_EVENT_ILLEGAL:
        arg = uc->rip;
        break;
    case PAL_EVENT_MEMFAULT:
        /* TODO
         * SGX1 doesn't provide fault address.
         * SGX2 gives providing page. (lower address bits are masked)
         */
        break;
    default:
        /* nothing */
        break;
    }
    _DkGenericSignalHandle(event_num, arg, &ctx);
    restore_pal_context(uc, &ctx);
}

void _DkRaiseFailure (int error)
{
    PAL_EVENT_HANDLER upcall = _DkGetExceptionHandler(PAL_EVENT_FAILURE);

    if (!upcall)
        return;

    PAL_EVENT event;
    event.event_num = PAL_EVENT_FAILURE;
    event.context   = NULL;

    (*upcall) ((PAL_PTR) &event, error, NULL);
}

void _DkExceptionReturn (void * event)
{
    PAL_EVENT * e = event;
    PAL_CONTEXT * ctx = e->context;

    if (!ctx) {
        return;
    }

    sgx_cpu_context_t uc;
    restore_pal_context(&uc, ctx);
}

noreturn void _DkHandleExternalEvent(PAL_NUM event, sgx_cpu_context_t* uc,
                                     PAL_XREGS_STATE* xregs_state)
{
    assert(event);
    assert((((uintptr_t)xregs_state) % PAL_XSTATE_ALIGN) == 0);
    assert((PAL_XREGS_STATE*) (uc + 1) == xregs_state);

    PAL_CONTEXT ctx;
    save_pal_context(&ctx, uc, xregs_state);
    ctx.err = 0;
    ctx.trapno = event; /* XXX TODO: what kind of value should be returned in
                         * trapno. This is very implementation specific
                         */
    ctx.oldmask = 0;
    ctx.cr2 = 0;

    if (!_DkGenericSignalHandle(event, 0, &ctx)
        && event != PAL_EVENT_RESUME)
        _DkThreadExit(/*clear_child_tid=*/NULL);

    restore_sgx_context(uc, ctx.fpregs);
}
