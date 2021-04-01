/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2021 Intel Corporation
 *                    Paweł Marczewski <pawel@invisiblethingslab.com>
 */

/*
 * This file describes Graphene's entrypoints from userspace (mostly from patched libc).
 */

#ifndef SHIM_ENTRY_H_
#define SHIM_ENTRY_H_

/*!
 * \brief LibOS syscall emulation entrypoint.
 *
 * Actual implementation and ABI are architecture-specific, but generally should dump the CPU
 * context and call `shim_emulate_syscall`.
 */
void syscalldb(void);

/*!
 * \brief Register a new library after loading by dynamic linker.
 *
 * Used mostly for debugger integration.
 */
int register_library(const char* name, unsigned long load_address);

#endif /* SHIM_ENTRY_H_ */
