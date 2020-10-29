/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * This file contains x86_64-specific functions of the PAL loader.
 */

#include <asm/prctl.h>

#include "api.h"
#include "cpu.h"
#include "linux_utils.h"
#include "pal_linux.h"

static double get_bogomips(void) {
    char buf[2048];
    ssize_t len;

    len = read_file_buffer("/proc/cpuinfo", buf, sizeof(buf) - 1);
    if (len < 0)
        return 0.0;
    buf[len] = 0;

    return sanitize_bogomips_value(get_bogomips_from_cpuinfo_buf(buf));
}

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

int _DkGetCPUInfo(PAL_CPU_INFO* ci) {
    unsigned int words[PAL_CPUID_WORD_NUM];
    int rv = 0;

    const size_t VENDOR_ID_SIZE = 13;
    char* vendor_id = malloc(VENDOR_ID_SIZE);
    if (!vendor_id)
        return -PAL_ERROR_NOMEM;

    cpuid(0, 0, words);

    FOUR_CHARS_VALUE(&vendor_id[0], words[PAL_CPUID_WORD_EBX]);
    FOUR_CHARS_VALUE(&vendor_id[4], words[PAL_CPUID_WORD_EDX]);
    FOUR_CHARS_VALUE(&vendor_id[8], words[PAL_CPUID_WORD_ECX]);
    vendor_id[VENDOR_ID_SIZE - 1] = '\0';
    ci->cpu_vendor = vendor_id;

    const size_t BRAND_SIZE = 49;
    char* brand = malloc(BRAND_SIZE);
    if (!brand) {
        rv = -PAL_ERROR_NOMEM;
        goto out_vendor_id;
    }
    cpuid(0x80000002, 0, words);
    memcpy(&brand[ 0], words, sizeof(unsigned int) * PAL_CPUID_WORD_NUM);
    cpuid(0x80000003, 0, words);
    memcpy(&brand[16], words, sizeof(unsigned int) * PAL_CPUID_WORD_NUM);
    cpuid(0x80000004, 0, words);
    memcpy(&brand[32], words, sizeof(unsigned int) * PAL_CPUID_WORD_NUM);
    brand[BRAND_SIZE - 1] = '\0';
    ci->cpu_brand = brand;

    /* we cannot use CPUID(0xb) because it counts even disabled-by-BIOS cores (e.g. HT cores);
     * instead extract info on total number of logical processors, number of physical cores,
     * SMT support etc. by parsing sysfs pseudo-files */
    int cpu_num = get_hw_resource("/sys/devices/system/cpu/online", /*count=*/true);
    if (cpu_num < 0) {
        rv = cpu_num;
        goto out_brand;
    }
    ci->cpu_num = cpu_num;

    int possible_cpus = get_hw_resource("/sys/devices/system/cpu/possible", /*count=*/true);
    /* TODO: correctly support offline CPUs */
    if ((possible_cpus > 0) && (possible_cpus > cpu_num)) {
         printf("Warning: some CPUs seem to be offline; Graphene doesn't take this into account "
                "which may lead to subpar performance\n");
    }

    int cpu_cores = get_hw_resource("/sys/devices/system/cpu/cpu0/topology/core_siblings_list",
                                     /*count=*/true);
    if (cpu_cores < 0) {
        rv = cpu_cores;
        goto out_brand;
    }

    int smt_siblings = get_hw_resource("/sys/devices/system/cpu/cpu0/topology/thread_siblings_list",
                                       /*count=*/true);
    if (smt_siblings < 0) {
        rv = smt_siblings;
        goto out_brand;
    }
    ci->cpu_cores = cpu_cores / smt_siblings;

    /* array of "logical processor -> physical package" mappings */
    int* phy_id = (int*)malloc(cpu_num * sizeof(int));
    if (!phy_id) {
        rv = -PAL_ERROR_NOMEM;
        goto out_brand;
    }

    char filename[128];
    for (int idx = 0; idx < cpu_num; idx++) {
        snprintf(filename, sizeof(filename),
                 "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", idx);
        phy_id[idx] = get_hw_resource(filename, /*count=*/false);
        if (phy_id[idx] < 0) {
            printf("Cannot read %s\n", filename);
            rv = phy_id[idx];
            goto out_phy_id;
        }
    }
    ci->phy_id = (PAL_PTR)phy_id;

    cpuid(1, 0, words);
    ci->cpu_family   = BIT_EXTRACT_LE(words[PAL_CPUID_WORD_EAX], 8, 12);
    ci->cpu_model    = BIT_EXTRACT_LE(words[PAL_CPUID_WORD_EAX], 4, 8);
    ci->cpu_stepping = BIT_EXTRACT_LE(words[PAL_CPUID_WORD_EAX], 0, 4);

    if (!memcmp(vendor_id, "GenuineIntel", 12) || !memcmp(vendor_id, "AuthenticAMD", 12)) {
        ci->cpu_family += BIT_EXTRACT_LE(words[PAL_CPUID_WORD_EAX], 20, 28);
        ci->cpu_model  += BIT_EXTRACT_LE(words[PAL_CPUID_WORD_EAX], 16, 20) << 4;
    }

    int flen = 0, fmax = 80;
    char* flags = malloc(fmax);
    if (!flags) {
        rv = -PAL_ERROR_NOMEM;
        goto out_phy_id;
    }

    for (int i = 0; i < 32; i++) {
        if (!g_cpu_flags[i])
            continue;

        if (BIT_EXTRACT_LE(words[PAL_CPUID_WORD_EDX], i, i + 1)) {
            int len = strlen(g_cpu_flags[i]);
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
        printf("Warning: bogomips could not be retrieved, passing 0.0 to the application\n");
    }

    return rv;
out_flags:
    free(flags);
out_phy_id:
    free(phy_id);
out_brand:
    free(brand);
out_vendor_id:
    free(vendor_id);
    return rv;
}

#if USE_ARCH_RDRAND == 1
int _DkRandomBitsRead(void* buffer, size_t size) {
    uint32_t rand;
    for (size_t i = 0; i < size; i += sizeof(rand)) {
        rand = rdrand();
        memcpy(buffer + i, &rand, MIN(sizeof(rand), size - i));
    }
    return 0;
}
#endif

int _DkSegmentRegisterSet(int reg, const void* addr) {
    int ret = 0;

    if (reg == PAL_SEGMENT_FS) {
        ret = INLINE_SYSCALL(arch_prctl, 2, ARCH_SET_FS, addr);
    } else if (reg == PAL_SEGMENT_GS) {
        return -PAL_ERROR_DENIED;
    } else {
        return -PAL_ERROR_INVAL;
    }
    if (IS_ERR(ret))
        return -PAL_ERROR_DENIED;

    return 0;
}

int _DkSegmentRegisterGet(int reg, void** addr) {
    int ret;
    unsigned long ret_addr;

    if (reg == PAL_SEGMENT_FS) {
        ret = INLINE_SYSCALL(arch_prctl, 2, ARCH_GET_FS, &ret_addr);
    } else if (reg == PAL_SEGMENT_GS) {
        // The GS segment is used for the internal TCB of PAL
        return -PAL_ERROR_DENIED;
    } else {
        return -PAL_ERROR_INVAL;
    }

    if (IS_ERR(ret))
        return -PAL_ERROR_DENIED;

    *addr = (void*)ret_addr;
    return 0;
}

int _DkCpuIdRetrieve(unsigned int leaf, unsigned int subleaf, unsigned int values[4]) {
    cpuid(leaf, subleaf, values);
    return 0;
}
