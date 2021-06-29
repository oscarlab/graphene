/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2018-2020 Invisible Things Lab
 *                         Rafal Wojdyla <omeg@invisiblethingslab.com>
 */

#include <linux/fs.h>

#include "crypto.h"
#include "hex.h"
#include "pal_internal.h"
#include "pal_linux.h"
#include "pal_linux_error.h"
#include "spinlock.h"
#include "toml.h"

/* SGX-specific keys for protected files, used for SGX sealing. The former key is bound to the
 * MRENCLAVE measurement of the SGX enclave (only the same enclave can unseal secrets). The latter
 * key is bound to the MRSIGNER measurement (all enclaves from the same signer can unseal secrets).
 * We don't use synchronization on them since they are only set during initialization where Graphene
 * runs single-threaded. */
pf_key_t g_pf_mrenclave_key = {0};
pf_key_t g_pf_mrsigner_key = {0};

/* Wrap key for protected files, either hard-coded in manifest, provisioned during attestation, or
 * inherited from the parent process. We don't use synchronization on them since they are only set
 * during initialization where Graphene runs single-threaded. */
pf_key_t g_pf_wrap_key = {0};
bool g_pf_wrap_key_set = false;

/*
 * At startup, protected file paths are read from the manifest and the specified files
 * or directories registered. For supported I/O operations, handlers (in db_files.c)
 * check if the file is a PF to perform the required operation transparently.
 *
 * Since PF's "logical" size is different than the real FS size (and to avoid potential
 * infinite recursion in FS handlers) we don't use PAL file APIs here, but raw OCALLs.
 */

/* List of map buffers */
LISTP_TYPE(pf_map) g_pf_map_list = LISTP_INIT;

/* Callbacks for protected files handling */
static pf_status_t cb_read(pf_handle_t handle, void* buffer, uint64_t offset, size_t size) {
    int fd = *(int*)handle;
    size_t buffer_offset = 0;
    size_t to_read = size;
    while (to_read > 0) {
        ssize_t read = ocall_pread(fd, buffer + buffer_offset, to_read, offset + buffer_offset);
        if (read == -EINTR)
            continue;

        if (read < 0) {
            log_error("cb_read(%d, %p, %lu, %lu): read failed: %ld\n", fd, buffer, offset,
                      size, read);
            return PF_STATUS_CALLBACK_FAILED;
        }

        /* EOF is an error condition, we want to read exactly `size` bytes */
        if (read == 0) {
            log_error("cb_read(%d, %p, %lu, %lu): EOF\n", fd, buffer, offset, size);
            return PF_STATUS_CALLBACK_FAILED;
        }

        to_read -= read;
        buffer_offset += read;
    }
    return PF_STATUS_SUCCESS;
}

static pf_status_t cb_write(pf_handle_t handle, const void* buffer, uint64_t offset, size_t size) {
    int fd = *(int*)handle;
    size_t buffer_offset = 0;
    size_t to_write = size;
    while (to_write > 0) {
        ssize_t written = ocall_pwrite(fd, buffer + buffer_offset, to_write,
                                       offset + buffer_offset);
        if (written == -EINTR)
            continue;

        if (written < 0) {
            log_error("cb_write(%d, %p, %lu, %lu): write failed: %ld\n", fd, buffer, offset,
                      size, written);
            return PF_STATUS_CALLBACK_FAILED;
        }

        /* EOF is an error condition, we want to write exactly `size` bytes */
        if (written == 0) {
            log_error("cb_write(%d, %p, %lu, %lu): EOF\n", fd, buffer, offset, size);
            return PF_STATUS_CALLBACK_FAILED;
        }

        to_write -= written;
        buffer_offset += written;
    }
    return PF_STATUS_SUCCESS;
}

static pf_status_t cb_truncate(pf_handle_t handle, uint64_t size) {
    int fd = *(int*)handle;
    int ret = ocall_ftruncate(fd, size);
    if (ret < 0) {
        log_error("cb_truncate(%d, %lu): ocall failed: %d\n", fd, size, ret);
        return PF_STATUS_CALLBACK_FAILED;
    }
    return PF_STATUS_SUCCESS;
}

#ifdef DEBUG
static void cb_debug(const char* msg) {
    log_debug("%s", msg);
}
#endif

static pf_status_t cb_aes_cmac(const pf_key_t* key, const void* input, size_t input_size,
                               pf_mac_t* mac) {
    int ret = lib_AESCMAC((const uint8_t*)key, sizeof(*key), input, input_size, (uint8_t*)mac,
                          sizeof(*mac));
    if (ret != 0) {
        log_error("lib_AESCMAC failed: %d\n", ret);
        return PF_STATUS_CALLBACK_FAILED;
    }
    return PF_STATUS_SUCCESS;
}

static pf_status_t cb_aes_gcm_encrypt(const pf_key_t* key, const pf_iv_t* iv, const void* aad,
                                      size_t aad_size, const void* input, size_t input_size,
                                      void* output, pf_mac_t* mac) {
    int ret = lib_AESGCMEncrypt((const uint8_t*)key, sizeof(*key), (const uint8_t*)iv, input,
                                input_size, aad, aad_size, output, (uint8_t*)mac, sizeof(*mac));
    if (ret != 0) {
        log_error("lib_AESGCMEncrypt failed: %d\n", ret);
        return PF_STATUS_CALLBACK_FAILED;
    }
    return PF_STATUS_SUCCESS;
}

static pf_status_t cb_aes_gcm_decrypt(const pf_key_t* key, const pf_iv_t* iv, const void* aad,
                                      size_t aad_size, const void* input, size_t input_size,
                                      void* output, const pf_mac_t* mac) {
    int ret = lib_AESGCMDecrypt((const uint8_t*)key, sizeof(*key), (const uint8_t*)iv, input,
                                input_size, aad, aad_size, output, (const uint8_t*)mac,
                                sizeof(*mac));
    if (ret != 0) {
        log_error("lib_AESGCMDecrypt failed: %d\n", ret);
        return PF_STATUS_CALLBACK_FAILED;
    }
    return PF_STATUS_SUCCESS;
}

static pf_status_t cb_random(uint8_t* buffer, size_t size) {
    int ret = _DkRandomBitsRead(buffer, size);
    if (ret < 0) {
        log_error("_DkRandomBitsRead failed: %d\n", ret);
        return PF_STATUS_CALLBACK_FAILED;
    }
    return PF_STATUS_SUCCESS;
}

/* Collection of registered protected files */
static struct protected_file* g_protected_files = NULL;

/* Collection of registered protected directories */
static struct protected_file* g_protected_dirs = NULL;

/* Lock for operations on global PF structures */
static spinlock_t g_protected_file_lock = INIT_SPINLOCK_UNLOCKED;

/* Take ownership of the global PF lock */
void pf_lock(void) {
    spinlock_lock(&g_protected_file_lock);
}

/* Release ownership of the global PF lock */
void pf_unlock(void) {
    spinlock_unlock(&g_protected_file_lock);
}

/* Exact match of path in g_protected_files */
struct protected_file* find_protected_file(const char* path) {
    struct protected_file* pf = NULL;

    pf_lock();
    HASH_FIND_STR(g_protected_files, path, pf);
    pf_unlock();
    return pf;
}

/* Find registered pf directory starting with the given path */
static struct protected_file* find_protected_dir(const char* path) {
    struct protected_file* pf  = NULL;
    struct protected_file* tmp = NULL;
    size_t len = strlen(path);

    pf_lock();
    // TODO: avoid linear lookup
    for (tmp = g_protected_dirs; tmp != NULL; tmp = tmp->hh.next) {
        if (tmp->path_len < len && !memcmp(tmp->path, path, tmp->path_len) &&
                (!path[tmp->path_len] || path[tmp->path_len] == '/')) {
            pf = tmp;
            break;
        }
    }

    pf_unlock();
    return pf;
}

/* Find PF by handle */
struct protected_file* find_protected_file_handle(PAL_HANDLE handle) {
    char* uri = malloc(URI_MAX);
    if (!uri) {
        return NULL;
    }

    struct protected_file* ret = NULL;

    /* TODO: this logic is inefficient, add a PF reference to PAL_HANDLE instead */
    int uri_len = _DkStreamGetName(handle, uri, URI_MAX);
    if (uri_len < 0) {
        goto out;
    }

    /* uri is prefixed by "file:", we need path */
    assert(strstartswith(uri, URI_PREFIX_FILE));
    ret = find_protected_file(uri + URI_PREFIX_FILE_LEN);

out:
    free(uri);
    return ret;
}

static int register_protected_path(const char* path, int key_type, struct protected_file** new_pf);

/* Return a registered PF that matches specified path
   (or the path that is contained in a registered PF directory) */
struct protected_file* get_protected_file(const char* path) {
    struct protected_file* pf = find_protected_file(path);
    if (pf)
        goto out;

    pf = find_protected_dir(path);
    if (pf) {
        /* path not registered but matches registered dir */
        log_debug("get_pf: registering new PF '%s' in dir '%s'\n", path, pf->path);
        int ret = register_protected_path(path, pf->key_type, &pf);
        __UNUSED(ret);
        assert(ret == 0);
        /* return newly registered PF */
    }

out:
    return pf;
}

#define S_ISDIR(m) ((m & 0170000) == 0040000)

static int is_directory(const char* path, bool* is_dir) {
    int fd = -1;
    struct stat st;

    *is_dir = false;
    int ret = ocall_open(path, O_RDONLY, 0);
    if (ret < 0) {
        /* this can be called on a path without the file existing, assume non-dir for now */
        ret = 0;
        goto out;
    }

    fd = ret;
    ret = ocall_fstat(fd, &st);
    if (ret < 0) {
        log_error("is_directory(%s): fstat failed: %d\n", path, ret);
        goto out;
    }

    if (S_ISDIR(st.st_mode))
        *is_dir = true;

out:
    if (fd >= 0) {
        int rv = ocall_close(fd);
        if (rv < 0) {
            log_error("is_directory(%s): close failed: %d\n", path, rv);
        }
    }

    return ret < 0 ? unix_to_pal_error(ret) : ret;
}

/* Register all files from the given directory recursively */
static int register_protected_dir(const char* path, int key_type) {
    int fd = -1;
    int ret = -PAL_ERROR_NOMEM;
    size_t bufsize = 1024;
    void* buf = malloc(bufsize);

    if (!buf)
        return -PAL_ERROR_NOMEM;

    ret = ocall_open(path, O_RDONLY | O_DIRECTORY, 0);
    if (ret < 0) {
        log_error("register_protected_dir: opening %s failed: %d\n", path, ret);
        ret = unix_to_pal_error(ret);
        goto out;
    }
    fd = ret;

    size_t path_size = strlen(path) + 1;
    int returned;
    do {
        returned = ocall_getdents(fd, buf, bufsize);
        if (returned < 0) {
            ret = unix_to_pal_error(returned);
            log_error("register_protected_dir: reading %s failed: %d\n", path, ret);
            goto out;
        }

        int pos = 0;
        struct linux_dirent64* dir;

        while (pos < returned) {
            dir = (struct linux_dirent64*)((char*)buf + pos);

            if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
                goto next;

            /* register file */
            size_t sub_path_size = URI_PREFIX_FILE_LEN + path_size + 1 + strlen(dir->d_name);
            char* sub_path = malloc(sub_path_size);
            ret = -PAL_ERROR_NOMEM;
            if (!sub_path)
                goto out;

            snprintf(sub_path, sub_path_size, URI_PREFIX_FILE "%s/%s", path, dir->d_name);
            ret = register_protected_path(sub_path, key_type, NULL);
            if (ret != 0) {
                free(sub_path);
                goto out;
            }
            free(sub_path);
        next:
            pos += dir->d_reclen;
        }
    } while (returned != 0);

    ret = 0;
out:
    if (fd >= 0)
        ocall_close(fd);
    free(buf);
    return ret;
}

/* Register a single PF (if it's a directory, recursively) */
static int register_protected_path(const char* path, int key_type, struct protected_file** new_pf) {
    int ret = -PAL_ERROR_NOMEM;
    struct protected_file* new = NULL;

    char* normpath = malloc(URI_MAX);
    if (!normpath)
        goto out;

    size_t len = URI_MAX;
    ret = get_norm_path(path, normpath, &len);
    if (ret < 0) {
        log_error("Couldn't normalize path (%s): %s\n", path, pal_strerror(ret));
        goto out;
    }

    /* discard the "file:" prefix */
    if (strstartswith(normpath, URI_PREFIX_FILE))
        path = normpath + URI_PREFIX_FILE_LEN;
    else
        path = normpath;

    if (find_protected_file(path)) {
        ret = 0;
        log_debug("register_protected_path: file %s already registered\n", path);
        goto out;
    }

    new = calloc(1, sizeof(*new));
    if (!new) {
        ret = -PAL_ERROR_NOMEM;
        goto out;
    }

    new->key_type = key_type;

    new->path_len = strlen(path);
    /* This is never freed but so isn't the whole struct, PFs persist for the whole lifetime
       of the process. */
    new->path = malloc(new->path_len + 1);
    if (!new->path) {
        ret = -PAL_ERROR_NOMEM;
        goto out;
    }

    memcpy(new->path, path, new->path_len + 1);
    new->refcount = 0;
    new->writable_fd = -1;

    bool is_dir;
    ret = is_directory(path, &is_dir);
    if (ret < 0)
        goto out;

    log_debug("register_protected_path: [%s] %s = %p\n", is_dir ? "dir" : "file", path, new);

    if (is_dir)
        register_protected_dir(path, key_type);

    pf_lock();

    if (is_dir) {
        HASH_ADD_STR(g_protected_dirs, path, new);
    } else {
        HASH_ADD_STR(g_protected_files, path, new);
    }

    pf_unlock();

    if (new_pf)
        *new_pf = new;

    ret = 0;
out:
    free(normpath);
    if (ret < 0) {
        if (new)
            free(new->path);
        free(new);
    }
    return ret;
}

/* Read PF paths from manifest and register them */
static int register_protected_files(int key_type) {
    int ret;
    toml_table_t* manifest_sgx = toml_table_in(g_pal_state.manifest_root, "sgx");
    if (!manifest_sgx)
        return 0;

    char* table_name = NULL;
    switch (key_type) {
        case PROTECTED_FILE_KEY_WRAP:
            table_name = "protected_files";
            break;
        case PROTECTED_FILE_KEY_MRENCLAVE:
            table_name = "protected_mrenclave_files";
            break;
        case PROTECTED_FILE_KEY_MRSIGNER:
            table_name = "protected_mrsigner_files";
            break;
        default:
            log_error("Invalid key type when registering protected files!\n");
            return -PAL_ERROR_INVAL;
    }

    assert(table_name);
    toml_table_t* toml_pfs = toml_table_in(manifest_sgx, table_name);
    if (!toml_pfs)
        return 0;

    ssize_t toml_pfs_cnt = toml_table_nkval(toml_pfs);
    if (toml_pfs_cnt <= 0) {
        /* no PF entries found in the manifest */
        return 0;
    }

    for (ssize_t i = 0; i < toml_pfs_cnt; i++) {
        const char* toml_pf_key = toml_key_in(toml_pfs, i);
        assert(toml_pf_key);
        toml_raw_t toml_pf_value_raw = toml_raw_in(toml_pfs, toml_pf_key);
        assert(toml_pf_value_raw);

        char* toml_pf_value = NULL;
        ret = toml_rtos(toml_pf_value_raw, &toml_pf_value);
        if (ret < 0) {
            log_error("Invalid PF entry in manifest: \'%s\'\n", toml_pf_key);
            continue;
        }

        if (!strstartswith(toml_pf_value, URI_PREFIX_FILE)) {
            log_error("Invalid URI [%s]: URIs of protected files must start with \'"
                      URI_PREFIX_FILE "\'\n", toml_pf_value);
        } else {
            register_protected_path(toml_pf_value, key_type, NULL);
        }
        free(toml_pf_value);
    }

    pf_lock();
    log_debug("Registered %u protected directories and %u protected files\n",
              HASH_COUNT(g_protected_dirs), HASH_COUNT(g_protected_files));
    pf_unlock();
    return 0;
}

/* Initialize the PF library, register PFs from the manifest */
int init_protected_files(void) {
    int ret;
    pf_debug_f debug_callback = NULL;

#ifdef DEBUG
    debug_callback = cb_debug;
#endif

    pf_set_callbacks(cb_read, cb_write, cb_truncate, cb_aes_cmac, cb_aes_gcm_encrypt,
                     cb_aes_gcm_decrypt, cb_random, debug_callback);

    ret = sgx_get_seal_key(KEYPOLICY_MRENCLAVE, &g_pf_mrenclave_key);
    if (ret < 0) {
        log_error("Cannot obtain MRENCLAVE-specific protected files key\n");
        return ret;
    }

    ret = sgx_get_seal_key(KEYPOLICY_MRSIGNER, &g_pf_mrsigner_key);
    if (ret < 0) {
        log_error("Cannot obtain MRSIGNER-specific protected files key\n");
        return ret;
    }

    /* if wrap key is not hard-coded in the manifest, assume that it was received from parent or
     * it will be provisioned after local/remote attestation; otherwise read it from manifest */
    char* protected_files_key_str = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "sgx.protected_files_key",
                         &protected_files_key_str);
    if (ret < 0) {
        log_error("Cannot parse \'sgx.protected_files_key\' "
                  "(the value must be put in double quotes!)\n");
        return -PAL_ERROR_INVAL;
    }

    if (protected_files_key_str) {
        if (strlen(protected_files_key_str) != PF_KEY_SIZE * 2) {
            log_error("Malformed \'sgx.protected_files_key\' value in the manifest\n");
            free(protected_files_key_str);
            return -PAL_ERROR_INVAL;
        }

        memset(g_pf_wrap_key, 0, sizeof(g_pf_wrap_key));
        for (size_t i = 0; i < strlen(protected_files_key_str); i++) {
            int8_t val = hex2dec(protected_files_key_str[i]);
            if (val < 0) {
                log_error("Malformed \'sgx.protected_files_key\' value in the manifest\n");
                free(protected_files_key_str);
                return -PAL_ERROR_INVAL;
            }
            g_pf_wrap_key[i / 2] = g_pf_wrap_key[i / 2] * 16 + (uint8_t)val;
        }

        free(protected_files_key_str);
        g_pf_wrap_key_set = true;
    }

    ret = register_protected_files(PROTECTED_FILE_KEY_WRAP);
    if (ret < 0) {
        log_error("Malformed protected files found in manifest\n");
        return ret;
    }

    ret = register_protected_files(PROTECTED_FILE_KEY_MRENCLAVE);
    if (ret < 0) {
        log_error("Malformed MRENCLAVE-specific protected files found in manifest\n");
        return ret;
    }

    ret = register_protected_files(PROTECTED_FILE_KEY_MRSIGNER);
    if (ret < 0) {
        log_error("Malformed MRSIGNER-specific protected files found in manifest\n");
        return ret;
    }

    return 0;
}

/* Open/create a PF */
static int open_protected_file(const char* path, struct protected_file* pf, pf_handle_t handle,
                               uint64_t size, pf_file_mode_t mode, bool create) {
    pf_key_t* pf_key = NULL;
    switch (pf->key_type) {
        case PROTECTED_FILE_KEY_WRAP:
            if (!g_pf_wrap_key_set) {
                log_error("pf_open failed: wrap key was not provided\n");
                return -PAL_ERROR_DENIED;
            }
            pf_key = &g_pf_wrap_key;
            break;
        case PROTECTED_FILE_KEY_MRENCLAVE:
            pf_key = &g_pf_mrenclave_key;
            break;
        case PROTECTED_FILE_KEY_MRSIGNER:
            pf_key = &g_pf_mrsigner_key;
            break;
        default:
            log_error("Invalid key type when opening protected file!\n");
            return -PAL_ERROR_DENIED;
    }
    assert(pf_key);

    pf_status_t pfs;
    pfs = pf_open(handle, path, size, mode, create, pf_key, &pf->context);
    if (PF_FAILURE(pfs)) {
        log_error("pf_open(%d, %s) failed: %s\n", *(int*)handle, path, pf_strerror(pfs));
        return -PAL_ERROR_DENIED;
    }
    return 0;
}

/* Prepare a PF for I/O
   This function registers the PF if path is in a registered PF directory, then
   calls the appropriate PF function to open/create it (if allowed) */
struct protected_file* load_protected_file(const char* path, int* fd, uint64_t size,
                                           pf_file_mode_t mode, bool create,
                                           struct protected_file* pf) {
    log_debug("load_protected_file: %s, fd %d, size %lu, mode %d, create %d, pf %p\n", path,
              *fd, size, mode, create, pf);

    if (!pf)
        pf = get_protected_file(path);

    if (pf) {
        if (!pf->context) {
            log_debug("load_protected_file: %s, fd %d: opening new PF %p\n", path, *fd, pf);
            int ret = open_protected_file(path, pf, (pf_handle_t)fd, size, mode, create);
            if (ret < 0)
                return NULL;
        } else {
            log_debug("load_protected_file: %s, fd %d: returning old PF %p\n", path, *fd, pf);
        }
    }

    return pf;
}

/* Flush PF map buffers and optionally remove and free them.
 * If pf is NULL, process all maps containing given buffer.
 * If buffer is NULL, process all maps for given pf.
 * If both pf and buffer are NULL, process all maps for all PFs.
 */
int flush_pf_maps(struct protected_file* pf, void* buffer, bool remove) {
    struct pf_map* map;
    struct pf_map* tmp;
    uint64_t pf_size;
    pf_status_t pfs;

    pf_lock();
    LISTP_FOR_EACH_ENTRY_SAFE(map, tmp, &g_pf_map_list, list) {
        if (pf && map->pf != pf)
            continue;

        if (buffer && map->buffer != buffer)
            continue;

        size_t map_size = map->size;
        struct protected_file* map_pf = pf ? pf : map->pf;

        pfs = pf_get_size(map_pf->context, &pf_size);
        assert(PF_SUCCESS(pfs));

        assert(pf_size >= map->offset);
        if (map->offset + map_size > pf_size)
            map_size = pf_size - map->offset;

        if (map_size > 0) {
            pfs = pf_write(map_pf->context, map->offset, map_size, map->buffer);
            if (PF_FAILURE(pfs)) {
                log_error("flush_pf_maps: pf_write failed: %s\n", pf_strerror(pfs));
                pf_unlock();
                return -PAL_ERROR_INVAL;
            }
        }

        if (remove) {
            LISTP_DEL(map, &g_pf_map_list, list);
            free(map);
        }
    }

    pf_unlock();
    return 0;
}

/* Flush map buffers and unload/close the PF */
int unload_protected_file(struct protected_file* pf) {
    /* flush all pf's maps and delete them */
    int ret = flush_pf_maps(pf, NULL, true);
    if (ret < 0)
        return ret;
    pf_status_t pfs = pf_close(pf->context);
    if (PF_FAILURE(pfs)) {
        log_error("unload_protected_file(%p) failed: %s\n", pf, pf_strerror(pfs));
    }

    pf->context = NULL;
    return 0;
}

int set_protected_files_key(const char* pf_key_hex) {
    size_t pf_key_hex_len = strlen(pf_key_hex);
    if (pf_key_hex_len != PF_KEY_SIZE * 2) {
        return -PAL_ERROR_INVAL;
    }

    pf_lock();
    memset(g_pf_wrap_key, 0, sizeof(g_pf_wrap_key));
    for (size_t i = 0; i < pf_key_hex_len; i++) {
        int8_t val = hex2dec(pf_key_hex[i]);
        if (val < 0) {
            memset(g_pf_wrap_key, 0, sizeof(g_pf_wrap_key));
            pf_unlock();
            return -PAL_ERROR_INVAL;
        }
        g_pf_wrap_key[i / 2] = g_pf_wrap_key[i / 2] * 16 + (uint8_t)val;
    }
    g_pf_wrap_key_set = true;
    pf_unlock();

    return 0;
}
