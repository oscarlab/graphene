/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University
 * Copyright (C) 2020 Intel Corporation
 *                    Michał Kowalczyk <mkow@invisiblethingslab.com>
 * Copyright (C) 2020 Invisible Things Lab
 *                    Michał Kowalczyk <mkow@invisiblethingslab.com>
 */

#define _GNU_SOURCE

/* FIXME: Sorting+re-grouping includes here causes tons of
 * "../../../include/sysdeps/generic/ldsodefs.h:30:32: error: unknown type name ‘Elf__ELF_NATIVE_CLASS_Addr’
 *   #define ElfW(type)       _ElfW(Elf, __ELF_NATIVE_CLASS, type)"
 * errors.
 */
#include "pal_internal-arch.h"
#include "pal_linux_defs.h"
#include "pal_linux_error.h"
#include "pal_rtld.h"
#include "hex.h"
#include "toml.h"
#include "topo_info.h"

#include "debug_map.h"
#include "gdb_integration/sgx_gdb.h"
#include "linux_utils.h"
#include "rpc_queue.h"
#include "sgx_api.h"
#include "sgx_enclave.h"
#include "sgx_internal.h"
#include "sgx_log.h"
#include "sgx_tls.h"

#include <asm/errno.h>
#include <asm/fcntl.h>
#include <asm/socket.h>
#include <ctype.h>
#include <linux/fs.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <sys/auxv.h>
#include <sys/mman.h>

#include "sysdeps/generic/ldsodefs.h"

const size_t g_page_size = PRESET_PAGESIZE;

char* g_pal_loader_path = NULL;
char* g_libpal_path = NULL;

struct pal_enclave g_pal_enclave;

/*
 * FIXME: the ELF-parsing functions in this file (scan_enclave_binary, load_enclave_binary) assume
 * that all the program headers will be found within first FILEBUF_SIZE bytes. This will be true for
 * most binaries, but is not guaranteed.
 *
 * (Glibc also allocates such a buffer but recovers when it's too small, see elf/dl-load.c in glibc
 * sources.)
 */

static int scan_enclave_binary(int fd, unsigned long* base, unsigned long* size,
                               unsigned long* entry) {
    int ret = 0;

    ret = INLINE_SYSCALL(lseek, 3, fd, 0, SEEK_SET);
    if (ret < 0)
        return ret;

    char filebuf[FILEBUF_SIZE];
    ret = INLINE_SYSCALL(read, 3, fd, filebuf, FILEBUF_SIZE);
    if (ret < 0)
        return ret;

    if ((size_t)ret < sizeof(ElfW(Ehdr)))
        return -ENOEXEC;

    const ElfW(Ehdr)* header = (void*)filebuf;
    const ElfW(Phdr)* phdr   = (void*)filebuf + header->e_phoff;
    const ElfW(Phdr)* ph;

    if (memcmp(header->e_ident, ELFMAG, SELFMAG) != 0)
        return -ENOEXEC;

    struct loadcmd {
        ElfW(Addr) mapstart, mapend;
    } loadcmds[16], *c;
    int nloadcmds = 0;

    for (ph = phdr; ph < &phdr[header->e_phnum]; ph++)
        if (ph->p_type == PT_LOAD) {
            if (nloadcmds == 16)
                return -EINVAL;

            c = &loadcmds[nloadcmds++];
            c->mapstart = ALLOC_ALIGN_DOWN(ph->p_vaddr);
            c->mapend   = ALLOC_ALIGN_UP(ph->p_vaddr + ph->p_memsz);
        }

    *base = loadcmds[0].mapstart;
    *size = loadcmds[nloadcmds - 1].mapend - loadcmds[0].mapstart;
    if (entry)
        *entry = header->e_entry;
    return 0;
}

static int load_enclave_binary(sgx_arch_secs_t* secs, int fd, unsigned long base,
                               unsigned long prot) {
    int ret = 0;

    ret = INLINE_SYSCALL(lseek, 3, fd, 0, SEEK_SET);
    if (ret < 0)
        return ret;

    char filebuf[FILEBUF_SIZE];
    ret = INLINE_SYSCALL(read, 3, fd, filebuf, FILEBUF_SIZE);
    if (ret < 0)
        return ret;

    const ElfW(Ehdr)* header = (void*)filebuf;
    const ElfW(Phdr)* phdr   = (void*)filebuf + header->e_phoff;
    const ElfW(Phdr)* ph;

    struct loadcmd {
        ElfW(Addr) mapstart, mapend, datastart, dataend, allocend;
        unsigned int mapoff;
        int prot;
    } loadcmds[16], *c;
    int nloadcmds = 0;

    for (ph = phdr; ph < &phdr[header->e_phnum]; ph++)
        if (ph->p_type == PT_LOAD) {
            if (nloadcmds == 16)
                return -EINVAL;

            c = &loadcmds[nloadcmds++];
            c->mapstart  = ALLOC_ALIGN_DOWN(ph->p_vaddr);
            c->mapend    = ALLOC_ALIGN_UP(ph->p_vaddr + ph->p_filesz);
            c->datastart = ph->p_vaddr;
            c->dataend   = ph->p_vaddr + ph->p_filesz;
            c->allocend  = ph->p_vaddr + ph->p_memsz;
            c->mapoff    = ALLOC_ALIGN_DOWN(ph->p_offset);
            c->prot = (ph->p_flags & PF_R ? PROT_READ : 0) | (ph->p_flags & PF_W ? PROT_WRITE : 0) |
                      (ph->p_flags & PF_X ? PROT_EXEC : 0) | prot;
        }

    base -= loadcmds[0].mapstart;
    for (c = loadcmds; c < &loadcmds[nloadcmds]; c++) {
        ElfW(Addr) zero     = c->dataend;
        ElfW(Addr) zeroend  = ALLOC_ALIGN_UP(c->allocend);
        ElfW(Addr) zeropage = ALLOC_ALIGN_UP(zero);

        if (zeroend < zeropage)
            zeropage = zeroend;

        if (c->mapend > c->mapstart) {
            void* addr = (void*)INLINE_SYSCALL(mmap, 6, NULL, c->mapend - c->mapstart,
                                               PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FILE, fd,
                                               c->mapoff);

            if (IS_ERR_P(addr))
                return -ERRNO_P(addr);

            if (c->datastart > c->mapstart)
                memset(addr, 0, c->datastart - c->mapstart);

            if (zeropage > zero)
                memset(addr + zero - c->mapstart, 0, zeropage - zero);

            ret = add_pages_to_enclave(secs, (void*)base + c->mapstart, addr,
                                       c->mapend - c->mapstart,
                                       SGX_PAGE_REG, c->prot, /*skip_eextend=*/false,
                                       (c->prot & PROT_EXEC) ? "code" : "data");

            INLINE_SYSCALL(munmap, 2, addr, c->mapend - c->mapstart);

            if (ret < 0)
                return ret;
        }

        if (zeroend > zeropage) {
            ret = add_pages_to_enclave(secs, (void*)base + zeropage, NULL, zeroend - zeropage,
                                       SGX_PAGE_REG, c->prot, false, "bss");
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int initialize_enclave(struct pal_enclave* enclave, const char* manifest_to_measure) {
    int ret = 0;
    int enclave_image = -1;
    sgx_arch_token_t enclave_token;
    sgx_arch_enclave_css_t enclave_sigstruct;
    sgx_arch_secs_t enclave_secs;
    unsigned long enclave_entry_addr;
    unsigned long enclave_heap_min;

    int enclave_mem = -1;

    /* this array may overflow the stack, so we allocate it in BSS */
    static void* tcs_addrs[MAX_DBG_THREADS];

    enclave_image = INLINE_SYSCALL(open, 3, enclave->libpal_uri + URI_PREFIX_FILE_LEN, O_RDONLY, 0);
    if (enclave_image < 0) {
        log_error("Cannot find enclave image: %s", enclave->libpal_uri);
        ret = enclave_image;
        goto out;
    }

    if (enclave->nonpie_binary) {
        /* executable is non-PIE: enclave base address must cover code segment loaded at some
         * hardcoded address (usually 0x400000), and heap cannot start at zero (modern OSes do not
         * allow this) */
        enclave->baseaddr = DEFAULT_ENCLAVE_BASE;
        enclave_heap_min  = MMAP_MIN_ADDR;
    } else {
        /* executable is PIE: enclave base address can be arbitrary (we choose it same as
         * enclave_size), and heap can start immediately at this base address */
        enclave->baseaddr = enclave->size;
        enclave_heap_min  = enclave->baseaddr;
    }

    ret = read_enclave_token(enclave->token, &enclave_token);
    if (ret < 0) {
        log_error("Reading enclave token failed: %d", ret);
        goto out;
    }
    enclave->pal_sec.enclave_attributes = enclave_token.body.attributes;

#ifdef DEBUG
    if (enclave->profile_enable) {
        if (!(enclave->pal_sec.enclave_attributes.flags & SGX_FLAGS_DEBUG)) {
            log_error("Cannot use 'sgx.profile' with a production enclave");
            ret = -EINVAL;
            goto out;
        }

        ret = sgx_profile_init();
        if (ret < 0)
            goto out;
    }
#endif

    ret = read_enclave_sigstruct(enclave->sigfile, &enclave_sigstruct);
    if (ret < 0) {
        log_error("Reading enclave sigstruct failed: %d", ret);
        goto out;
    }

    memset(&enclave_secs, 0, sizeof(enclave_secs));
    enclave_secs.base = enclave->baseaddr;
    enclave_secs.size = enclave->size;
    ret = create_enclave(&enclave_secs, &enclave_token);
    if (ret < 0) {
        log_error("Creating enclave failed: %d", ret);
        goto out;
    }

    /* SECS contains SSA frame size in pages, convert to size in bytes */
    enclave->ssa_frame_size = enclave_secs.ssa_frame_size * g_page_size;

    /* Start populating enclave memory */
    struct mem_area {
        const char* desc;
        bool skip_eextend;

        enum {
            ELF_FD, // read from `fd` and parse as ELF
            ZERO,
            BUF,
            TCS,
            TLS
        } data_src;
        union {
            int fd; // valid iff data_src == ELF_FD
            struct { // valid iff data_src == BUF
                const char* buf;
                size_t buf_size;
            };
        };

        unsigned long addr, size, prot;
        enum sgx_page_type type;
    };

    /*
     * 10 for manifest, SSA, TCS, etc
     * + enclave->thread_num for normal stack
     * + enclave->thread_num for signal stack
     */
    int area_num_max = 10 + enclave->thread_num * 2;
    struct mem_area* areas = __alloca(sizeof(areas[0]) * area_num_max);
    int area_num = 0;

    /* The manifest needs to be allocated at the upper end of the enclave
     * memory. That's used by pal_linux_main to find the manifest area. So add
     * it first to the list with memory areas. */
    size_t manifest_size = strlen(manifest_to_measure) + 1;
    areas[area_num] = (struct mem_area){.desc         = "manifest",
                                        .skip_eextend = false,
                                        .data_src     = BUF,
                                        .buf          = manifest_to_measure,
                                        .buf_size     = manifest_size,
                                        .addr         = 0,
                                        .size         = ALLOC_ALIGN_UP(manifest_size),
                                        .prot         = PROT_READ,
                                        .type         = SGX_PAGE_REG};
    area_num++;

    areas[area_num] =
        (struct mem_area){.desc         = "ssa",
                          .skip_eextend = false,
                          .data_src     = ZERO,
                          .addr         = 0,
                          .size         = enclave->thread_num * enclave->ssa_frame_size *
                                              SSA_FRAME_NUM,
                          .prot         = PROT_READ | PROT_WRITE,
                          .type         = SGX_PAGE_REG};
    struct mem_area* ssa_area = &areas[area_num++];

    areas[area_num] = (struct mem_area){.desc = "tcs",
                                        .skip_eextend = false,
                                        .data_src     = TCS,
                                        .addr         = 0,
                                        .size         = enclave->thread_num * g_page_size,
                                        .prot         = PROT_READ | PROT_WRITE,
                                        .type         = SGX_PAGE_TCS};
    struct mem_area* tcs_area = &areas[area_num++];

    areas[area_num] = (struct mem_area){.desc         = "tls",
                                        .skip_eextend = false,
                                        .data_src     = TLS,
                                        .addr         = 0,
                                        .size         = enclave->thread_num * g_page_size,
                                        .prot         = PROT_READ | PROT_WRITE,
                                        .type         = SGX_PAGE_REG};
    struct mem_area* tls_area = &areas[area_num++];

    struct mem_area* stack_areas = &areas[area_num]; /* memorize for later use */
    for (uint32_t t = 0; t < enclave->thread_num; t++) {
        areas[area_num] = (struct mem_area){.desc         = "stack",
                                            .skip_eextend = false,
                                            .data_src     = ZERO,
                                            .addr         = 0,
                                            .size         = ENCLAVE_STACK_SIZE,
                                            .prot         = PROT_READ | PROT_WRITE,
                                            .type         = SGX_PAGE_REG};
        area_num++;
    }

    struct mem_area* sig_stack_areas = &areas[area_num]; /* memorize for later use */
    for (uint32_t t = 0; t < enclave->thread_num; t++) {
        areas[area_num] = (struct mem_area){.desc         = "sig_stack",
                                            .skip_eextend = false,
                                            .data_src     = ZERO,
                                            .addr         = 0,
                                            .size         = ENCLAVE_SIG_STACK_SIZE,
                                            .prot         = PROT_READ | PROT_WRITE,
                                            .type         = SGX_PAGE_REG};
        area_num++;
    }

    areas[area_num] = (struct mem_area){.desc         = "pal",
                                        .skip_eextend = false,
                                        .data_src     = ELF_FD,
                                        .fd           = enclave_image,
                                        /* `addr` and `size` are set below */
                                        .prot         = 0,
                                        .type         = SGX_PAGE_REG};
    struct mem_area* pal_area = &areas[area_num++];

    ret = scan_enclave_binary(enclave_image, &pal_area->addr, &pal_area->size, &enclave_entry_addr);
    if (ret < 0) {
        log_error("Scanning Pal binary (%s) failed: %d", enclave->libpal_uri, ret);
        goto out;
    }

    uintptr_t last_populated_addr = enclave->baseaddr + enclave->size;
    for (int i = 0; i < area_num; i++) {
        if (areas[i].addr)
            continue;
        areas[i].addr = last_populated_addr - areas[i].size;
        last_populated_addr = areas[i].addr;
    }

    enclave_entry_addr += pal_area->addr;

    if (last_populated_addr > enclave_heap_min) {
        areas[area_num] = (struct mem_area){.desc         = "free",
                                            .skip_eextend = true,
                                            .data_src     = ZERO,
                                            .addr         = enclave_heap_min,
                                            .size         = last_populated_addr - enclave_heap_min,
                                            .prot         = PROT_READ | PROT_WRITE | PROT_EXEC,
                                            .type         = SGX_PAGE_REG};
        area_num++;
    }

    for (int i = 0; i < area_num; i++) {
        if (areas[i].data_src == ELF_FD) {
            ret = load_enclave_binary(&enclave_secs, areas[i].fd, areas[i].addr, areas[i].prot);
            if (ret < 0) {
                log_error("Loading enclave binary failed: %d", ret);
                goto out;
            }
            continue;
        }

        void* data = NULL;
        if (areas[i].data_src != ZERO) {
            data = (void*)INLINE_SYSCALL(mmap, 6, NULL, areas[i].size, PROT_READ | PROT_WRITE,
                                         MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            if (IS_ERR_P(data) || data == NULL) {
                /* Note that Graphene currently doesn't handle 0x0 addresses */
                log_error("Allocating memory failed");
                ret = -ENOMEM;
                goto out;
            }
        }

        if (areas[i].data_src == TLS) {
            for (uint32_t t = 0; t < enclave->thread_num; t++) {
                struct enclave_tls* gs = data + g_page_size * t;
                memset(gs, 0, g_page_size);
                assert(sizeof(*gs) <= g_page_size);
                gs->common.self = (PAL_TCB*)(tls_area->addr + g_page_size * t);
                gs->common.stack_protector_canary = STACK_PROTECTOR_CANARY_DEFAULT;
                gs->enclave_size = enclave->size;
                gs->tcs_offset = tcs_area->addr - enclave->baseaddr + g_page_size * t;
                gs->initial_stack_addr = stack_areas[t].addr + ENCLAVE_STACK_SIZE;
                gs->sig_stack_low = sig_stack_areas[t].addr;
                gs->sig_stack_high = sig_stack_areas[t].addr + ENCLAVE_SIG_STACK_SIZE;
                gs->ssa = (void*)ssa_area->addr + enclave->ssa_frame_size * SSA_FRAME_NUM * t;
                gs->gpr = gs->ssa + enclave->ssa_frame_size - sizeof(sgx_pal_gpr_t);
                gs->manifest_size = manifest_size;
                gs->heap_min = (void*)enclave_heap_min;
                gs->heap_max = (void*)pal_area->addr;
                gs->thread = NULL;
            }
        } else if (areas[i].data_src == TCS) {
            for (uint32_t t = 0; t < enclave->thread_num; t++) {
                sgx_arch_tcs_t* tcs = data + g_page_size * t;
                memset(tcs, 0, g_page_size);
                // .ossa, .oentry, .ofs_base and .ogs_base are offsets from enclave base, not VAs.
                tcs->ossa      = ssa_area->addr - enclave->baseaddr
                                 + enclave->ssa_frame_size * SSA_FRAME_NUM * t;
                tcs->nssa      = SSA_FRAME_NUM;
                tcs->oentry    = enclave_entry_addr - enclave->baseaddr;
                tcs->ofs_base  = 0;
                tcs->ogs_base  = tls_area->addr - enclave->baseaddr + t * g_page_size;
                tcs->ofs_limit = 0xfff;
                tcs->ogs_limit = 0xfff;
                tcs_addrs[t] = (void*)tcs_area->addr + g_page_size * t;
            }
        } else if (areas[i].data_src == BUF) {
            memcpy(data, areas[i].buf, areas[i].buf_size);
        } else {
            assert(areas[i].data_src == ZERO);
        }

        ret = add_pages_to_enclave(&enclave_secs, (void*)areas[i].addr, data, areas[i].size,
                                   areas[i].type, areas[i].prot, areas[i].skip_eextend,
                                   areas[i].desc);

        if (data)
            INLINE_SYSCALL(munmap, 2, data, areas[i].size);

        if (ret < 0) {
            log_error("Adding pages (%s) to enclave failed: %d", areas[i].desc, ret);
            goto out;
        }
    }

    ret = init_enclave(&enclave_secs, &enclave_sigstruct, &enclave_token);
    if (ret < 0) {
        log_error("Initializing enclave failed: %d", ret);
        goto out;
    }

    create_tcs_mapper((void*)tcs_area->addr, enclave->thread_num);

    struct enclave_dbginfo* dbg = (void*)INLINE_SYSCALL(
        mmap, 6, DBGINFO_ADDR, sizeof(struct enclave_dbginfo), PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (IS_ERR_P(dbg)) {
        log_warning("Cannot allocate debug information (GDB will not work)");
    } else {
        dbg->pid            = INLINE_SYSCALL(getpid, 0);
        dbg->base           = enclave->baseaddr;
        dbg->size           = enclave->size;
        dbg->ssa_frame_size = enclave->ssa_frame_size;
        dbg->aep            = async_exit_pointer;
        dbg->eresume        = eresume_pointer;
        dbg->thread_tids[0] = dbg->pid;
        for (int i = 0; i < MAX_DBG_THREADS; i++)
            dbg->tcs_addrs[i] = tcs_addrs[i];
    }

    if (g_sgx_enable_stats) {
        /* set TCS.FLAGS.DBGOPTIN in all enclave threads to enable perf counters, Intel PT, etc */
        ret = INLINE_SYSCALL(open, 3, "/proc/self/mem", O_RDWR | O_LARGEFILE, 0);
        if (ret < 0) {
            log_error("Setting TCS.FLAGS.DBGOPTIN failed: %d", ret);
            goto out;
        }
        enclave_mem = ret;

        for (size_t i = 0; i < enclave->thread_num; i++) {
            uint64_t tcs_flags;
            uint64_t* tcs_flags_ptr = tcs_addrs[i] + offsetof(sgx_arch_tcs_t, flags);

            ret = INLINE_SYSCALL(pread, 4, enclave_mem, &tcs_flags, sizeof(tcs_flags),
                                 (off_t)tcs_flags_ptr);
            if (ret < 0) {
                log_error("Reading TCS.FLAGS.DBGOPTIN failed: %d", ret);
                goto out;
            }

            tcs_flags |= TCS_FLAGS_DBGOPTIN;

            ret = INLINE_SYSCALL(pwrite, 4, enclave_mem, &tcs_flags, sizeof(tcs_flags),
                                 (off_t)tcs_flags_ptr);
            if (ret < 0) {
                log_error("Writing TCS.FLAGS.DBGOPTIN failed: %d", ret);
                goto out;
            }
        }
    }

#ifdef DEBUG
    /*
     * Report libpal map. All subsequent files will be reported via DkDebugMapAdd(), but this
     * one has to be handled separately.
     *
     * We report it here, before enclave start (as opposed to setup_pal_map()), because we want both
     * GDB integration and profiling to be active from the very beginning of enclave execution.
     */

    debug_map_add(enclave->libpal_uri + URI_PREFIX_FILE_LEN, (void*)pal_area->addr);
    sgx_profile_report_elf(enclave->libpal_uri + URI_PREFIX_FILE_LEN, (void*)pal_area->addr);

    /* Report outer PAL maps to profiler, so that we can record samples pointing to outer PAL. */
    sgx_profile_report_urts_elfs();
#endif

    ret = 0;

out:
    if (enclave_image >= 0)
        INLINE_SYSCALL(close, 1, enclave_image);
    if (enclave_mem >= 0)
        INLINE_SYSCALL(close, 1, enclave_mem);

    return ret;
}

static void create_instance(struct pal_sec* pal_sec) {
    PAL_NUM id = ((uint64_t)rdrand() << 32) | rdrand();
    snprintf(pal_sec->pipe_prefix, sizeof(pal_sec->pipe_prefix), "/graphene/%016lx/", id);
    pal_sec->instance_id = id;
}

/* Parses only the information needed by the untrusted PAL to correctly initialize the enclave. */
static int parse_loader_config(char* manifest, struct pal_enclave* enclave_info) {
    int ret = 0;
    toml_table_t* manifest_root = NULL;
    char* sgx_ra_client_spid_str = NULL;

    char errbuf[256];
    manifest_root = toml_parse(manifest, errbuf, sizeof(errbuf));
    if (!manifest_root) {
        log_error(
            "PAL failed at parsing the manifest: %s\n"
            "  Graphene switched to the TOML format recently, please update the manifest\n"
            "  (in particular, string values must be put in double quotes)", errbuf);
        ret = -EINVAL;
        goto out;
    }

    ret = toml_sizestring_in(manifest_root, "sgx.enclave_size", /*defaultval=*/0,
                             &enclave_info->size);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.enclave_size' (the value must be put in double quotes!)");
        ret = -EINVAL;
        goto out;
    }

    if (!enclave_info->size || !IS_POWER_OF_2(enclave_info->size)) {
        log_error("Enclave size not a power of two (an SGX-imposed requirement)");
        ret = -EINVAL;
        goto out;
    }

    int64_t thread_num_int64;
    ret = toml_int_in(manifest_root, "sgx.thread_num", /*defaultval=*/0, &thread_num_int64);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.thread_num'");
        ret = -EINVAL;
        goto out;
    }

    if (thread_num_int64 < 0) {
        log_error("Negative 'sgx.thread_num' is impossible");
        ret = -EINVAL;
        goto out;
    }

    enclave_info->thread_num = thread_num_int64;

    if (!enclave_info->thread_num) {
        log_warning("Number of enclave threads ('sgx.thread_num') is not specified; assumed "
                    "to be 1");
        enclave_info->thread_num = 1;
    }

    if (enclave_info->thread_num > MAX_DBG_THREADS) {
        log_error("Too large 'sgx.thread_num', maximum allowed is %d", MAX_DBG_THREADS);
        ret = -EINVAL;
        goto out;
    }

    int64_t rpc_thread_num_int64;
    ret = toml_int_in(manifest_root, "sgx.rpc_thread_num", /*defaultval=*/0, &rpc_thread_num_int64);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.rpc_thread_num'");
        ret = -EINVAL;
        goto out;
    }

    if (rpc_thread_num_int64 < 0) {
        log_error("Negative 'sgx.rpc_thread_num' is impossible");
        ret = -EINVAL;
        goto out;
    }

    enclave_info->rpc_thread_num = rpc_thread_num_int64;

    if (enclave_info->rpc_thread_num > MAX_RPC_THREADS) {
        log_error("Too large 'sgx.rpc_thread_num', maximum allowed is %d", MAX_RPC_THREADS);
        ret = -EINVAL;
        goto out;
    }

    if (enclave_info->rpc_thread_num && enclave_info->thread_num > RPC_QUEUE_SIZE) {
        log_error("Too many threads for exitless feature (more than capacity of RPC queue)");
        ret = -EINVAL;
        goto out;
    }

    bool nonpie_binary;
    ret = toml_bool_in(manifest_root, "sgx.nonpie_binary", /*defaultval=*/false, &nonpie_binary);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.nonpie_binary' (the value must be `true` or `false`)");
        ret = -EINVAL;
        goto out;
    }
    enclave_info->nonpie_binary = nonpie_binary;

    ret = toml_bool_in(manifest_root, "sgx.enable_stats", /*defaultval=*/false,
                       &g_sgx_enable_stats);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.enable_stats' (the value must be `true` or `false`)");
        ret = -EINVAL;
        goto out;
    }

    char* dummy_sigfile_str = NULL;
    ret = toml_string_in(manifest_root, "sgx.sigfile", &dummy_sigfile_str);
    if (ret < 0 || dummy_sigfile_str) {
        log_error("sgx.sigfile is not supported anymore. Please update your manifest according to "
                  "the current documentation.");
        ret = -EINVAL;
        goto out;
    }
    free(dummy_sigfile_str);

    bool sgx_remote_attestation_enabled;
    ret = toml_bool_in(manifest_root, "sgx.remote_attestation", /*defaultval=*/false,
                       &sgx_remote_attestation_enabled);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.remote_attestation' (the value must be `true` or `false`)");
        ret = -EINVAL;
        goto out;
    }
    enclave_info->remote_attestation_enabled = sgx_remote_attestation_enabled;

    ret = toml_string_in(manifest_root, "sgx.ra_client_spid", &sgx_ra_client_spid_str);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.ra_client_spid' (the value must be put in double quotes!)");
        ret = -EINVAL;
        goto out;
    }

    bool sgx_ra_client_linkable_specified =
        toml_key_exists(manifest_root, "sgx.ra_client_linkable");

    if (!enclave_info->remote_attestation_enabled &&
            (sgx_ra_client_spid_str || sgx_ra_client_linkable_specified)) {
        log_error(
            "Detected EPID remote attestation parameters 'ra_client_spid' and/or "
            "'ra_client_linkable' in the manifest but no 'remote_attestation' parameter. "
            "Please add 'sgx.remote_attestation = true' to the manifest.");
        ret = -EINVAL;
        goto out;
    }

    /* EPID is used if SPID is a non-empty string in manifest, otherwise DCAP/ECDSA */
    enclave_info->use_epid_attestation = sgx_ra_client_spid_str && strlen(sgx_ra_client_spid_str);

    char* profile_str = NULL;
    ret = toml_string_in(manifest_root, "sgx.profile.enable", &profile_str);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.profile.enable' "
                  "(the value must be \"none\", \"main\" or \"all\")");
        ret = -EINVAL;
        goto out;
    }

#ifdef DEBUG
    enclave_info->profile_enable = false;
    enclave_info->profile_filename[0] = '\0';

    if (!profile_str || !strcmp(profile_str, "none")) {
        // do not enable
    } else if (!strcmp(profile_str, "main")) {
        if (enclave_info->is_first_process) {
            snprintf(enclave_info->profile_filename, ARRAY_SIZE(enclave_info->profile_filename),
                     SGX_PROFILE_FILENAME);
            enclave_info->profile_enable = true;
        }
    } else if (!strcmp(profile_str, "all")) {
        enclave_info->profile_enable = true;
        snprintf(enclave_info->profile_filename, ARRAY_SIZE(enclave_info->profile_filename),
                 SGX_PROFILE_FILENAME_WITH_PID, (int)INLINE_SYSCALL(getpid, 0));
    } else {
        log_error("Invalid 'sgx.profile.enable' "
                  "(the value must be \"none\", \"main\" or \"all\")");
        ret = -EINVAL;
        goto out;
    }

    char* profile_mode_str = NULL;
    ret = toml_string_in(manifest_root, "sgx.profile.mode", &profile_mode_str);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.profile.mode' "
                  "(the value must be \"aex\", \"ocall_inner\" or \"ocall_outer\")");
        ret = -EINVAL;
        goto out;
    }
    if (!profile_mode_str) {
        enclave_info->profile_mode = SGX_PROFILE_MODE_AEX;
    } else if (!strcmp(profile_mode_str, "aex")) {
        enclave_info->profile_mode = SGX_PROFILE_MODE_AEX;
    } else if (!strcmp(profile_mode_str, "ocall_inner")) {
        enclave_info->profile_mode = SGX_PROFILE_MODE_OCALL_INNER;
    } else if (!strcmp(profile_mode_str, "ocall_outer")) {
        enclave_info->profile_mode = SGX_PROFILE_MODE_OCALL_OUTER;
    } else {
        log_error("Invalid 'sgx.profile.mode' "
                  "(the value must be \"aex\", \"ocall_inner\" or \"ocall_outer\")");
        ret = -EINVAL;
        goto out;
    }

    bool profile_with_stack;
    ret = toml_bool_in(manifest_root, "sgx.profile.with_stack", /*defaultval=*/false,
                       &profile_with_stack);
    if (ret < 0) {
        log_error("Cannot parse 'sgx.profile.with_stack' (the value must be `true` or `false`)");
        ret = -EINVAL;
        goto out;
    }
    enclave_info->profile_with_stack = profile_with_stack;

    if (enclave_info->profile_with_stack &&
        enclave_info->profile_mode == SGX_PROFILE_MODE_OCALL_OUTER) {

        log_error("Invalid 'sgx.profile.mode' and 'sgx.profile.with_stack' combination "
                  "(\"ocall_outer\" mode cannot be used with stack)");
        ret = -EINVAL;
        goto out;
    }

    int64_t profile_frequency;
    ret = toml_int_in(manifest_root, "sgx.profile.frequency", SGX_PROFILE_DEFAULT_FREQUENCY,
                      &profile_frequency);
    if (ret < 0 || !(0 < profile_frequency && profile_frequency <= SGX_PROFILE_MAX_FREQUENCY)) {
        log_error("Cannot parse 'sgx.profile.frequency' (the value must be between 1 and %d)",
                  SGX_PROFILE_MAX_FREQUENCY);
        ret = -EINVAL;
        goto out;
    }
    enclave_info->profile_frequency = profile_frequency;
#else
    if (profile_str && strcmp(profile_str, "none")) {
        log_error("Invalid 'sgx.profile.enable' "
                  "(SGX profiling works only when Graphene is compiled with DEBUG=1)");
        ret = -EINVAL;
        goto out;
    }
#endif

    int log_level = PAL_LOG_DEFAULT_LEVEL;
    char* log_level_str = NULL;
    ret = toml_string_in(manifest_root, "loader.log_level", &log_level_str);
    if (ret < 0) {
        log_error("Cannot parse 'loader.log_level'");
        ret = -EINVAL;
        goto out;
    }
    if (log_level_str) {
        if (!strcmp(log_level_str, "none")) {
            log_level = LOG_LEVEL_NONE;
        } else if (!strcmp(log_level_str, "error")) {
            log_level = LOG_LEVEL_ERROR;
        } else if (!strcmp(log_level_str, "warning")) {
            log_level = LOG_LEVEL_WARNING;
        } else if (!strcmp(log_level_str, "debug")) {
            log_level = LOG_LEVEL_DEBUG;
        } else if (!strcmp(log_level_str, "trace")) {
            log_level = LOG_LEVEL_TRACE;
        } else if (!strcmp(log_level_str, "all")) {
            log_level = LOG_LEVEL_ALL;
        } else {
            log_error("Unknown 'loader.log_level'");
            ret = -EINVAL;
            goto out;
        }
    }
    free(log_level_str);

    char* log_file = NULL;
    ret = toml_string_in(manifest_root, "loader.log_file", &log_file);
    if (ret < 0) {
        log_error("Cannot parse 'loader.log_file'");
        ret = -EINVAL;
        goto out;
    }
    if (log_level > LOG_LEVEL_NONE && log_file) {
        ret = urts_log_init(log_file);

        if (ret < 0) {
            log_error("Cannot open log file: %d", ret);
            goto out;
        }
    }
    free(log_file);

    g_urts_log_level = log_level;

    ret = 0;

out:
    free(sgx_ra_client_spid_str);
    toml_free(manifest_root);
    return ret;
}

/* Warning: This function does not free up resources on failure - it assumes that the whole process
 * exits after this function's failure. */
static int load_enclave(struct pal_enclave* enclave, char* args, size_t args_size, char* env,
                        size_t env_size, bool need_gsgx) {
    int ret;
    struct timeval tv;
    struct pal_sec* pal_sec = &enclave->pal_sec;

    uint64_t start_time;
    INLINE_SYSCALL(gettimeofday, 2, &tv, NULL);
    start_time = tv.tv_sec * 1000000UL + tv.tv_usec;

    ret = parse_loader_config(enclave->raw_manifest_data, enclave);
    if (ret < 0) {
        log_error("Parsing manifest failed");
        return -EINVAL;
    }

    ret = open_sgx_driver(need_gsgx);
    if (ret < 0)
        return ret;

    if (!is_wrfsbase_supported())
        return -EPERM;

    pal_sec->pid = INLINE_SYSCALL(getpid, 0);
    pal_sec->uid = INLINE_SYSCALL(getuid, 0);
    pal_sec->gid = INLINE_SYSCALL(getgid, 0);

    /* we cannot use CPUID(0xb) because it counts even disabled-by-BIOS cores (e.g. HT cores);
     * instead extract info on total number of logical cores, number of physical cores,
     * SMT support etc. by parsing sysfs pseudo-files */
    int online_logical_cores = get_hw_resource("/sys/devices/system/cpu/online", /*count=*/true);
    if (online_logical_cores < 0)
        return online_logical_cores;
    pal_sec->online_logical_cores = online_logical_cores;

    int possible_logical_cores = get_hw_resource("/sys/devices/system/cpu/possible",
                                                 /*count=*/true);
    if (possible_logical_cores < 0)
        return possible_logical_cores;
    pal_sec->possible_logical_cores = possible_logical_cores;

    /* TODO: correctly support offline cores */
    if (possible_logical_cores > 0 && possible_logical_cores > online_logical_cores) {
         log_warning("some CPUs seem to be offline; Graphene doesn't take this into account "
                     "which may lead to subpar performance");
    }

    int core_siblings = get_hw_resource("/sys/devices/system/cpu/cpu0/topology/core_siblings_list",
                                        /*count=*/true);
    if (core_siblings < 0)
        return core_siblings;

    int smt_siblings = get_hw_resource("/sys/devices/system/cpu/cpu0/topology/thread_siblings_list",
                                       /*count=*/true);
    if (smt_siblings < 0)
        return smt_siblings;
    pal_sec->physical_cores_per_socket = core_siblings / smt_siblings;

    /* array of "logical core -> socket" mappings */
    int* cpu_socket = (int*)malloc(online_logical_cores * sizeof(int));
    if (!cpu_socket)
        return -ENOMEM;

    char filename[128];
    for (int idx = 0; idx < online_logical_cores; idx++) {
        snprintf(filename, sizeof(filename),
                 "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", idx);
        cpu_socket[idx] = get_hw_resource(filename, /*count=*/false);
        if (cpu_socket[idx] < 0) {
            log_error("Cannot read %s", filename);
            ret = cpu_socket[idx];
            free(cpu_socket);
            return ret;
        }
    }
    pal_sec->cpu_socket = cpu_socket;

    ret = get_topology_info(&pal_sec->topo_info);
    if (ret < 0)
        return ret;

#ifdef DEBUG
    size_t env_i = 0;
    while (env_i < env_size) {
        if (!strcmp(&env[env_i], "IN_GDB=1")) {
            log_warning("[ Running under GDB ]");
            pal_sec->in_gdb = true;
        }

        env_i += strnlen(&env[env_i], env_size - env_i) + 1;
    }
#endif

    enclave->libpal_uri = alloc_concat(URI_PREFIX_FILE, URI_PREFIX_FILE_LEN, g_libpal_path, -1);
    if (!enclave->libpal_uri) {
        log_error("Out of memory for enclave->libpal_uri");
        return -ENOMEM;
    }

    if (enclave->libpal_uri[URI_PREFIX_FILE_LEN] != '/') {
        log_error("Path to in-enclave PAL (%s) must be absolute", enclave->libpal_uri);
        return -EINVAL;
    }

    char* sig_path = alloc_concat(g_pal_enclave.application_path, -1, ".sig", -1);
    if (!sig_path) {
        return -ENOMEM;
    }

    enclave->sigfile = INLINE_SYSCALL(open, 3, sig_path, O_RDONLY | O_CLOEXEC, 0);
    if (enclave->sigfile < 0) {
        log_error("Cannot open sigstruct file %s", sig_path);
        return -EINVAL;
    }
    free(sig_path);

    char* token_path = alloc_concat(g_pal_enclave.application_path, -1, ".token", -1);
    if (!token_path) {
        return -ENOMEM;
    }

    enclave->token = INLINE_SYSCALL(open, 3, token_path, O_RDONLY | O_CLOEXEC, 0);
    if (enclave->token < 0) {
        log_error(
            "Cannot open token %s. Use graphene-sgx-get-token on the runtime host or run "
            "`make SGX=1 sgx-tokens` in the Graphene source to create the token file.",
            token_path);
        return -EINVAL;
    }
    log_debug("Token file: %s", token_path);
    free(token_path);

    ret = initialize_enclave(enclave, enclave->raw_manifest_data);
    if (ret < 0)
        return ret;

    if (!pal_sec->instance_id)
        create_instance(&enclave->pal_sec);

    ret = sgx_signal_setup();
    if (ret < 0)
        return ret;

    if (enclave->remote_attestation_enabled) {
        /* initialize communication with Quoting Enclave only if app requests attestation */
        bool is_epid = enclave->use_epid_attestation;
        log_debug("Using SGX %s attestation", is_epid ? "EPID" : "DCAP/ECDSA");
        ret = init_quoting_enclave_targetinfo(is_epid, &pal_sec->qe_targetinfo);
        if (ret < 0)
            return ret;
    }

    void* alt_stack = (void*)INLINE_SYSCALL(mmap, 6, NULL, ALT_STACK_SIZE, PROT_READ | PROT_WRITE,
                                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (IS_ERR_P(alt_stack))
        return -ENOMEM;

    /* initialize TCB at the top of the alternative stack */
    PAL_TCB_URTS* tcb = alt_stack + ALT_STACK_SIZE - sizeof(PAL_TCB_URTS);
    pal_tcb_urts_init(tcb, /*stack=*/NULL,
                      alt_stack); /* main thread uses the stack provided by Linux */
    ret = pal_thread_init(tcb);
    if (ret < 0)
        return ret;

    uint64_t end_time;
    INLINE_SYSCALL(gettimeofday, 2, &tv, NULL);
    end_time = tv.tv_sec * 1000000UL + tv.tv_usec;

    if (g_sgx_enable_stats) {
        /* This shows the time for Graphene + the Intel SGX driver to initialize the untrusted
         * PAL, config and create the SGX enclave, add enclave pages, measure and init it.
         */
        log_always("----- SGX enclave loading time = %10lu microseconds -----",
                   end_time - start_time);
    }

    /* start running trusted PAL */
    ecall_enclave_start(enclave->libpal_uri, args, args_size, env, env_size);

    unmap_tcs();
    INLINE_SYSCALL(munmap, 2, alt_stack, ALT_STACK_SIZE);
    INLINE_SYSCALL(exit, 1, 0);
    die_or_inf_loop();
}

/* Grow the stack of the main thread to THREAD_STACK_SIZE by probing each stack page above current
 * stack pointer (Linux dynamically grows the stack of the main thread but gets confused with
 * huge-jump stack accesses coming from within the enclave). Note that other, non-main threads
 * are created manually via clone(.., THREAD_STACK_SIZE, ..) and thus do not need this hack. */
static void force_linux_to_grow_stack(void) {
    ARCH_PROBE_STACK(THREAD_STACK_SIZE, PRESET_PAGESIZE);
}

noreturn static void print_usage_and_exit(const char* argv_0) {
    const char* self = argv_0 ?: "<this program>";
    log_always("USAGE:\n"
               "\tFirst process: %s <path to libpal.so> init <application> args...\n"
               "\tChildren:      %s <path to libpal.so> child <parent_pipe_fd> args...",
               self, self);
    log_always("This is an internal interface. Use pal_loader to launch applications in "
               "Graphene.");
    INLINE_SYSCALL(exit_group, 1, 1);
    die_or_inf_loop();
}

int main(int argc, char* argv[], char* envp[]) {
    char* manifest_path = NULL;
    int ret = 0;
    bool need_gsgx = true;
    char* manifest = NULL;

    force_linux_to_grow_stack();

    if (argc < 4)
        print_usage_and_exit(argv[0]);

    g_pal_loader_path = get_main_exec_path();
    if (!g_pal_loader_path) {
        return -ENOMEM;
    }

    /* check whether host kernel supports FSGSBASE feature, otherwise we need the GSGX driver */
    if (getauxval(AT_HWCAP2) & 0x2) {
        need_gsgx = false;
    }

    g_libpal_path = strdup(argv[1]);
    if (!g_libpal_path) {
        return -ENOMEM;
    }

    // Are we the first in this Graphene's namespace?
    bool first_process = !strcmp(argv[2], "init");
    if (!first_process && strcmp(argv[2], "child")) {
        print_usage_and_exit(argv[0]);
    }

    if (first_process) {
        g_pal_enclave.is_first_process = true;

        g_pal_enclave.application_path = argv[3];
        manifest_path = alloc_concat(g_pal_enclave.application_path, -1, ".manifest.sgx", -1);
        if (!manifest_path) {
            return -ENOMEM;
        }

        log_debug("Manifest file: %s", manifest_path);
        ret = read_text_file_to_cstr(manifest_path, &manifest);
        if (ret < 0) {
            log_error("Reading manifest failed");
            return ret;
        }
        free(manifest_path);
        manifest_path = NULL;
    } else {
        /* We're one of the children spawned to host new processes started inside Graphene. */
        g_pal_enclave.is_first_process = false;

        /* We'll receive our argv and config via IPC. */
        int parent_pipe_fd = atoi(argv[3]);
        ret = sgx_init_child_process(parent_pipe_fd, &g_pal_enclave.pal_sec,
                                     &g_pal_enclave.application_path, &manifest);
        if (ret < 0)
            return ret;
    }
    g_pal_enclave.raw_manifest_data = manifest;

    /*
     * While C does not guarantee that the argv[i] and envp[i] strings are
     * continuous we know that we are running on Linux, which does this. This
     * saves us creating a copy of all argv and envp strings.
     */
    char* args;
    size_t args_size;
    if (first_process) {
        args = argv[3];
        args_size = argc > 3 ? (argv[argc - 1] - args) + strlen(argv[argc - 1]) + 1 : 0;
    } else {
        args = argv[4];
        args_size = argc > 4 ? (argv[argc - 1] - args) + strlen(argv[argc - 1]) + 1 : 0;
    }

    size_t envc = 0;
    while (envp[envc] != NULL) {
        envc++;
    }
    char* env = envp[0];
    size_t env_size = envc > 0 ? (envp[envc - 1] - envp[0]) + strlen(envp[envc - 1]) + 1 : 0;

    ret = load_enclave(&g_pal_enclave, args, args_size, env, env_size, need_gsgx);
    if (ret < 0) {
        log_error("load_enclave() failed with error %d", ret);
    }
    return 0;
}
