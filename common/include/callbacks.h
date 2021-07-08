/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2021 Intel Corporation */

/*
 * Defines a set of callbacks that the common library expects from environment it is linked into.
 * Currently, the common library expects `shim_`-prefixed callbacks from LibOS, `pal_`-prefixed
 * callbacks from PAL, and not-prefixed callbacks from all other environments (e.g., PAL regression
 * tests and non-Graphene programs). This header aliases the actual callback implementations, i.e.,
 * `shim_abort()` is aliased as `abort()` for use by the common library.
 *
 * Strictly speaking, current Graphene doesn't need different callback names for LibOS, PAL and
 * other environments. We introduce this notation for the future change where LibOS and PAL will be
 * statically linked together in a single binary (thus, we want to avoid name collisions in
 * callbacks).
 *
 * All environments should implement the following callbacks:
 *
 * - _log(), prints a debug message (at different log levels)
 * - abort(), terminates the process
 *
 */

#ifndef COMMON_CALLBACKS_H
#define COMMON_CALLBACKS_H

#include <stdnoreturn.h>

#ifdef IN_SHIM
void shim_log(int level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
noreturn void shim_abort(void);
#define _log(level, format...) shim_log(level, format)
#define abort() shim_abort()

#elif IN_PAL
void pal_log(int level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
noreturn void pal_abort(void);
#define _log(level, format...) pal_log(level, format)
#define abort() pal_abort()

#else
/* untrusted PAL, we have glibc */
void pal_log(int level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#define _log(level, format...) pal_log(level, format)
#include <stdlib.h> /* for abort(3) */
#endif

#endif /* COMMON_CALLBACKS_H */
