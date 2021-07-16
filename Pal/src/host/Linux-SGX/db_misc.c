/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * This file contains APIs for miscellaneous use.
 */

#include <asm/fcntl.h>
#include <linux/time.h>
#include <stdint.h>

#include "api.h"
#include "cpu.h"
#include "gsgx.h"
#include "hex.h"
#include "linux_utils.h"
#include "pal.h"
#include "pal_defs.h"
#include "pal_error.h"
#include "pal_internal.h"
#include "pal_linux.h"
#include "pal_linux_defs.h"
#include "pal_security.h"
#include "seqlock.h"
#include "sgx_api.h"
#include "sgx_attest.h"
#include "spinlock.h"
#include "toml.h"

#define TSC_REFINE_INIT_TIMEOUT_USECS 10000000

uint64_t g_tsc_hz = 0; /* TSC frequency for fast and accurate time ("invariant TSC" HW feature) */
static uint64_t g_start_tsc = 0;
static uint64_t g_start_usec = 0;
static seqlock_t g_tsc_lock = INIT_SEQLOCK_UNLOCKED;

/**
 * Initialize the data structures used for date/time emulation using TSC
 */
void init_tsc(void) {
    if (is_tsc_usable()) {
        g_tsc_hz = get_tsc_hz();
    }
}

/* TODO: result comes from the untrusted host, introduce some schielding */
int _DkSystemTimeQuery(uint64_t* out_usec) {
    int ret;

    if (!g_tsc_hz) {
        /* RDTSC is not allowed or no Invariant TSC feature -- fallback to the slow ocall */
        return ocall_gettime(out_usec);
    }

    uint32_t seq;
    uint64_t start_tsc;
    uint64_t start_usec;
    do {
        seq = read_seqbegin(&g_tsc_lock);
        start_tsc  = g_start_tsc;
        start_usec = g_start_usec;
    } while (read_seqretry(&g_tsc_lock, seq));

    uint64_t usec = 0;
    if (start_tsc > 0 && start_usec > 0) {
        /* baseline TSC/usec pair was initialized, can calculate time via RDTSC (but should be
         * careful with integer overflow during calculations) */
        uint64_t diff_tsc = get_tsc() - start_tsc;
        if (diff_tsc < UINT64_MAX / 1000000) {
            uint64_t diff_usec = diff_tsc * 1000000 / g_tsc_hz;
            if (diff_usec < TSC_REFINE_INIT_TIMEOUT_USECS) {
                /* less than TSC_REFINE_INIT_TIMEOUT_USECS passed from the previous update of
                 * TSC/usec pair (time drift is contained), use the RDTSC-calculated time */
                usec = start_usec + diff_usec;
                if (usec < start_usec)
                    return -PAL_ERROR_OVERFLOW;
            }
        }
    }

    if (usec) {
        *out_usec = usec;
        return 0;
    }

    /* if we are here, either the baseline TSC/usec pair was not yet initialized or too much time
     * passed since the previous TSC/usec update, so let's refresh them to contain the time drift */
    uint64_t tsc_cyc1 = get_tsc();
    ret = ocall_gettime(&usec);
    if (ret < 0)
        return -PAL_ERROR_DENIED;
    uint64_t tsc_cyc2 = get_tsc();

    /* we need to match the OCALL-obtained timestamp (`usec`) with the RDTSC-obtained number of
     * cycles (`tsc_cyc`); since OCALL is a time-consuming operation, we estimate `tsc_cyc` as a
     * mid-point between the RDTSC values obtained right-before and right-after the OCALL. */
    uint64_t tsc_cyc = tsc_cyc1 + (tsc_cyc2 - tsc_cyc1) / 2;
    if (tsc_cyc < tsc_cyc1)
        return -PAL_ERROR_OVERFLOW;

    /* refresh the baseline data if no other thread updated g_start_tsc */
    write_seqbegin(&g_tsc_lock);
    if (g_start_tsc < tsc_cyc) {
        g_start_tsc  = tsc_cyc;
        g_start_usec = usec;
    }
    write_seqend(&g_tsc_lock);

    *out_usec = usec;
    return 0;
}

#define CPUID_CACHE_SIZE 64 /* cache only 64 distinct CPUID entries; sufficient for most apps */
static struct pal_cpuid {
    unsigned int leaf, subleaf;
    unsigned int values[4];
} g_pal_cpuid_cache[CPUID_CACHE_SIZE];

static int g_pal_cpuid_cache_top   = 0;
static spinlock_t g_cpuid_cache_lock = INIT_SPINLOCK_UNLOCKED;

static int get_cpuid_from_cache(unsigned int leaf, unsigned int subleaf, unsigned int values[4]) {
    int ret = -PAL_ERROR_DENIED;

    spinlock_lock(&g_cpuid_cache_lock);
    for (int i = 0; i < g_pal_cpuid_cache_top; i++) {
        if (g_pal_cpuid_cache[i].leaf == leaf && g_pal_cpuid_cache[i].subleaf == subleaf) {
            values[0] = g_pal_cpuid_cache[i].values[0];
            values[1] = g_pal_cpuid_cache[i].values[1];
            values[2] = g_pal_cpuid_cache[i].values[2];
            values[3] = g_pal_cpuid_cache[i].values[3];
            ret = 0;
            break;
        }
    }
    spinlock_unlock(&g_cpuid_cache_lock);
    return ret;
}

static void add_cpuid_to_cache(unsigned int leaf, unsigned int subleaf, unsigned int values[4]) {
    spinlock_lock(&g_cpuid_cache_lock);

    struct pal_cpuid* chosen = NULL;
    if (g_pal_cpuid_cache_top < CPUID_CACHE_SIZE) {
        for (int i = 0; i < g_pal_cpuid_cache_top; i++) {
            if (g_pal_cpuid_cache[i].leaf == leaf && g_pal_cpuid_cache[i].subleaf == subleaf) {
                /* this CPUID entry is already present in the cache, no need to add */
                break;
            }
        }
        chosen = &g_pal_cpuid_cache[g_pal_cpuid_cache_top++];
    }

    if (chosen) {
        chosen->leaf      = leaf;
        chosen->subleaf   = subleaf;
        chosen->values[0] = values[0];
        chosen->values[1] = values[1];
        chosen->values[2] = values[2];
        chosen->values[3] = values[3];
    }

    spinlock_unlock(&g_cpuid_cache_lock);
}

static inline uint32_t extension_enabled(uint32_t xfrm, uint32_t bit_idx) {
    uint32_t feature_bit = 1U << bit_idx;
    return xfrm & feature_bit;
}

static __sgx_mem_aligned sgx_report_t report;
static __sgx_mem_aligned sgx_target_info_t target_info;
static __sgx_mem_aligned sgx_report_data_t report_data;

/* Initialize the data structures used for CPUID emulation. */
void init_cpuid(void) {
    memset(&report, 0, sizeof(report));
    memset(&target_info, 0, sizeof(target_info));
    memset(&report_data, 0, sizeof(report_data));
    sgx_report(&target_info, &report_data, &report);
}

/**
 * Sanity check untrusted CPUID inputs.
 *
 * The basic idea is that there are only a handful of extensions and we know the size needed to
 * store each extension's state. Use this to sanitize host's untrusted cpuid output. We also know
 * through xfrm what extensions are enabled inside the enclave.
 */
static void sanity_check_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t values[4]) {
    uint64_t xfrm = report.body.attributes.xfrm;

    enum cpu_extension { x87 = 0, SSE, AVX, MPX_1, MPX_2, AVX512_1, AVX512_2, AVX512_3, PKRU = 9 };
    const uint32_t extension_sizes_bytes[] = {
        [AVX] = 256,      [MPX_1] = 64,      [MPX_2] = 64, [AVX512_1] = 64,
        [AVX512_2] = 512, [AVX512_3] = 1024, [PKRU] = 8};
    /* Note that AVX offset is 576 bytes and MPX_1 starts at 960. The AVX state size is 256, leaving
     * 128 bytes unaccounted for. */
    const uint32_t extension_offset_bytes[] = {
        [AVX] = 576,       [MPX_1] = 960,     [MPX_2] = 1024, [AVX512_1] = 1088,
        [AVX512_2] = 1152, [AVX512_3] = 1664, [PKRU] = 2688};
    enum register_index { EAX = 0, EBX, ECX, EDX };

    const uint32_t EXTENDED_STATE_LEAF = 0xd;

    if (leaf == EXTENDED_STATE_LEAF) {
        switch (subleaf) {
            case 0x0:
                /* From the SDM: "EDX:EAX is a bitmap of all the user state components that can be
                 * managed using the XSAVE feature set. A bit can be set in XCR0 if and only if the
                 * corresponding bit is set in this bitmap. Every processor that supports the XSAVE
                 * feature set will set EAX[0] (x87 state) and EAX[1] (SSE state)."
                 *
                 * On EENTER/ERESUME, the system installs xfrm into XCR0. Hence, we return xfrm here
                 * in EAX.
                 */
                values[EAX] = xfrm;

                /* From the SDM: "EBX enumerates the size (in bytes) required by the XSAVE
                 * instruction for an XSAVE area containing all the user state components
                 * corresponding to bits currently set in XCR0."
                 */
                uint32_t xsave_size = 0;
                /* Start from AVX since x87 and SSE are always captured using XSAVE. Also, x87 and
                 * SSE state size is implicitly included in the extension's offset, e.g., AVX's
                 * offset is 576 which includes x87 and SSE state as well as the XSAVE header. */
                for (int i = AVX; i <= PKRU; i++) {
                    if (extension_enabled(xfrm, i)) {
                        xsave_size = extension_offset_bytes[i] + extension_sizes_bytes[i];
                    }
                }
                values[EBX] = xsave_size;

                /* From the SDM: "ECX enumerates the size (in bytes) required by the XSAVE
                 * instruction for an XSAVE area containing all the user state components supported
                 * by this processor."
                 *
                 * We are assuming here that inside the enclave, ECX and EBX for leaf 0xD and
                 * subleaf 0x1 should always be identical, while outside they can potentially be
                 * different. Also, outside of SGX EBX can change at runtime, while ECX is a static
                 * property.
                 */
                values[ECX] = values[EBX];
                values[EDX] = 0;

                break;
            case 0x1: {
                const uint32_t xsave_legacy_size = 512;
                const uint32_t xsave_header = 64;
                uint32_t save_size_bytes = xsave_legacy_size + xsave_header;

                /* Start with AVX, since x87 and SSE state is already included when initializing
                 * `save_size_bytes`. */
                for (int i = AVX; i <= PKRU; i++) {
                    if (extension_enabled(xfrm, i)) {
                        save_size_bytes += extension_sizes_bytes[i];
                    }
                }
                /* EBX reports the actual size occupied by those extensions irrespective of their
                 * offsets within the xsave area.
                 */
                values[EBX] = save_size_bytes;

                break;
            }
            case AVX:
            case MPX_1:
            case MPX_2:
            case AVX512_1:
            case AVX512_2:
            case AVX512_3:
            case PKRU:
                if (extension_enabled(xfrm, subleaf)) {
                    if (values[EAX] != extension_sizes_bytes[subleaf] ||
                            values[EBX] != extension_offset_bytes[subleaf]) {
                        log_error("Unexpected value in host CPUID. Exiting...");
                        _DkProcessExit(1);
                    }
                } else {
                    /* SGX enclave doesn't use this CPU extension, pretend it doesn't exist by
                     * forcing EAX ("size in bytes of the save area for an extended state feature")
                     * and EBX ("offset in bytes of this extended state component's save area from
                     * the beginning of the XSAVE/XRSTOR area") to zero */
                    values[EAX] = 0;
                    values[EBX] = 0;
                }
                break;
        }
    }
}

struct cpuid_leaf {
    unsigned int leaf;
    bool zero_subleaf; /* if subleaf is not used by this leaf, then CPUID instruction expects it to
                          be explicitly zeroed out (see _DkCpuIdRetrieve() implementation below) */
    bool cache;        /* if leaf + subleaf pair is constant across all cores and sockets, then we
                          can add the returned CPUID values of this pair to the local cache (see
                          _DkCpuIdRetrieve() implementation below) */
};

/* NOTE: some CPUID leaves/subleaves may theoretically return different values when accessed from
 *       different sockets in a multisocket system and thus should not be declared with
 *       `.cache = true` below, but we don't know of any such systems and currently ignore this */
static const struct cpuid_leaf cpuid_known_leaves[] = {
    /* basic CPUID leaf functions start here */
    {.leaf = 0x00, .zero_subleaf = true,  .cache = true},  /* Highest Func Param and Manufacturer */
    {.leaf = 0x01, .zero_subleaf = true,  .cache = false}, /* Processor Info and Feature Bits */
    {.leaf = 0x02, .zero_subleaf = true,  .cache = true},  /* Cache and TLB Descriptor */
    {.leaf = 0x03, .zero_subleaf = true,  .cache = true},  /* Processor Serial Number */
    {.leaf = 0x04, .zero_subleaf = false, .cache = false}, /* Deterministic Cache Parameters */
    {.leaf = 0x05, .zero_subleaf = true,  .cache = true},  /* MONITOR/MWAIT */
    {.leaf = 0x06, .zero_subleaf = true,  .cache = true},  /* Thermal and Power Management */
    {.leaf = 0x07, .zero_subleaf = false, .cache = true},  /* Structured Extended Feature Flags */
    /* NOTE: 0x08 leaf is reserved, see code below */
    {.leaf = 0x09, .zero_subleaf = true,  .cache = true},  /* Direct Cache Access Information */
    {.leaf = 0x0A, .zero_subleaf = true,  .cache = true},  /* Architectural Performance Monitoring */
    {.leaf = 0x0B, .zero_subleaf = false, .cache = false}, /* Extended Topology Enumeration */
    /* NOTE: 0x0C leaf is reserved, see code below */
    {.leaf = 0x0D, .zero_subleaf = false, .cache = true},  /* Processor Extended State Enumeration */
    /* NOTE: 0x0E leaf is reserved, see code below */
    {.leaf = 0x0F, .zero_subleaf = false, .cache = true},  /* Intel RDT Monitoring */
    {.leaf = 0x10, .zero_subleaf = false, .cache = true},  /* RDT/L2/L3 Cache Allocation Tech */
    /* NOTE: 0x11 leaf is reserved, see code below */
    {.leaf = 0x12, .zero_subleaf = false, .cache = true},  /* Intel SGX Capability */
    /* NOTE: 0x13 leaf is reserved, see code below */
    {.leaf = 0x14, .zero_subleaf = false, .cache = true},  /* Intel Processor Trace Enumeration */
    {.leaf = 0x15, .zero_subleaf = true,  .cache = true},  /* Time Stamp Counter/Core Clock */
    {.leaf = 0x16, .zero_subleaf = true,  .cache = true},  /* Processor Frequency Information */
    {.leaf = 0x17, .zero_subleaf = false, .cache = true},  /* System-On-Chip Vendor Attribute */
    {.leaf = 0x18, .zero_subleaf = false, .cache = true},  /* Deterministic Address Translation */
    {.leaf = 0x19, .zero_subleaf = true,  .cache = true},  /* Key Locker */
    {.leaf = 0x1A, .zero_subleaf = true,  .cache = false}, /* Hybrid Information Enumeration */
    /* NOTE: 0x1B leaf is not recognized, see code below */
    /* NOTE: 0x1C leaf is not recognized, see code below */
    /* NOTE: 0x1D leaf is not recognized, see code below */
    /* NOTE: 0x1E leaf is not recognized, see code below */
    {.leaf = 0x1F, .zero_subleaf = false, .cache = false}, /* Intel V2 Ext Topology Enumeration */
    /* basic CPUID leaf functions end here */

    /* invalid CPUID leaf functions (no existing or future CPU will return any meaningful
     * information in these leaves) occupy 40000000 - 4FFFFFFFH -- they are treated the same as
     * unrecognized leaves, see code below */

    /* extended CPUID leaf functions start here */
    {.leaf = 0x80000000, .zero_subleaf = true, .cache = true}, /* Get Highest Extended Function */
    {.leaf = 0x80000001, .zero_subleaf = true, .cache = true}, /* Extended Processor Info */
    {.leaf = 0x80000002, .zero_subleaf = true, .cache = true}, /* Processor Brand String 1 */
    {.leaf = 0x80000003, .zero_subleaf = true, .cache = true}, /* Processor Brand String 2 */
    {.leaf = 0x80000004, .zero_subleaf = true, .cache = true}, /* Processor Brand String 3 */
    {.leaf = 0x80000005, .zero_subleaf = true, .cache = true}, /* L1 Cache and TLB Identifiers */
    {.leaf = 0x80000006, .zero_subleaf = true, .cache = true}, /* Extended L2 Cache Features */
    {.leaf = 0x80000007, .zero_subleaf = true, .cache = true}, /* Advanced Power Management */
    {.leaf = 0x80000008, .zero_subleaf = true, .cache = true}, /* Virtual/Physical Address Sizes */
    /* extended CPUID leaf functions end here */
};

int _DkCpuIdRetrieve(unsigned int leaf, unsigned int subleaf, unsigned int values[4]) {
    /* A few basic leaves are considered reserved and always return zeros; see corresponding EAX
     * cases in the "Operation" section of CPUID description in Intel SDM, Vol. 2A, Chapter 3.2.
     *
     * NOTE: Leaves 0x11 and 0x13 are not marked as reserved in Intel SDM but the actual CPUs return
     *       all-zeros on them (as if these leaves are reserved). It is unclear why this discrepancy
     *       exists, but we decided to emulate how actual CPUs behave. */
    if (leaf == 0x08 || leaf == 0x0C || leaf == 0x0E || leaf == 0x11 || leaf == 0x13) {
        values[0] = 0;
        values[1] = 0;
        values[2] = 0;
        values[3] = 0;
        return 0;
    }

    const struct cpuid_leaf* known_leaf = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(cpuid_known_leaves); i++) {
        if (leaf == cpuid_known_leaves[i].leaf) {
            known_leaf = &cpuid_known_leaves[i];
            break;
        }
    }

    if (!known_leaf) {
        /* leaf is not recognized (EAX value is outside of recongized range for CPUID), return info
         * for highest basic information leaf (see cpuid_known_leaves table; currently 0x1F); see
         * the DEFAULT case in the "Operation" section of CPUID description in Intel SDM, Vol. 2A,
         * Chapter 3.2 */
        leaf = 0x1F;
        for (size_t i = 0; i < ARRAY_SIZE(cpuid_known_leaves); i++) {
            if (leaf == cpuid_known_leaves[i].leaf) {
                known_leaf = &cpuid_known_leaves[i];
                break;
            }
        }
    }

    if (!known_leaf)
       goto fail;

    if ((leaf == 0x07 && subleaf != 0 && subleaf != 1) ||
        (leaf == 0x0F && subleaf != 0 && subleaf != 1) ||
        (leaf == 0x10 && subleaf != 0 && subleaf != 1 && subleaf != 2) ||
        (leaf == 0x14 && subleaf != 0 && subleaf != 1)) {
        /* leaf-specific checks: some leaves have only specific subleaves */
        goto fail;
    }

    if (known_leaf->zero_subleaf)
        subleaf = 0;

    if (known_leaf->cache && !get_cpuid_from_cache(leaf, subleaf, values))
        return 0;

    if (ocall_cpuid(leaf, subleaf, values) < 0)
        return -PAL_ERROR_DENIED;

    sanity_check_cpuid(leaf, subleaf, values);

    if (known_leaf->cache)
        add_cpuid_to_cache(leaf, subleaf, values);

    return 0;
fail:
    log_error("Unrecognized leaf/subleaf in CPUID (EAX=0x%x, ECX=0x%x). Exiting...", leaf, subleaf);
    _DkProcessExit(1);
}

int _DkAttestationReport(PAL_PTR user_report_data, PAL_NUM* user_report_data_size,
                         PAL_PTR target_info, PAL_NUM* target_info_size, PAL_PTR report,
                         PAL_NUM* report_size) {
    __sgx_mem_aligned sgx_report_data_t stack_report_data = {0};
    __sgx_mem_aligned sgx_target_info_t stack_target_info = {0};
    __sgx_mem_aligned sgx_report_t stack_report = {0};

    if (!user_report_data_size || !target_info_size || !report_size)
        return -PAL_ERROR_INVAL;

    if (*user_report_data_size != sizeof(stack_report_data) ||
        *target_info_size != sizeof(stack_target_info) || *report_size != sizeof(stack_report)) {
        /* inform the caller of SGX sizes for user_report_data, target_info, and report */
        goto out;
    }

    if (!user_report_data || !target_info) {
        /* cannot produce report without user_report_data or target_info */
        goto out;
    }

    bool populate_target_info = false;
    if (!memcmp(target_info, &stack_target_info, sizeof(stack_target_info))) {
        /* caller supplied all-zero target_info, wants to get this enclave's target info */
        populate_target_info = true;
    }

    memcpy(&stack_report_data, user_report_data, sizeof(stack_report_data));
    memcpy(&stack_target_info, target_info, sizeof(stack_target_info));

    int ret = sgx_report(&stack_target_info, &stack_report_data, &stack_report);
    if (ret < 0) {
        /* caller already provided reasonable sizes, so just error out without updating them */
        return -PAL_ERROR_INVAL;
    }

    if (populate_target_info) {
        sgx_target_info_t* ti = (sgx_target_info_t*)target_info;
        memcpy(&ti->attributes, &stack_report.body.attributes, sizeof(ti->attributes));
        memcpy(&ti->config_id, &stack_report.body.config_id, sizeof(ti->config_id));
        memcpy(&ti->config_svn, &stack_report.body.config_svn, sizeof(ti->config_svn));
        memcpy(&ti->misc_select, &stack_report.body.misc_select, sizeof(ti->misc_select));
        memcpy(&ti->mr_enclave, &stack_report.body.mr_enclave, sizeof(ti->mr_enclave));
    }

    if (report) {
        /* report may be NULL if caller only wants to know the size of target_info and/or report */
        memcpy(report, &stack_report, sizeof(stack_report));
    }

out:
    *user_report_data_size = sizeof(stack_report_data);
    *target_info_size      = sizeof(stack_target_info);
    *report_size           = sizeof(stack_report);
    return 0;
}

int _DkAttestationQuote(const PAL_PTR user_report_data, PAL_NUM user_report_data_size,
                        PAL_PTR quote, PAL_NUM* quote_size) {
    if (user_report_data_size != sizeof(sgx_report_data_t))
        return -PAL_ERROR_INVAL;

    int ret;
    bool is_epid;
    sgx_spid_t spid = {0};
    bool linkable;

    /* read sgx.ra_client_spid from manifest (must be hex string) */
    char* ra_client_spid_str = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "sgx.ra_client_spid", &ra_client_spid_str);
    if (ret < 0) {
        log_error("Cannot parse \'sgx.ra_client_spid\' (the value must be put in double quotes!)");
        return -PAL_ERROR_INVAL;
    }

    if (!ra_client_spid_str || strlen(ra_client_spid_str) == 0) {
        /* No Software Provider ID (SPID) specified in the manifest, it is DCAP attestation --
         * for DCAP, spid and linkable arguments are ignored (we unset them for sanity) */
        is_epid = false;
        linkable = false;
    } else {
        /* SPID specified in the manifest, it is EPID attestation -- read spid and linkable */
        is_epid = true;

        if (strlen(ra_client_spid_str) != sizeof(sgx_spid_t) * 2) {
            log_error("Malformed \'sgx.ra_client_spid\' value in the manifest: %s",
                      ra_client_spid_str);
            free(ra_client_spid_str);
            return -PAL_ERROR_INVAL;
        }

        for (size_t i = 0; i < strlen(ra_client_spid_str); i++) {
            int8_t val = hex2dec(ra_client_spid_str[i]);
            if (val < 0) {
                log_error("Malformed \'sgx.ra_client_spid\' value in the manifest: %s",
                          ra_client_spid_str);
                free(ra_client_spid_str);
                return -PAL_ERROR_INVAL;
            }
            spid[i / 2] = spid[i / 2] * 16 + (uint8_t)val;
        }

        /* read sgx.ra_client_linkable from manifest */
        ret = toml_bool_in(g_pal_state.manifest_root, "sgx.ra_client_linkable",
                           /*defaultval=*/false, &linkable);
        if (ret < 0) {
            log_error("Cannot parse \'sgx.ra_client_linkable\' (the value must be `true` or "
                      "`false`)");
            free(ra_client_spid_str);
            return -PAL_ERROR_INVAL;
        }
    }

    free(ra_client_spid_str);

    sgx_quote_nonce_t nonce;
    ret = _DkRandomBitsRead(&nonce, sizeof(nonce));
    if (ret < 0)
        return ret;

    char* pal_quote       = NULL;
    size_t pal_quote_size = 0;

    ret = sgx_get_quote(is_epid ? &spid : NULL, &nonce, user_report_data, linkable, &pal_quote,
                        &pal_quote_size);
    if (ret < 0)
        return ret;

    if (*quote_size < pal_quote_size) {
        *quote_size = pal_quote_size;
        free(pal_quote);
        return -PAL_ERROR_NOMEM;
    }

    if (quote) {
        /* quote may be NULL if caller only wants to know the size of the quote */
        assert(pal_quote);
        memcpy(quote, pal_quote, pal_quote_size);
    }

    *quote_size = pal_quote_size;
    free(pal_quote);
    return 0;
}

int _DkSetProtectedFilesKey(const PAL_PTR pf_key_hex) {
    return set_protected_files_key(pf_key_hex);
}

/* Rest is moved from old `db_main-x86_64.c`. */

#define FOUR_CHARS_VALUE(s, w)      \
    (s)[0] = (w) & 0xff;            \
    (s)[1] = ((w) >>  8) & 0xff;    \
    (s)[2] = ((w) >> 16) & 0xff;    \
    (s)[3] = ((w) >> 24) & 0xff;

#define BPI  32
#define POWER2(power) \
    (1ULL << (power))
#define RIGHTMASK(width) \
    (((unsigned long)(width) >= BPI) ? ~0ULL : POWER2(width) - 1ULL)

#define BIT_EXTRACT_LE(value, start, after) \
    (((unsigned long)(value) & RIGHTMASK(after)) >> start)

#define CPUID_LEAF_INVARIANT_TSC 0x80000007
#define CPUID_LEAF_TSC_FREQ 0x15
#define CPUID_LEAF_PROC_FREQ 0x16

static const char* const g_cpu_flags[] = {
    "fpu",    // "x87 FPU on chip"
    "vme",    // "virtual-8086 mode enhancement"
    "de",     // "debugging extensions"
    "pse",    // "page size extensions"
    "tsc",    // "time stamp counter"
    "msr",    // "RDMSR and WRMSR support"
    "pae",    // "physical address extensions"
    "mce",    // "machine check exception"
    "cx8",    // "CMPXCHG8B inst."
    "apic",   // "APIC on chip"
    NULL,
    "sep",    // "SYSENTER and SYSEXIT"
    "mtrr",   // "memory type range registers"
    "pge",    // "PTE global bit"
    "mca",    // "machine check architecture"
    "cmov",   // "conditional move/compare instruction"
    "pat",    // "page attribute table"
    "pse36",  // "page size extension"
    "pn",     // "processor serial number"
    "clflush",    // "CLFLUSH instruction"
    NULL,
    "dts",    // "debug store"
    "acpi",   // "Onboard thermal control"
    "mmx",    // "MMX Technology"
    "fxsr",   // "FXSAVE/FXRSTOR"
    "sse",    // "SSE extensions"
    "sse2",   // "SSE2 extensions"
    "ss",     // "self snoop"
    "ht",     // "hyper-threading / multi-core supported"
    "tm",     // "therm. monitor"
    "ia64",   // "IA64"
    "pbe",    // "pending break event"
};

static ssize_t read_file_buffer(const char* filename, char* buf, size_t buf_size) {
    int fd;

    fd = ocall_open(filename, O_RDONLY, 0);
    if (fd < 0)
        return fd;

    /* Although the whole file might not fit in this size, the first cpu description should. */
    ssize_t n = ocall_read(fd, buf, buf_size);
    ocall_close(fd);

    return n;
}

static double get_bogomips(void) {
    char buf[2048];
    ssize_t len;

    len = read_file_buffer("/proc/cpuinfo", buf, sizeof(buf) - 1);
    if (len < 0)
        return 0.0;
    buf[len] = 0;

    return sanitize_bogomips_value(get_bogomips_from_cpuinfo_buf(buf));
}

bool is_tsc_usable(void) {
    uint32_t words[CPUID_WORD_NUM];
    _DkCpuIdRetrieve(CPUID_LEAF_INVARIANT_TSC, 0, words);
    return words[CPUID_WORD_EDX] & 1 << 8;
}

/* return TSC frequency or 0 if invariant TSC is not supported */
uint64_t get_tsc_hz(void) {
    uint32_t words[CPUID_WORD_NUM];

    _DkCpuIdRetrieve(CPUID_LEAF_TSC_FREQ, 0, words);
    if (!words[CPUID_WORD_EAX] || !words[CPUID_WORD_EBX]) {
        /* TSC/core crystal clock ratio is not enumerated, can't use RDTSC for accurate time */
        return 0;
    }

    if (words[CPUID_WORD_ECX] > 0) {
        /* calculate TSC frequency as core crystal clock frequency (EAX) * EBX / EAX; cast to 64-bit
         * first to prevent integer overflow */
        uint64_t ecx_hz = words[CPUID_WORD_ECX];
        return ecx_hz * words[CPUID_WORD_EBX] / words[CPUID_WORD_EAX];
    }

    /* some Intel CPUs do not report nominal frequency of crystal clock, let's calculate it
     * based on Processor Frequency Information Leaf (CPUID 16H); this leaf always exists if
     * TSC Frequency Leaf exists; logic is taken from Linux 5.11's arch/x86/kernel/tsc.c */
    _DkCpuIdRetrieve(CPUID_LEAF_PROC_FREQ, 0, words);
    if (!words[CPUID_WORD_EAX]) {
        /* processor base frequency (in MHz) is not enumerated, can't calculate frequency */
        return 0;
    }

    /* processor base frequency is in MHz but we need to return TSC frequency in Hz; cast to 64-bit
     * first to prevent integer overflow */
    uint64_t base_frequency_mhz = words[CPUID_WORD_EAX];
    return base_frequency_mhz * 1000000;
}

int _DkGetCPUInfo(PAL_CPU_INFO* ci) {
    unsigned int words[CPUID_WORD_NUM];
    int rv = 0;

    const size_t VENDOR_ID_SIZE = 13;
    char* vendor_id = malloc(VENDOR_ID_SIZE);
    if (!vendor_id)
        return -PAL_ERROR_NOMEM;

    _DkCpuIdRetrieve(0, 0, words);
    FOUR_CHARS_VALUE(&vendor_id[0], words[CPUID_WORD_EBX]);
    FOUR_CHARS_VALUE(&vendor_id[4], words[CPUID_WORD_EDX]);
    FOUR_CHARS_VALUE(&vendor_id[8], words[CPUID_WORD_ECX]);
    vendor_id[VENDOR_ID_SIZE - 1] = '\0';
    ci->cpu_vendor = vendor_id;
    // Must be an Intel CPU
    if (memcmp(vendor_id, "GenuineIntel", 12)) {
        rv = -PAL_ERROR_INVAL;
        goto out_vendor_id;
    }

    const size_t BRAND_SIZE = 49;
    char* brand = malloc(BRAND_SIZE);
    if (!brand) {
        rv = -PAL_ERROR_NOMEM;
        goto out_vendor_id;
    }
    _DkCpuIdRetrieve(0x80000002, 0, words);
    memcpy(&brand[ 0], words, sizeof(unsigned int) * CPUID_WORD_NUM);
    _DkCpuIdRetrieve(0x80000003, 0, words);
    memcpy(&brand[16], words, sizeof(unsigned int) * CPUID_WORD_NUM);
    _DkCpuIdRetrieve(0x80000004, 0, words);
    memcpy(&brand[32], words, sizeof(unsigned int) * CPUID_WORD_NUM);
    brand[BRAND_SIZE - 1] = '\0';
    ci->cpu_brand = brand;

    ci->online_logical_cores = g_pal_sec.online_logical_cores;
    ci->physical_cores_per_socket = g_pal_sec.physical_cores_per_socket;
    ci->cpu_socket = g_pal_sec.cpu_socket;

    _DkCpuIdRetrieve(1, 0, words);
    ci->cpu_family   = BIT_EXTRACT_LE(words[CPUID_WORD_EAX],  8, 12) +
                       BIT_EXTRACT_LE(words[CPUID_WORD_EAX], 20, 28);
    ci->cpu_model    = BIT_EXTRACT_LE(words[CPUID_WORD_EAX],  4,  8) +
                      (BIT_EXTRACT_LE(words[CPUID_WORD_EAX], 16, 20) << 4);
    ci->cpu_stepping = BIT_EXTRACT_LE(words[CPUID_WORD_EAX],  0,  4);

    size_t flen = 0;
    size_t fmax = 80;
    char* flags = malloc(fmax);
    if (!flags) {
        rv = -PAL_ERROR_NOMEM;
        goto out_brand;
    }

    for (int i = 0; i < 32; i++) {
        if (!g_cpu_flags[i])
            continue;

        if (BIT_EXTRACT_LE(words[CPUID_WORD_EDX], i, i + 1)) {
            size_t len = strlen(g_cpu_flags[i]);
            if (flen + len + 1 > fmax) {
                char* new_flags = malloc(fmax * 2);
                if (!new_flags) {
                    rv = -PAL_ERROR_NOMEM;
                    goto out_flags;
                }
                memcpy(new_flags, flags, flen);
                free(flags);
                fmax *= 2;
                flags = new_flags;
            }
            memcpy(flags + flen, g_cpu_flags[i], len);
            flen += len;
            flags[flen++] = ' ';
        }
    }

    flags[flen ? flen - 1 : 0] = 0;
    ci->cpu_flags = flags;

    ci->cpu_bogomips = get_bogomips();
    if (ci->cpu_bogomips == 0.0) {
        log_warning("bogomips could not be retrieved, passing 0.0 to the application");
    }

    return rv;
out_flags:
    free(flags);
out_brand:
    free(brand);
out_vendor_id:
    free(vendor_id);
    return rv;
}

int _DkGetTopologyInfo(PAL_TOPO_INFO* topo_info) {
    topo_info->num_online_nodes = g_pal_sec.topo_info.num_online_nodes;
    topo_info->num_cache_index  = g_pal_sec.topo_info.num_cache_index;
    topo_info->core_topology    = g_pal_sec.topo_info.core_topology;
    topo_info->numa_topology    = g_pal_sec.topo_info.numa_topology;
    COPY_ARRAY(topo_info->online_logical_cores, g_pal_sec.topo_info.online_logical_cores);
    COPY_ARRAY(topo_info->possible_logical_cores, g_pal_sec.topo_info.possible_logical_cores);
    COPY_ARRAY(topo_info->online_nodes, g_pal_sec.topo_info.online_nodes);

    return 0;
}

int _DkRandomBitsRead(void* buffer, size_t size) {
    uint32_t rand;
    for (size_t i = 0; i < size; i += sizeof(rand)) {
        rand = rdrand();
        memcpy(buffer + i, &rand, MIN(sizeof(rand), size - i));
    }
    return 0;
}

int _DkSegmentRegisterGet(int reg, void** addr) {
    switch (reg) {
        case PAL_SEGMENT_FS:
            *addr = (void*)GET_ENCLAVE_TLS(fsbase);
            return 0;
        case PAL_SEGMENT_GS:
            /* GS is internally used, deny any access to it */
            return -PAL_ERROR_DENIED;
        default:
            return -PAL_ERROR_INVAL;
    }
}

int _DkSegmentRegisterSet(int reg, void* addr) {
    switch (reg) {
        case PAL_SEGMENT_FS:
            SET_ENCLAVE_TLS(fsbase, addr);
            wrfsbase((uint64_t)addr);
            return 0;
        case PAL_SEGMENT_GS:
            /* GS is internally used, deny any access to it */
            return -PAL_ERROR_DENIED;
        default:
            return -PAL_ERROR_INVAL;
    }
}
