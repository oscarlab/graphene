/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

#ifndef PAL_LINUX_H
#define PAL_LINUX_H

#include <asm/mman.h>
#include <linux/mman.h>

#include "api.h"
#include "assert.h"
#include "crypto.h"
#include "enclave_ocalls.h"
#include "linux_types.h"
#include "log.h"
#include "pal.h"
#include "pal_defs.h"
#include "pal_internal.h"
#include "pal_linux_defs.h"
#include "protected_files.h"
#include "sgx_api.h"
#include "sgx_arch.h"
#include "sgx_attest.h"
#include "sgx_tls.h"
#include "sysdep-arch.h"

#define IS_ERR_P    INTERNAL_SYSCALL_ERROR_P
#define ERRNO_P     INTERNAL_SYSCALL_ERRNO_P
#define IS_UNIX_ERR INTERNAL_SYSCALL_ERRNO_RANGE

extern struct pal_linux_state {
    PAL_NUM parent_process_id;
    PAL_NUM process_id;

    const char** host_environ;

    /* credentials */
    unsigned int uid, gid;

    /* currently enabled signals */
    __sigset_t sigset;
    __sigset_t blocked_signals;

    /* enclave */
    const char* runtime_dir;
} g_linux_state;

#define DEFAULT_BACKLOG 2048

#define ACCESS_R 4
#define ACCESS_W 2
#define ACCESS_X 1

struct stat;
bool stataccess(struct stat* stats, int acc);

int init_child_process(PAL_HANDLE* parent);

#ifdef IN_ENCLAVE

extern const size_t g_page_size;
extern size_t g_pal_internal_mem_size;

struct pal_sec;
noreturn void pal_linux_main(char* uptr_libpal_uri, size_t libpal_uri_len, char* uptr_args,
                             size_t args_size, char* uptr_env, size_t env_size,
                             struct pal_sec* uptr_sec_info);
void pal_start_thread(void);

struct link_map;
void setup_pal_map(struct link_map* map);

extern char __text_start, __text_end, __data_start, __data_end;
#define TEXT_START ((void*)(&__text_start))
#define TEXT_END   ((void*)(&__text_end))
#define DATA_START ((void*)(&__data_start))
#define DATA_END   ((void*)(&__data_end))

typedef struct {
    uint8_t bytes[32];
} sgx_file_hash_t;
typedef struct {
    uint8_t bytes[16];
} sgx_chunk_hash_t;

extern int g_xsave_enabled;
extern uint64_t g_xsave_features;
extern uint32_t g_xsave_size;
#define XSAVE_RESET_STATE_SIZE (512 + 64)  // 512 for legacy regs, 64 for xsave header
extern const uint32_t g_xsave_reset_state[];

void init_xsave_size(uint64_t xfrm);
void save_xregs(PAL_XREGS_STATE* xsave_area);
void restore_xregs(const PAL_XREGS_STATE* xsave_area);
noreturn void _restore_sgx_context(sgx_cpu_context_t* uc, PAL_XREGS_STATE* xsave_area);

void _DkExceptionHandler(unsigned int exit_info, sgx_cpu_context_t* uc,
                         PAL_XREGS_STATE* xregs_state);
void _DkHandleExternalEvent(PAL_NUM event, sgx_cpu_context_t* uc, PAL_XREGS_STATE* xregs_state);

int init_trusted_files(void);
void init_cpuid(void);

bool is_tsc_usable(void);
uint64_t get_tsc_hz(void);
void init_tsc(void);

/*!
 * \brief check if the file to be opened is trusted or allowed, according to the manifest
 *
 * \param file              file handle to be opened
 * \param chunk_hashes_ptr  array of hashes over file chunks
 * \param size_ptr          returns size of opened file
 * \param create            whether this file is newly created
 * \param umem              untrusted memory address at which the file is loaded
 *
 * \return 0 on success, negative error code on failure
 */
int load_trusted_file(PAL_HANDLE file, sgx_chunk_hash_t** chunk_hashes_ptr, uint64_t* size_ptr,
                      int create, void** umem);

enum {
    FILE_CHECK_POLICY_STRICT = 0,
    FILE_CHECK_POLICY_ALLOW_ALL_BUT_LOG,
};

int init_file_check_policy(void);

int get_file_check_policy(void);

/*!
 * \brief Copy and check file contents from untrusted outside buffer to in-enclave buffer
 *
 * \param path            file path (currently only for a log message)
 * \param buf             in-enclave buffer where contents of the file are copied
 * \param umem            start of untrusted file memory mapped outside the enclave
 * \param aligned_offset  offset into file contents to copy, aligned to TRUSTED_CHUNK_SIZE
 * \param aligned_end     end of file contents to copy, aligned to TRUSTED_CHUNK_SIZE
 * \param offset          unaligned offset into file contents to copy
 * \param end             unaligned end of file contents to copy
 * \param chunk_hashes    array of hashes of all file chunks
 * \param file_size       total size of the file
 *
 * \return 0 on success, negative error code on failure
 *
 * If needed, regions at either the beginning or the end of the copied regions are copied into a
 * scratch buffer to avoid a TOCTTOU race. This is done to avoid the following TOCTTOU race
 * condition with the untrusted host as an adversary:
 *       *  Adversary: put good contents in buffer
 *       *  Enclave: buffer check passes
 *       *  Adversary: put bad contents in buffer
 *       *  Enclave: copies in bad buffer contents
 */
int copy_and_verify_trusted_file(const char* path, uint8_t* buf, const void* umem,
                                 off_t aligned_offset, off_t aligned_end, off_t offset, off_t end,
                                 sgx_chunk_hash_t* chunk_hashes, size_t file_size);

int register_trusted_child(const char* uri, const char* mr_enclave_str);

int init_enclave(void);
void init_untrusted_slab_mgr(void);

/* Used to track map buffers for protected files */
DEFINE_LIST(pf_map);
struct pf_map {
    LIST_TYPE(pf_map) list;
    struct protected_file* pf;
    void* buffer;
    uint64_t size;
    uint64_t offset; /* offset in PF, needed for write buffers when flushing to the PF */
};
DEFINE_LISTP(pf_map);

/* List of PF map buffers; this list is traversed on PF flush (on file close) */
extern LISTP_TYPE(pf_map) g_pf_map_list;

enum {
    PROTECTED_FILE_KEY_WRAP = 0,
    PROTECTED_FILE_KEY_MRENCLAVE,
    PROTECTED_FILE_KEY_MRSIGNER,
};

/* Data of a protected file */
struct protected_file {
    UT_hash_handle hh;
    size_t path_len;
    char* path;
    pf_context_t* context; /* NULL until PF is opened */
    int64_t refcount; /* used for deciding when to call unload_protected_file() */
    int writable_fd; /* fd of underlying file for writable PF, -1 if no writable handles are open */
    int key_type; /* one of KEY_WRAP (provisioned key), KEY_MRENCLAVE, KEY_MRSIGNER */
};

/* Initialize the PF library, register PFs from the manifest */
int init_protected_files(void);

/* Take ownership of the global PF lock */
void pf_lock(void);

/* Release ownership of the global PF lock */
void pf_unlock(void);

/* Set new wrap key for protected files (e.g., provisioned by remote user) */
int set_protected_files_key(const char* pf_key_hex);

/* Return a registered PF that matches specified path
   (or the path is contained in a registered PF directory) */
struct protected_file* get_protected_file(const char* path);

/* Load and initialize a PF (must be called before any I/O operations)
 *
 * path:   normalized host path
 * fd:     pointer to an opened file descriptor (must point to a valid value for the whole time PF
 *         is being accessed)
 * size:   underlying file size (in bytes)
 * mode:   access mode
 * create: if true, the PF is being created/truncated
 * pf:     (optional) PF pointer if already known
 */
struct protected_file* load_protected_file(const char* path, int* fd, size_t size,
                                           pf_file_mode_t mode, bool create,
                                           struct protected_file* pf);

/* Flush PF map buffers and optionally remove and free them.
   If pf is NULL, process all maps containing given buffer.
   If buffer is NULL, process all maps for given pf. */
int flush_pf_maps(struct protected_file* pf, void* buffer, bool remove);

/* Flush map buffers and unload/close the PF */
int unload_protected_file(struct protected_file* pf);

/* Find registered PF by path (exact match) */
struct protected_file* find_protected_file(const char* path);

/* Find protected file by handle (uses handle's path to call find_protected_file) */
struct protected_file* find_protected_file_handle(PAL_HANDLE handle);

/* exchange and establish a 256-bit session key */
int _DkStreamKeyExchange(PAL_HANDLE stream, PAL_SESSION_KEY* key);

/* master key for all enclaves of one application, populated by the first enclave and inherited by
 * all other enclaves (children, their children, etc.); used as master key in pipes' encryption */
extern PAL_SESSION_KEY g_master_key;

/*
 * sgx_verify_report: verify a CPU-signed report from another local enclave
 * @report: the buffer storing the report to verify
 */
int sgx_verify_report(sgx_report_t* report);

/*!
 * \brief Obtain a CPU-signed report for local attestation.
 *
 * Caller must align all parameters to 512 bytes (cf. `__sgx_mem_aligned`).
 *
 * \param[in]  target_info  Information on the target enclave.
 * \param[in]  data         User-specified data to be included in the report.
 * \param[out] report       Output buffer to store the report.
 * \return                  0 on success, negative error code otherwise.
 */
int sgx_get_report(const sgx_target_info_t* target_info, const sgx_report_data_t* data,
                   sgx_report_t* report);

/*!
 * \brief Obtain an enclave/signer-specific key via EGETKEY(SEAL_KEY) for secret migration/sealing
 * of files.
 *
 * \param[in]  key_policy  Must be KEYPOLICY_MRENCLAVE or KEYPOLICY_MRSIGNER. Binds the sealing key
 *                         to MRENCLAVE (only the same enclave can unseal secrets) or to MRSIGNER
 *                         (all enclaves from the same signer can unseal secrets).
 * \param[out] seal_key    Output buffer to store the sealing key.
 * \return 0 on success, negative error code otherwise.
 */
int sgx_get_seal_key(uint16_t key_policy, sgx_key_128bit_t* seal_key);

/*!
 * \brief Verify the remote enclave during SGX local attestation.
 *
 * Verifies that the MR_ENCLAVE of the remote enclave is the same as ours (all Graphene enclaves
 * with the same configuration have the same MR_ENCLAVE), and that the signer of the SGX report is
 * the owner of the newly established session key.
 *
 * \param  session key  Newly established session key between this enclave and remote enclave.
 * \param  mr_enclave   MR_ENCLAVE of the remote enclave received in its SGX report.
 * \param  remote_data  Remote enclave's SGX report data, contains hash of the session key.
 * \return 0 on success, negative error code otherwise.
 */
bool is_remote_enclave_ok(const PAL_SESSION_KEY* session_key, sgx_measurement_t* mr_enclave,
                          sgx_report_data_t* remote_data);
/*!
 * \brief Request a local report on an RPC stream (typically called by parent enclave).
 *
 * \param  stream           Stream handle for sending and receiving messages.
 * \param  sgx_report_data  User-defined data to embed into outbound SGX report.
 * \return 0 on success, negative error code otherwise.
 */
int _DkStreamReportRequest(PAL_HANDLE stream, sgx_report_data_t* sgx_report_data);
/*!
 * \brief Respond with a local report on an RPC stream (typically called by child enclave).
 *
 * \param  stream  stream handle for sending and receiving messages.
 * \param  sgx_report_data  User-defined data to embed into outbound SGX report.
 * \return 0 on success, negative error code otherwise.
 */
int _DkStreamReportRespond(PAL_HANDLE stream, sgx_report_data_t* sgx_report_data);

int _DkStreamSecureInit(PAL_HANDLE stream, bool is_server, PAL_SESSION_KEY* session_key,
                        LIB_SSL_CONTEXT** out_ssl_ctx, const uint8_t* buf_load_ssl_ctx,
                        size_t buf_size);
int _DkStreamSecureFree(LIB_SSL_CONTEXT* ssl_ctx);
int _DkStreamSecureRead(LIB_SSL_CONTEXT* ssl_ctx, uint8_t* buf, size_t len, bool is_blocking);
int _DkStreamSecureWrite(LIB_SSL_CONTEXT* ssl_ctx, const uint8_t* buf, size_t len,
                         bool is_blocking);
int _DkStreamSecureSave(LIB_SSL_CONTEXT* ssl_ctx, const uint8_t** obuf, size_t* olen);

#else /* IN_ENCLAVE */

#ifdef DEBUG
#ifndef SIGCHLD
#define SIGCHLD 17
#endif

#define ARCH_VFORK()                                                                 \
    (g_pal_enclave.pal_sec.in_gdb                                                    \
         ? INLINE_SYSCALL(clone, 4, CLONE_VM | CLONE_VFORK | SIGCHLD, 0, NULL, NULL) \
         : INLINE_SYSCALL(clone, 4, CLONE_VM | CLONE_VFORK, 0, NULL, NULL))
#else
#define ARCH_VFORK() \
    (INLINE_SYSCALL(clone, 4, CLONE_VM|CLONE_VFORK, 0, NULL, NULL))
#endif

int sgx_create_process(size_t nargs, const char** args, int* stream_fd, const char* manifest);

int clone(int (*__fn)(void* __arg), void* __child_stack, int __flags, const void* __arg, ...);

#endif /* IN_ENCLAVE */

#endif /* PAL_LINUX_H */
