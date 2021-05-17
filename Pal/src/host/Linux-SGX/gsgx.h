/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* (C) Copyright 2020-2021 Intel Corporation
 *                         Dmitrii Kuvaiskii <dmitrii.kuvaiskii@intel.com>
 *                         Wojtek Porczyk <woju@invisiblethingslab.com>
 */

#ifndef __ARCH_GSGX_H__
#define __ARCH_GSGX_H__

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#include <graphene-config.h>
#include <linux/stddef.h>
#include <linux/types.h>

#if defined(CONFIG_SGX_DRIVER_UPSTREAM)
#include <uapi/asm/sgx.h>
#elif defined(CONFIG_SGX_DRIVER_DCAP_1_10)
#include <sgx_user.h>
#elif defined(CONFIG_SGX_DRIVER_DCAP_1_6)
#include <uapi/asm/sgx_oot.h>
#elif defined(CONFIG_SGX_DRIVER_OOT)
#include <sgx_user.h>
#else
#error "No sgx driver configured"
#endif

#define GSGX_FILE  "/dev/gsgx"

/* Graphene needs the below subset of SGX instructions' return values */
#ifndef SGX_INVALID_SIG_STRUCT
#define SGX_INVALID_SIG_STRUCT  1
#endif

#ifndef SGX_INVALID_ATTRIBUTE
#define SGX_INVALID_ATTRIBUTE   2
#endif

#ifndef SGX_INVALID_MEASUREMENT
#define SGX_INVALID_MEASUREMENT 4
#endif

#ifndef SGX_INVALID_SIGNATURE
#define SGX_INVALID_SIGNATURE   8
#endif

#ifndef SGX_INVALID_EINITTOKEN
#define SGX_INVALID_EINITTOKEN  16
#endif

#ifndef SGX_INVALID_CPUSVN
#define SGX_INVALID_CPUSVN      32
#endif

/* SGX_INVALID_LICENSE was renamed to SGX_INVALID_EINITTOKEN in SGX driver 2.1:
 *   https://github.com/intel/linux-sgx-driver/commit/a7997dafe184d7d527683d8d46c4066db205758d */
#ifndef SGX_INVALID_LICENSE
#define SGX_INVALID_LICENSE     SGX_INVALID_EINITTOKEN
#endif

#endif /* __ARCH_GSGX_H__ */
