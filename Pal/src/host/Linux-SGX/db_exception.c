/* Copyright (C) 2014 Stony Brook University
                 2020 Intel Labs
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
 * db_exception.c
 *
 * This file contains APIs to set up signal handlers.
 */

#include "api.h"
#include "cpu.h"
#include "ecall_types.h"
#include "pal.h"
#include "pal_defs.h"
#include "pal_defs.h"
#include "pal_error.h"
#include "pal_internal.h"
#include "pal_linux.h"
#include "pal_linux_defs.h"
#include "pal_linux_defs.h"
#include "pal_security.h"

#include <linux/signal.h>
#include <sigset.h>
#include <ucontext.h>

#define ADDR_IN_PAL(addr) ((void*)(addr) > TEXT_START && (void*)(addr) < TEXT_END)

/* Restore an sgx_cpu_context_t as generated by .Lhandle_exception. Execution will
 * continue as specified by the rip in the context. */
noreturn static void restore_sgx_context(sgx_cpu_context_t* uc,
                                         PAL_XREGS_STATE* xregs_state) {
    if (xregs_state == NULL)
        xregs_state = (PAL_XREGS_STATE*)xsave_reset_state;
    _restore_sgx_context(uc, xregs_state);
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

    assert(xregs_state);
    ctx->fpregs = xregs_state;

    /* Emulate format for fp registers Linux sets up as signal frame.
     * https://elixir.bootlin.com/linux/v5.4.13/source/arch/x86/kernel/fpu/signal.c#L86
     * https://elixir.bootlin.com/linux/v5.4.13/source/arch/x86/kernel/fpu/signal.c#L459
     */
    PAL_FPX_SW_BYTES* fpx_sw = &xregs_state->fpstate.sw_reserved;
    fpx_sw->magic1 = PAL_FP_XSTATE_MAGIC1;
    fpx_sw->extended_size = xsave_size;
    fpx_sw->xfeatures = xsave_features;
    memset(fpx_sw->padding, 0, sizeof(fpx_sw->padding));
    if (xsave_enabled) {
        fpx_sw->xstate_size = xsave_size + PAL_FP_XSTATE_MAGIC2_SIZE;
        *(__typeof__(PAL_FP_XSTATE_MAGIC2)*)((void*)xregs_state + xsave_size) =
            PAL_FP_XSTATE_MAGIC2;
    } else {
        fpx_sw->xstate_size = xsave_size;
    }
}

/* return value: true if #UD is handled and execution can be continued without propagating #UD;
 *               false if #UD is not handled and exception needs to be raised up to LibOS/app */
static bool handle_ud(sgx_cpu_context_t* uc) {
    uint8_t* instr = (uint8_t*)uc->rip;
    if (instr[0] == 0x0f && instr[1] == 0xa2) {
        /* cpuid */
        unsigned int values[4];
        if (!_DkCpuIdRetrieve(uc->rax & 0xffffffff, uc->rcx & 0xffffffff, values)) {
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

/* perform exception handling inside the enclave */
void _DkExceptionHandler(unsigned int exit_info, sgx_cpu_context_t* uc,
                         PAL_XREGS_STATE* xregs_state) {
    assert(IS_ALIGNED_PTR(xregs_state, PAL_XSTATE_ALIGN));

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
            /* NOTREACHED */
        }
    }

    if (ADDR_IN_PAL(uc->rip) &&
        /* event isn't asynchronous (i.e., synchronous exception) */
        event_num != PAL_EVENT_QUIT &&
        event_num != PAL_EVENT_SUSPEND &&
        event_num != PAL_EVENT_RESUME) {
        printf("*** Unexpected AEX vector occurred inside PAL! ***\n"
               "(vector = 0x%x, type = 0x%x valid = %d, RIP = +0x%08lx)\n"
               "rax: 0x%08lx rcx: 0x%08lx rdx: 0x%08lx rbx: 0x%08lx\n"
               "rsp: 0x%08lx rbp: 0x%08lx rsi: 0x%08lx rdi: 0x%08lx\n"
               "r8 : 0x%08lx r9 : 0x%08lx r10: 0x%08lx r11: 0x%08lx\n"
               "r12: 0x%08lx r13: 0x%08lx r14: 0x%08lx r15: 0x%08lx\n"
               "rflags: 0x%08lx rip: 0x%08lx\n",
               ei.info.vector, ei.info.exit_type, ei.info.valid,
               uc->rip - (uintptr_t)TEXT_START,
               uc->rax, uc->rcx, uc->rdx, uc->rbx,
               uc->rsp, uc->rbp, uc->rsi, uc->rdi,
               uc->r8, uc->r9, uc->r10, uc->r11,
               uc->r12, uc->r13, uc->r14, uc->r15,
               uc->rflags, uc->rip);

        _DkProcessExit(1);
    }

    PAL_CONTEXT ctx;
    save_pal_context(&ctx, uc, xregs_state);

    /* TODO: save EXINFO from MISC region and populate below fields */
    ctx.err = 0;
    ctx.trapno = ei.info.valid ? ei.info.vector : event_num;
    ctx.oldmask = 0;
    ctx.cr2 = 0;

    PAL_NUM arg = 0;
    switch (event_num) {
    case PAL_EVENT_ILLEGAL:
        arg = uc->rip;
        break;
    case PAL_EVENT_MEMFAULT:
        /* TODO: SGX1 doesn't provide fault address but SGX2 does (with lower bits masked) */
        break;
    default:
        /* nothing */
        break;
    }

    PAL_EVENT_HANDLER upcall = _DkGetExceptionHandler(event_num);
    if (upcall) {
        (*upcall)(/*event=*/NULL, arg, &ctx);
    }

    restore_pal_context(uc, &ctx);
}

void _DkRaiseFailure(int error) {
    PAL_EVENT_HANDLER upcall = _DkGetExceptionHandler(PAL_EVENT_FAILURE);
    if (upcall) {
        (*upcall)(/*event=*/NULL, error, /*context=*/NULL);
    }
}

void _DkExceptionReturn(void* event) {
    __UNUSED(event);
}

noreturn void _DkHandleExternalEvent(PAL_NUM event, sgx_cpu_context_t* uc,
                                     PAL_XREGS_STATE* xregs_state) {
    assert(event);
    assert(IS_ALIGNED_PTR(xregs_state, PAL_XSTATE_ALIGN));

    /* we only end up in _DkHandleExternalEvent() if interrupted during host syscall; inform LibOS
     * layer that PAL was interrupted (by setting PAL_ERRNO) */
    _DkRaiseFailure(PAL_ERROR_INTERRUPTED);

    PAL_CONTEXT ctx;
    save_pal_context(&ctx, uc, xregs_state);
    ctx.err = 0;
    ctx.trapno = event; /* TODO: event is a PAL event; is that what LibOS/app wants to see? */
    ctx.oldmask = 0;
    ctx.cr2 = 0;

    PAL_EVENT_HANDLER upcall = _DkGetExceptionHandler(event);
    if (upcall) {
        (*upcall)(/*event=*/NULL, /*arg=*/0, &ctx);
    }

    /* modification to PAL_CONTEXT is discarded; it is assumed that LibOS won't change context
     * (GPRs, FP registers) if RIP is in PAL.
     *
     * TODO: in long term, record the signal and trigger the signal handler when returning from PAL
     * via ENTER_PAL_CALL/LEAVE_PAL_CALL/LEAVE_PAL_CALL_RETURN. */
    restore_sgx_context(uc, xregs_state);
}
