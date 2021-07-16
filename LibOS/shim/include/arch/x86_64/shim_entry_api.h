/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2021 Intel Corporation
 *                    Paweł Marczewski <pawel@invisiblethingslab.com>
 */

/*
 * This file contains code for calling Graphene from userspace. It can be used in patched
 * applications and libraries (e.g. glibc).
 *
 * To use, include this file in patched code and replace SYSCALL instructions with invocations of
 * SYSCALLDB assembly macro.
 */

#ifndef SHIM_ENTRY_API_H_
#define SHIM_ENTRY_API_H_

/* Offsets for GS register at which entry vectors can be found */
#define SHIM_SYSCALLDB_OFFSET         24
#define SHIM_REGISTER_LIBRARY_OFFSET  32

#ifdef __ASSEMBLER__

.macro SYSCALLDB
leaq .Lafter_syscalldb\@(%rip), %rcx
jmpq *%gs:SHIM_SYSCALLDB_OFFSET
.Lafter_syscalldb\@:
.endm

#else /* !__ASSEMBLER__ */

#define SHIM_STR(x) #x
#define SHIM_XSTR(x) SHIM_STR(x)

__asm__(
    ".macro SYSCALLDB\n"
    "leaq .Lafter_syscalldb\\@(%rip), %rcx\n"
    "jmpq *%gs:" SHIM_XSTR(SHIM_SYSCALLDB_OFFSET) "\n"
    ".Lafter_syscalldb\\@:\n"
    ".endm\n"
);

#undef SHIM_XSTR
#undef SHIM_STR

static inline int shim_register_library(const char* name, unsigned long load_address) {
    int (*register_library)(const char*, unsigned long);
    __asm__("movq %%gs:%c1, %0"
            : "=r"(register_library)
            : "i"(SHIM_REGISTER_LIBRARY_OFFSET)
            : "memory");
    return register_library(name, load_address);
}

#endif /* __ASSEMBLER__ */

#endif /* SHIM_ENTRY_API_H_ */
