/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * This source file contains functions to create a child process and terminate the running process.
 * Child does not inherit any objects or memory from its parent process. A parent process may not
 * modify the execution of its children. It can wait for a child to exit using its handle. Also,
 * parent and child may communicate through I/O streams provided by the parent to the child at
 * creation.
 */

#include <asm/fcntl.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/types.h>

#include "api.h"
#include "crypto.h"
#include "pal.h"
#include "pal_defs.h"
#include "pal_error.h"
#include "pal_internal.h"
#include "pal_linux.h"
#include "pal_linux_defs.h"
#include "pal_linux_error.h"
#include "pal_security.h"
#include "protected-files/protected_files.h"
#include "spinlock.h"

/*
 * For SGX, the creation of a child process requires a clean enclave and a secure channel between
 * the parent and child processes (enclaves). The establishment of the secure channel must be
 * resilient to a host-level, root-privilege adversary. Such an adversary can either create
 * arbitrary enclaves, or intercept the handshake protocol between the parent and child enclaves to
 * launch a man-in-the-middle attack.
 *
 * The below protocol combines unauthenticated Diffie-Hellman Key Exchange (DHKE) with the SGX local
 * attestation (this combination provides authenticated DHKE). The protocol is a modified ISO KE
 * protocol (as described in Section 4 of the "SIGMA: the `SIGn-and-MAc` Approach to Authenticated
 * Diffie-Hellman and its Use in the IKE Protocols" paper from Hugo Krawczyk). The ISO KE protocol
 * does not support the "identity protection" feature; this feature is however useless in the SGX
 * local attestation threat model (malicious host knows identities of all running SGX enclaves
 * anyway). The ISO KE protocol is proven to be secure and minimal.
 *
 * One superficial difference of the below protocol from the original ISO KE protocol is the number
 * of steps involved. Our protocol uses 5 steps (send DH's g_x, receive DH's g_y, send SGX target
 * info with identity of enclave A, receive SGX report of enclave B, send SGX report of enclave A).
 * Note here that SGX report of an enclave contains also the identity of the enclave. The original
 * ISO KE protocol uses 3 steps (send DH's g_x and identity of A, receive DH's g_y and identity of B
 * and signature of B over {g_x, g_y, A}, send signature of A over {g_x, g_y, B}). Looking at the
 * steps, it is clear that our protocol could be optimized to send 3 messages instead of 5 messages
 * and would result in the original ISO KE protocol. We implement our protocol in 5 steps purely for
 * readability.
 *
 * Another difference of the below protocol from the original ISO KE protocol is the notion of
 * "identity". In SGX local attestation, identity of an enclave is a combination of its
 * measurements: mrenclave, attributes, configsvn, etc. This enclave identity is enclosed in the SGX
 * targetinfo struct as well as in the SGX report. Thus, we use SGX targetinfo and SGX report as
 * identities.
 *
 * Final difference is CMAC used in SGX local attestation for SGX report validation, instead of the
 * signatures in the original ISO KE protocol. This CMAC however provides the same security
 * guarantees because it is generated over the SGX report that contains the target identity of the
 * remote enclave, thus it corresponds to the "directed signature from B to A" in ISO KE.
 *
 * Note that Graphene does *not* use the SIGMA-like protocol (used in e.g. Intel SGX SDK). SIGMA
 * family of protocols is an enhancement over ISO KE to add the "identity protection" feature, which
 * is irrelevant for SGX local attestation. Thus, Graphene chooses a simpler protocol.
 *
 * Prerequisites of a secure channel:
 * (1) A session key needs to be shared only between the parent and child enclaves.
 *
 *       See the implementation in _DkStreamKeyExchange(). When initializing a secure stream, both
 *       ends of the stream needs to use Diffie-Hellman to establish a session key. The DH
 *       collaterals are used to identify the connection (via SHA256(g_x || g_y) to prevent
 *       man-in-the-middle attack) as well as to derive the session key for secure IPC.
 *
 * (2) Both the parent and child enclaves need to be proven by the Intel CPU.
 *
 *       See the implementation in _DkStreamReportRequest() and _DkStreamReportRespond(). The two
 *       ends of the stream need to exchange SGX local attestation reports signed by the Intel CPUs
 *       to prove themselves to be running inside enclaves on the same platform. The local
 *       attestation reports contain no secret information and can be verified cryptographically,
 *       and can be sent on an unencrypted channel.
 *
 *       The flow of local attestation is as follows:
 *         - Parent: Send targetinfo(Parent) to Child
 *         - Child:  Generate report(Child -> Parent) and send to Parent
 *         - Parent: Verify report(Child -> Parent)
 *         - Parent: Extract targetinfo(Child) from report(Child -> Parent)
 *                   and then generate report(Parent -> Child)
 *         - Child:  Verify report(Parent -> Child)
 *
 * (3) Both the parent and child enclaves need to have matching measurements.
 *
 *       All Graphene enclaves with the same configuration (manifest) and same Graphene (LibOS, PAL)
 *       binaries should have the same measurement. During initialization, it's decided based on
 *       input from untrusted PAL, whether a particular enclave will become a leader of a new
 *       Graphene namespace, or will wait on a pipe for some parent enclave connection.
 *
 * (4) The two parties who create the session key need to be the ones proven by the CPU
 *     (for preventing man-in-the-middle attacks).
 *
 *       See the implementation in is_remote_enclave_ok(). The local reports from both sides will
 *       contain a secure hash over DH collaterals: SHA256(g_x || g_y). Because a new DH key
 *       exchange protocol is run for each enclave pair, no report can be reused even from an
 *       enclave with the same mr_enclave.
 */

bool is_remote_enclave_ok(sgx_measurement_t* mr_enclave, sgx_report_data_t* my_data,
                          sgx_report_data_t* remote_data) {
    /* must make sure the signer of the report is also the owner of the key,
       in order to prevent man-in-the-middle attack */
    if (memcmp(remote_data, my_data, sizeof(*my_data)))
        return false;

    /* all Graphene enclaves with same configuration (manifest) should have the same MR_ENCLAVE */
    if (!memcmp(mr_enclave, &g_pal_sec.mr_enclave, sizeof(*mr_enclave)))
        return true;

    return false;
}

int _DkProcessCreate(PAL_HANDLE* handle, const char** args) {
    unsigned int child_pid;
    int stream_fd;
    int nargs = 0, ret;

    if (args)
        for (const char** a = args; *a; a++)
            nargs++;

    ret = ocall_create_process(nargs, args, &stream_fd, &child_pid);
    if (ret < 0)
        return unix_to_pal_error(ret);

    PAL_HANDLE child = malloc(HANDLE_SIZE(process));
    if (!child)
        return -PAL_ERROR_NOMEM;

    SET_HANDLE_TYPE(child, process);
    HANDLE_HDR(child)->flags |= RFD(0) | WFD(0);
    child->process.stream      = stream_fd;
    child->process.pid         = child_pid;
    child->process.nonblocking = PAL_FALSE;
    child->process.is_server   = true;
    child->process.ssl_ctx     = NULL;

    __sgx_mem_aligned sgx_report_data_t sgx_report_data = {0};
    ret = _DkStreamKeyExchange(/*is_parent=*/false, child, &child->process.session_key,
                               (uint8_t*)&sgx_report_data);
    if (ret < 0)
        goto failed;

    ret = _DkStreamReportRequest(child, &sgx_report_data);
    if (ret < 0)
        goto failed;

    ret = _DkStreamSecureInit(child, child->process.is_server, &child->process.session_key,
                              (LIB_SSL_CONTEXT**)&child->process.ssl_ctx, NULL, 0);
    if (ret < 0)
        goto failed;

    /* securely send the master key to child in the newly established SSL session */
    ret = _DkStreamSecureWrite(child->process.ssl_ctx, (uint8_t*)&g_master_key,
                               sizeof(g_master_key), /*is_blocking=*/!child->process.nonblocking);
    if (ret != sizeof(g_master_key))
        goto failed;

    /* securely send the wrap key for protected files to child (only if there is one) */
    char pf_wrap_key_set_char[1];
    pf_wrap_key_set_char[0] = g_pf_wrap_key_set ? '1' : '0';

    ret = _DkStreamSecureWrite(child->process.ssl_ctx, (uint8_t*)&pf_wrap_key_set_char,
                               sizeof(pf_wrap_key_set_char),
                               /*is_blocking=*/!child->process.nonblocking);
    if (ret != sizeof(pf_wrap_key_set_char))
        goto failed;

    if (g_pf_wrap_key_set) {
        ret = _DkStreamSecureWrite(child->process.ssl_ctx, (uint8_t*)&g_pf_wrap_key,
                                   sizeof(g_pf_wrap_key),
                                   /*is_blocking=*/!child->process.nonblocking);
        if (ret != sizeof(g_pf_wrap_key))
            goto failed;
    }

    *handle = child;
    return 0;

failed:
    free(child);
    return ret;
}

int init_child_process(PAL_HANDLE* parent_handle) {
    if (g_pal_sec.enclave_flags & PAL_ENCLAVE_INITIALIZED)
        return -PAL_ERROR_DENIED;

    PAL_HANDLE parent = malloc(HANDLE_SIZE(process));
    if (!parent)
        return -PAL_ERROR_NOMEM;

    SET_HANDLE_TYPE(parent, process);
    HANDLE_HDR(parent)->flags |= RFD(0) | WFD(0);

    parent->process.stream      = g_pal_sec.stream_fd;
    parent->process.pid         = g_pal_sec.ppid;
    parent->process.nonblocking = PAL_FALSE;
    parent->process.is_server   = false;
    parent->process.ssl_ctx     = NULL;

    __sgx_mem_aligned sgx_report_data_t sgx_report_data = {0};
    int ret = _DkStreamKeyExchange(/*is_parent=*/true, parent, &parent->process.session_key,
                                   (uint8_t*)&sgx_report_data);
    if (ret < 0)
        return ret;

    ret = _DkStreamReportRespond(parent, &sgx_report_data);
    if (ret < 0)
        return ret;

    ret = _DkStreamSecureInit(parent, parent->process.is_server, &parent->process.session_key,
                              (LIB_SSL_CONTEXT**)&parent->process.ssl_ctx, NULL, 0);
    if (ret < 0)
        return ret;

    /* securely receive the master key from parent in the newly established SSL session */
    ret = _DkStreamSecureRead(parent->process.ssl_ctx, (uint8_t*)&g_master_key,
                              sizeof(g_master_key), /*is_blocking=*/!parent->process.nonblocking);
    if (ret != sizeof(g_master_key))
        return ret;

    /* securely receive the wrap key for protected files from parent (only if there is one) */
    char pf_wrap_key_set_char[1] = {0};
    ret = _DkStreamSecureRead(parent->process.ssl_ctx, (uint8_t*)&pf_wrap_key_set_char,
                              sizeof(pf_wrap_key_set_char),
                              /*is_blocking=*/!parent->process.nonblocking);
    if (ret != sizeof(pf_wrap_key_set_char))
        return ret;

    if (pf_wrap_key_set_char[0] == '1') {
        ret = _DkStreamSecureRead(parent->process.ssl_ctx, (uint8_t*)&g_pf_wrap_key,
                                  sizeof(g_pf_wrap_key),
                                  /*is_blocking=*/!parent->process.nonblocking);
        if (ret != sizeof(g_pf_wrap_key)) {
            g_pf_wrap_key_set = false;
            return ret;
        }

        g_pf_wrap_key_set = true;
    }

    *parent_handle = parent;
    return 0;
}

noreturn void _DkProcessExit(int exitcode) {
    if (exitcode)
        log_debug("DkProcessExit: Returning exit code %d\n", exitcode);
    ocall_exit(exitcode, /*is_exitgroup=*/true);
    /* Unreachable. */
}

static int64_t proc_read(PAL_HANDLE handle, uint64_t offset, uint64_t count, void* buffer) {
    if (offset)
        return -PAL_ERROR_INVAL;

    ssize_t bytes;
    if (handle->process.ssl_ctx) {
        bytes = _DkStreamSecureRead(handle->process.ssl_ctx, buffer, count,
                                    /*is_blocking=*/!handle->process.nonblocking);
    } else {
        bytes = ocall_read(handle->process.stream, buffer, count);
        bytes = bytes < 0 ? unix_to_pal_error(bytes) : bytes;
    }

    return bytes;
}

static int64_t proc_write(PAL_HANDLE handle, uint64_t offset, uint64_t count, const void* buffer) {
    if (offset)
        return -PAL_ERROR_INVAL;

    ssize_t bytes;
    if (handle->process.ssl_ctx) {
        bytes = _DkStreamSecureWrite(handle->process.ssl_ctx, buffer, count,
                                     /*is_blocking=*/!handle->process.nonblocking);
    } else {
        bytes = ocall_write(handle->process.stream, buffer, count);
        bytes = bytes < 0 ? unix_to_pal_error(bytes) : bytes;
    }

    return bytes;
}

static int proc_close(PAL_HANDLE handle) {
    if (handle->process.stream != PAL_IDX_POISON) {
        ocall_close(handle->process.stream);
        handle->process.stream = PAL_IDX_POISON;
    }

    if (handle->process.ssl_ctx) {
        _DkStreamSecureFree((LIB_SSL_CONTEXT*)handle->process.ssl_ctx);
        handle->process.ssl_ctx = NULL;
    }

    return 0;
}

static int proc_delete(PAL_HANDLE handle, int access) {
    int shutdown;
    switch (access) {
        case 0:
            shutdown = SHUT_RDWR;
            break;
        case PAL_DELETE_RD:
            shutdown = SHUT_RD;
            break;
        case PAL_DELETE_WR:
            shutdown = SHUT_WR;
            break;
        default:
            return -PAL_ERROR_INVAL;
    }

    if (handle->process.stream != PAL_IDX_POISON)
        ocall_shutdown(handle->process.stream, shutdown);

    return 0;
}

static int proc_attrquerybyhdl(PAL_HANDLE handle, PAL_STREAM_ATTR* attr) {
    int ret;

    if (handle->process.stream == PAL_IDX_POISON)
        return -PAL_ERROR_BADHANDLE;

    attr->handle_type  = HANDLE_HDR(handle)->type;
    attr->nonblocking  = handle->process.nonblocking;
    attr->disconnected = HANDLE_HDR(handle)->flags & ERROR(0);

    /* get number of bytes available for reading */
    ret = ocall_fionread(handle->process.stream);
    if (ret < 0)
        return unix_to_pal_error(ret);

    attr->pending_size = ret;

    /* query if there is data available for reading */
    struct pollfd pfd = {.fd = handle->process.stream, .events = POLLIN | POLLOUT, .revents = 0};
    ret = ocall_poll(&pfd, 1, 0);
    if (ret < 0)
        return unix_to_pal_error(ret);

    attr->readable = ret == 1 && (pfd.revents & (POLLIN | POLLERR | POLLHUP)) == POLLIN;
    attr->writable = ret == 1 && (pfd.revents & (POLLOUT | POLLERR | POLLHUP)) == POLLOUT;
    return 0;
}

static int proc_attrsetbyhdl(PAL_HANDLE handle, PAL_STREAM_ATTR* attr) {
    if (handle->process.stream == PAL_IDX_POISON)
        return -PAL_ERROR_BADHANDLE;

    if (attr->nonblocking != handle->process.nonblocking) {
        int ret = ocall_fsetnonblock(handle->process.stream, handle->process.nonblocking);
        if (ret < 0)
            return unix_to_pal_error(ret);

        handle->process.nonblocking = attr->nonblocking;
    }

    return 0;
}

struct handle_ops g_proc_ops = {
    .read           = &proc_read,
    .write          = &proc_write,
    .close          = &proc_close,
    .delete         = &proc_delete,
    .attrquerybyhdl = &proc_attrquerybyhdl,
    .attrsetbyhdl   = &proc_attrsetbyhdl,
};
