/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2020 Invisible Things Lab
 *                    Michał Kowalczyk <mkow@invisiblethingslab.com>
 */

/*
 * This file defines conversions between various PAL API flags and the flags used by Linux syscalls.
 *
 * The counterpart of this file is shim_flag_conv.h in LibOS.
 */

#ifndef PAL_FLAGS_CONV_H
#define PAL_FLAGS_CONV_H

#include <asm/fcntl.h>
#include <linux/mman.h>
#include <sys/types.h>

#include "assert.h"
#include "pal.h"

static inline int PAL_MEM_FLAGS_TO_LINUX(int alloc_type, int prot) {
    assert(WITHIN_MASK(alloc_type, PAL_ALLOC_MASK));
    assert(WITHIN_MASK(prot,       PAL_PROT_MASK));

    return (alloc_type & PAL_ALLOC_RESERVE ? MAP_NORESERVE | MAP_UNINITIALIZED : 0) |
           (prot & PAL_PROT_WRITECOPY      ? MAP_PRIVATE : MAP_SHARED);
}

static inline int PAL_PROT_TO_LINUX(int prot) {
    assert(WITHIN_MASK(prot, PAL_PROT_MASK));
    return (prot & PAL_PROT_READ  ? PROT_READ  : 0) |
           (prot & PAL_PROT_WRITE ? PROT_WRITE : 0) |
           (prot & PAL_PROT_EXEC  ? PROT_EXEC  : 0);
}

static inline int PAL_ACCESS_TO_LINUX_OPEN(int access) {
    assert(WITHIN_MASK(access, PAL_ACCESS_MASK));
    return (access & PAL_ACCESS_RDONLY ? O_RDONLY : 0) |
           (access & PAL_ACCESS_WRONLY ? O_WRONLY : 0) |
           (access & PAL_ACCESS_RDWR   ? O_RDWR   : 0) |
           (access & PAL_ACCESS_APPEND ? O_APPEND : 0);
}

static inline int PAL_CREATE_TO_LINUX_OPEN(int create) {
    assert(create == 0 || create == PAL_CREATE_TRY || create == PAL_CREATE_ALWAYS);
    return (create & PAL_CREATE_TRY    ? O_CREAT          : 0) |
           (create & PAL_CREATE_ALWAYS ? O_CREAT | O_EXCL : 0);
}

static inline int PAL_OPTION_TO_LINUX_OPEN(int options) {
    assert(WITHIN_MASK(options, PAL_OPTION_CLOEXEC | PAL_OPTION_NONBLOCK | PAL_OPTION_RENAME));
    return (options & PAL_OPTION_CLOEXEC  ? O_CLOEXEC  : 0) |
           (options & PAL_OPTION_NONBLOCK ? O_NONBLOCK : 0);
}

#endif /* PAL_FLAGS_CONV_H */
