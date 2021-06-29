#include <stdbool.h>

#include "api.h"
#include "crypto.h"
#include "enclave_pages.h"
#include "hex.h"
#include "list.h"
#include "pal_error.h"
#include "pal_internal.h"
#include "pal_linux.h"
#include "pal_linux_error.h"
#include "pal_security.h"
#include "sgx_arch.h"
#include "spinlock.h"
#include "toml.h"

void* g_enclave_base;
void* g_enclave_top;

static int register_trusted_file(const char* uri, const char* checksum_str, bool check_duplicates);

bool sgx_is_completely_within_enclave(const void* addr, size_t size) {
    if ((uintptr_t)addr > UINTPTR_MAX - size) {
        return false;
    }

    return g_enclave_base <= addr && addr + size <= g_enclave_top;
}

bool sgx_is_completely_outside_enclave(const void* addr, size_t size) {
    if ((uintptr_t)addr > UINTPTR_MAX - size) {
        return false;
    }

    return g_enclave_base >= addr + size || g_enclave_top <= addr;
}

/*
 * When DEBUG is enabled, we run sgx_profile_sample() during asynchronous enclave exit (AEX), which
 * uses the stack. Make sure to update URSP so that the AEX handler does not overwrite the part of
 * the stack that we just allocated.
 *
 * (Recall that URSP is an outside stack pointer, saved by EENTER and restored on AEX by the SGX
 * hardware itself.)
 */
#ifdef DEBUG

#define UPDATE_USTACK(_ustack)                           \
    do {                                                 \
        SET_ENCLAVE_TLS(ustack, _ustack);                \
        GET_ENCLAVE_TLS(gpr)->ursp = (uint64_t)_ustack;  \
    } while(0)

#else

#define UPDATE_USTACK(_ustack) SET_ENCLAVE_TLS(ustack, _ustack)

#endif

void* sgx_prepare_ustack(void) {
    void* old_ustack = GET_ENCLAVE_TLS(ustack);

    void* ustack = old_ustack;
    if (ustack != GET_ENCLAVE_TLS(ustack_top))
        ustack -= RED_ZONE_SIZE;
    UPDATE_USTACK(ustack);

    return old_ustack;
}

void* sgx_alloc_on_ustack_aligned(size_t size, size_t alignment) {
    assert(IS_POWER_OF_2(alignment));
    void* ustack = GET_ENCLAVE_TLS(ustack) - size;
    ustack = ALIGN_DOWN_PTR_POW2(ustack, alignment);
    if (!sgx_is_completely_outside_enclave(ustack, size)) {
        return NULL;
    }
    UPDATE_USTACK(ustack);
    return ustack;
}

void* sgx_alloc_on_ustack(size_t size) {
    return sgx_alloc_on_ustack_aligned(size, 1);
}

void* sgx_copy_to_ustack(const void* ptr, size_t size) {
    if (!sgx_is_completely_within_enclave(ptr, size)) {
        return NULL;
    }
    void* uptr = sgx_alloc_on_ustack(size);
    if (uptr) {
        memcpy(uptr, ptr, size);
    }
    return uptr;
}

void sgx_reset_ustack(const void* old_ustack) {
    assert(old_ustack <= GET_ENCLAVE_TLS(ustack_top));
    UPDATE_USTACK(old_ustack);
}

bool sgx_copy_ptr_to_enclave(void** ptr, void* uptr, size_t size) {
    assert(ptr);
    if (!sgx_is_completely_outside_enclave(uptr, size)) {
        *ptr = NULL;
        return false;
    }
    *ptr = uptr;
    return true;
}

bool sgx_copy_to_enclave(void* ptr, size_t maxsize, const void* uptr, size_t usize) {
    if (usize > maxsize ||
        !sgx_is_completely_outside_enclave(uptr, usize) ||
        !sgx_is_completely_within_enclave(ptr, usize)) {
        return false;
    }
    memcpy(ptr, uptr, usize);
    return true;
}

static void print_report(sgx_report_t* r) {
    log_debug("  cpu_svn:     %s\n",     ALLOCA_BYTES2HEXSTR(r->body.cpu_svn.svn));
    log_debug("  mr_enclave:  %s\n",     ALLOCA_BYTES2HEXSTR(r->body.mr_enclave.m));
    log_debug("  mr_signer:   %s\n",     ALLOCA_BYTES2HEXSTR(r->body.mr_signer.m));
    log_debug("  attr.flags:  %016lx\n", r->body.attributes.flags);
    log_debug("  attr.xfrm:   %016lx\n", r->body.attributes.xfrm);
    log_debug("  isv_prod_id: %02x\n",   r->body.isv_prod_id);
    log_debug("  isv_svn:     %02x\n",   r->body.isv_svn);
    log_debug("  report_data: %s\n",     ALLOCA_BYTES2HEXSTR(r->body.report_data.d));
    log_debug("  key_id:      %s\n",     ALLOCA_BYTES2HEXSTR(r->key_id.id));
    log_debug("  mac:         %s\n",     ALLOCA_BYTES2HEXSTR(r->mac));
}

int sgx_get_report(const sgx_target_info_t* target_info, const sgx_report_data_t* data,
                   sgx_report_t* report) {
    int ret = sgx_report(target_info, data, report);
    if (ret) {
        log_error("sgx_report failed: ret = %d\n", ret);
        return -PAL_ERROR_DENIED;
    }
    return 0;
}

int sgx_verify_report(sgx_report_t* report) {
    __sgx_mem_aligned sgx_key_request_t keyrequest;
    memset(&keyrequest, 0, sizeof(sgx_key_request_t));
    keyrequest.key_name = REPORT_KEY;
    memcpy(&keyrequest.key_id, &report->key_id, sizeof(keyrequest.key_id));

    sgx_key_128bit_t report_key __attribute__((aligned(sizeof(sgx_key_128bit_t))));
    memset(&report_key, 0, sizeof(report_key));

    int ret = sgx_getkey(&keyrequest, &report_key);
    if (ret) {
        log_error("Can't get report key\n");
        return -PAL_ERROR_DENIED;
    }

    log_debug("Get report key for verification: %s\n", ALLOCA_BYTES2HEXSTR(report_key));

    sgx_mac_t check_mac;
    memset(&check_mac, 0, sizeof(check_mac));

    // Generating the MAC with AES-CMAC using the report key. Only hash the part of the report
    // BEFORE the keyid field (hence the offsetof(...) trick). ENCLU[EREPORT] does not include
    // the MAC and the keyid fields when generating the MAC.
    lib_AESCMAC((uint8_t*)&report_key, sizeof(report_key),
                (uint8_t*)report, offsetof(sgx_report_t, key_id),
                (uint8_t*)&check_mac, sizeof(check_mac));

    // Clear the report key for security
    memset(&report_key, 0, sizeof(report_key));

    log_debug("Verify report:\n");
    print_report(report);
    log_debug("  verify:     %s\n", ALLOCA_BYTES2HEXSTR(check_mac));

    if (memcmp(&check_mac, &report->mac, sizeof(check_mac))) {
        log_error("Report verification failed\n");
        return -PAL_ERROR_DENIED;
    }

    return 0;
}

DEFINE_LISTP(trusted_file);
static LISTP_TYPE(trusted_file) g_trusted_file_list = LISTP_INIT;
static spinlock_t g_trusted_file_lock = INIT_SPINLOCK_UNLOCKED;
static int g_file_check_policy = FILE_CHECK_POLICY_STRICT;

/* assumes `path` is normalized */
static bool path_is_equal_or_subpath(const struct trusted_file* tf, const char* path,
                                     size_t path_len) {
    const char* tf_path = tf->uri + URI_PREFIX_FILE_LEN;
    size_t tf_path_len  = tf->uri_len - URI_PREFIX_FILE_LEN;

    if (tf_path_len > path_len || memcmp(tf_path, path, tf_path_len)) {
        /* tf path is not prefix of `path` */
        return false;
    }
    if (tf_path_len == path_len) {
        /* Both are equal */
        return true;
    }
    if (tf_path[tf_path_len - 1] == '/' || path[tf_path_len] == '/') {
        /* tf path is a subpath of `path` */
        return true;
    }
    return false;
}

struct trusted_file* get_trusted_or_allowed_file(const char* path) {
    struct trusted_file* tf = NULL;

    size_t path_len = strlen(path);

    spinlock_lock(&g_trusted_file_lock);

    struct trusted_file* tmp;
    LISTP_FOR_EACH_ENTRY(tmp, &g_trusted_file_list, list) {
        if (tmp->allowed) {
            /* allowed files: must be a subfolder or file */
            if (path_is_equal_or_subpath(tmp, path, path_len)) {
                tf = tmp;
                break;
            }
        } else {
            /* trusted files: must be exactly the same URI */
            const char* tf_path = tmp->uri + URI_PREFIX_FILE_LEN;
            size_t tf_path_len  = tmp->uri_len - URI_PREFIX_FILE_LEN;
            if (tf_path_len == path_len && !memcmp(tf_path, path, path_len + 1)) {
                tf = tmp;
                break;
            }
        }
    }

    spinlock_unlock(&g_trusted_file_lock);

    return tf;
}

int load_trusted_or_allowed_file(struct trusted_file* tf, PAL_HANDLE file, int create,
                                 sgx_chunk_hash_t** chunk_hashes_ptr, uint64_t* size_ptr,
                                 void** umem) {
    int ret;

    *chunk_hashes_ptr = NULL;
    *size_ptr = 0;
    *umem = NULL;

    if (create) {
        assert(tf->allowed);

        char* uri = malloc(URI_MAX);
        if (!uri)
            return -PAL_ERROR_NOMEM;

        ret = _DkStreamGetName(file, uri, URI_MAX);
        if (ret < 0) {
            free(uri);
            return ret;
        }

        ret = register_trusted_file(uri, /*checksum_str=*/NULL, /*check_duplicates=*/true);

        free(uri);
        return ret;
    }

    if (tf->allowed) {
        /* allowed files: do not need any integrity, so no need for chunk hashes */
        return 0;
    }

    /* trusted files: need integrity, so calculate chunk hashes and compare with hash in manifest */
    if (!file->file.seekable)
        return -PAL_ERROR_DENIED;

    sgx_chunk_hash_t* chunk_hashes = NULL;
    uint8_t* tmp_chunk = NULL; /* scratch buf to calculate whole-file and chunk-of-file hashes */

    /* mmap the whole trusted file in untrusted memory for future reads/writes; it is
     * caller's responsibility to unmap those areas after use */
    *size_ptr = tf->size;
    if (*size_ptr) {
        ret = ocall_mmap_untrusted(umem, tf->size, PROT_READ, MAP_SHARED, file->file.fd,
                                   /*offset=*/0);
        if (ret < 0) {
            *umem = NULL;
            ret = unix_to_pal_error(ret);
            goto fail;
        }
    }

    spinlock_lock(&g_trusted_file_lock);
    if (tf->chunk_hashes) {
        *chunk_hashes_ptr = tf->chunk_hashes;
        spinlock_unlock(&g_trusted_file_lock);
        return 0;
    }
    spinlock_unlock(&g_trusted_file_lock);

    chunk_hashes = malloc(sizeof(sgx_chunk_hash_t) * DIV_ROUND_UP(tf->size, TRUSTED_CHUNK_SIZE));
    if (!chunk_hashes) {
        ret = -PAL_ERROR_NOMEM;
        goto fail;
    }

    tmp_chunk = malloc(TRUSTED_CHUNK_SIZE);
    if (!tmp_chunk) {
        ret = -PAL_ERROR_NOMEM;
        goto fail;
    }

    sgx_chunk_hash_t* chunk_hashes_item = chunk_hashes;
    uint64_t offset = 0;
    LIB_SHA256_CONTEXT file_sha;

    ret = lib_SHA256Init(&file_sha);
    if (ret < 0)
        goto fail;

    for (; offset < tf->size; offset += TRUSTED_CHUNK_SIZE, chunk_hashes_item++) {
        /* For each file chunk of size TRUSTED_CHUNK_SIZE, generate 128-bit hash from SHA-256 hash
         * over contents of this file chunk (we simply truncate SHA-256 hash to first 128 bits; this
         * is fine for integrity purposes). Also, generate a SHA-256 hash for the whole file
         * contents to compare with the manifest "reference" hash value. */
        uint64_t chunk_size = MIN(tf->size - offset, TRUSTED_CHUNK_SIZE);
        LIB_SHA256_CONTEXT chunk_sha;
        ret = lib_SHA256Init(&chunk_sha);
        if (ret < 0)
            goto fail;

        /* to prevent TOCTOU attacks, copy file contents into the enclave before hashing */
        memcpy(tmp_chunk, *umem + offset, chunk_size);

        ret = lib_SHA256Update(&file_sha, tmp_chunk, chunk_size);
        if (ret < 0)
            goto fail;

        ret = lib_SHA256Update(&chunk_sha, tmp_chunk, chunk_size);
        if (ret < 0)
            goto fail;

        sgx_chunk_hash_t chunk_hash[2]; /* each chunk_hash is 128 bits in size */
        ret = lib_SHA256Final(&chunk_sha, (uint8_t*)&chunk_hash[0]);
        if (ret < 0)
            goto fail;

        /* note that we truncate SHA256 to 128 bits */
        memcpy(chunk_hashes_item, &chunk_hash[0], sizeof(*chunk_hashes_item));
    }

    sgx_file_hash_t file_hash;
    ret = lib_SHA256Final(&file_sha, file_hash.bytes);
    if (ret < 0)
        goto fail;

    /* check the generated hash-over-whole-file against the reference hash in the manifest */
    if (memcmp(&file_hash, &tf->file_hash, sizeof(file_hash))) {
        ret = -PAL_ERROR_DENIED;
        goto fail;
    }

    spinlock_lock(&g_trusted_file_lock);
    if (tf->chunk_hashes) {
        *chunk_hashes_ptr = tf->chunk_hashes;
        spinlock_unlock(&g_trusted_file_lock);
        free(chunk_hashes);
        free(tmp_chunk);
        return 0;
    }
    tf->chunk_hashes = chunk_hashes;
    *chunk_hashes_ptr = chunk_hashes;
    spinlock_unlock(&g_trusted_file_lock);

    free(tmp_chunk);
    return 0;

fail:
    if (*umem) {
        assert(*size_ptr > 0);
        ocall_munmap_untrusted(*umem, *size_ptr);
    }
    free(chunk_hashes);
    free(tmp_chunk);
    return ret;
}

int get_file_check_policy(void) {
    return g_file_check_policy;
}

static void set_file_check_policy(int policy) {
    g_file_check_policy = policy;
}

int copy_and_verify_trusted_file(const char* path, uint8_t* buf, const void* umem,
                                 off_t aligned_offset, off_t aligned_end, off_t offset, off_t end,
                                 sgx_chunk_hash_t* chunk_hashes, size_t file_size) {
    int ret = 0;

    assert(IS_ALIGNED(aligned_offset, TRUSTED_CHUNK_SIZE));
    assert(offset >= aligned_offset && end <= aligned_end);

    uint8_t* tmp_chunk = malloc(TRUSTED_CHUNK_SIZE);
    if (!tmp_chunk) {
        ret = -PAL_ERROR_NOMEM;
        goto failed;
    }

    sgx_chunk_hash_t* chunk_hashes_item = chunk_hashes + aligned_offset / TRUSTED_CHUNK_SIZE;

    uint8_t* buf_pos = buf;
    off_t chunk_offset = aligned_offset;
    for (; chunk_offset < aligned_end; chunk_offset += TRUSTED_CHUNK_SIZE, chunk_hashes_item++) {
        size_t chunk_size = MIN(file_size - chunk_offset, TRUSTED_CHUNK_SIZE);
        off_t chunk_end   = chunk_offset + chunk_size;

        sgx_chunk_hash_t chunk_hash[2]; /* each chunk_hash is 128 bits in size but we need 256 */

        LIB_SHA256_CONTEXT chunk_sha;
        ret = lib_SHA256Init(&chunk_sha);
        if (ret < 0)
            goto failed;

        if (chunk_offset >= offset && chunk_end <= end) {
            /* if current chunk-to-copy completely resides in the requested region-to-copy,
             * directly copy into buf (without a scratch buffer) and hash in-place */
            memcpy(buf_pos, umem + chunk_offset, chunk_size);

            ret = lib_SHA256Update(&chunk_sha, buf_pos, chunk_size);
            if (ret < 0)
                goto failed;

            buf_pos += chunk_size;
        } else {
            /* if current chunk-to-copy only partially overlaps with the requested region-to-copy,
             * read the file contents into a scratch buffer, verify hash and then copy only the part
             * needed by the caller */
            memcpy(tmp_chunk, umem + chunk_offset, chunk_size);

            ret = lib_SHA256Update(&chunk_sha, tmp_chunk, chunk_size);
            if (ret < 0)
                goto failed;

            /* determine which part of the chunk is needed by the caller */
            off_t copy_start = MAX(chunk_offset, offset);
            off_t copy_end   = MIN(chunk_offset + (off_t)chunk_size, end);
            assert(copy_end > copy_start);

            memcpy(buf_pos, tmp_chunk + copy_start - chunk_offset, copy_end - copy_start);
            buf_pos += copy_end - copy_start;
        }

        ret = lib_SHA256Final(&chunk_sha, (uint8_t*)&chunk_hash[0]);
        if (ret < 0)
            goto failed;

        if (memcmp(chunk_hashes_item, &chunk_hash[0], sizeof(*chunk_hashes_item))) {
            log_error("Accessing file '%s' is denied: incorrect hash of file chunk at %lu-%lu.\n",
                      path, chunk_offset, chunk_end);
            ret = -PAL_ERROR_DENIED;
            goto failed;
        }
    }

    free(tmp_chunk);
    return 0;

failed:
    free(tmp_chunk);
    memset(buf, 0, end - offset);
    return ret;
}

static int register_trusted_file(const char* uri, const char* checksum_str, bool check_duplicates) {
    int ret;

    size_t uri_len = strlen(uri);
    if (uri_len >= URI_MAX) {
        log_error("Size of file exceeds maximum %dB: %s\n", URI_MAX, uri);
        return -PAL_ERROR_INVAL;
    }

    if (check_duplicates) {
        /* this check is only done during runtime (when creating a new file) and not needed during
         * initialization (because manifest is assumed to have no duplicates); skipping this check
         * significantly improves startup time */
        spinlock_lock(&g_trusted_file_lock);
        struct trusted_file* tf;
        LISTP_FOR_EACH_ENTRY(tf, &g_trusted_file_list, list) {
            if (tf->uri_len == uri_len && !memcmp(tf->uri, uri, uri_len)) {
                spinlock_unlock(&g_trusted_file_lock);
                return 0;
            }
        }
        spinlock_unlock(&g_trusted_file_lock);
    }

    struct trusted_file* new = malloc(sizeof(*new) + uri_len + 1);
    if (!new)
        return -PAL_ERROR_NOMEM;

    INIT_LIST_HEAD(new, list);
    new->size = 0;
    new->chunk_hashes = NULL;
    new->allowed = false;
    new->uri_len = uri_len;
    memcpy(new->uri, uri, uri_len + 1);

    if (checksum_str) {
        PAL_STREAM_ATTR attr;
        ret = _DkStreamAttributesQuery(uri, &attr);
        if (ret < 0) {
            log_error("Could not find size of file: %s\n", uri);
            free(new);
            return ret;
        }
        new->size = attr.pending_size;

        assert(strlen(checksum_str) >= sizeof(sgx_file_hash_t) * 2);
        for (size_t i = 0; i < sizeof(sgx_file_hash_t); i++) {
            int8_t byte1 = hex2dec(checksum_str[i * 2]);
            int8_t byte2 = hex2dec(checksum_str[i * 2 + 1]);

            if (byte1 < 0 || byte2 < 0) {
                log_error("Could not parse checksum of file: %s\n", uri);
                free(new);
                return -PAL_ERROR_INVAL;
            }

            new->file_hash.bytes[i] = byte1 * 16 + byte2;
        }
    } else {
        memset(&new->file_hash, 0, sizeof(new->file_hash));
        new->allowed = true;
    }

    spinlock_lock(&g_trusted_file_lock);

    if (check_duplicates) {
        /* this check is only done during runtime and not needed during initialization (see above);
         * we check again because same file could have been added by another thread in meantime */
        struct trusted_file* tf;
        LISTP_FOR_EACH_ENTRY(tf, &g_trusted_file_list, list) {
            if (tf->uri_len == uri_len && !memcmp(tf->uri, uri, uri_len)) {
                spinlock_unlock(&g_trusted_file_lock);
                free(new);
                return 0;
            }
        }
    }

    LISTP_ADD_TAIL(new, &g_trusted_file_list, list);
    spinlock_unlock(&g_trusted_file_lock);

    return 0;
}

static int init_trusted_file(const char* key, const char* uri) {
    int ret;
    char* normpath = NULL;

    /* read sgx.trusted_checksum.<key> entry from manifest */
    char* fullkey = alloc_concat3("sgx.trusted_checksum.\"", -1, key, -1, "\"", -1);
    if (!fullkey)
        return -PAL_ERROR_NOMEM;

    /* NOTE: sgx.trusted_checksum entries are actually SHA-256 hashes, so the better name would be
     * sgx.trusted_hash but we don't want to break old manifests so we keep the legacy name */
    char* trusted_checksum_str = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, fullkey, &trusted_checksum_str);
    if (ret < 0) {
        log_error("Cannot parse '%s'\n", fullkey);
        ret = -PAL_ERROR_INVAL;
        goto out;
    }
    if (!trusted_checksum_str) {
        log_error("Missing '%s' entry\n", fullkey);
        ret = -PAL_ERROR_INVAL;
        goto out;
    }

    /* Normalize the uri */
    const size_t normpath_size = URI_MAX;
    normpath = malloc(normpath_size);
    if (!normpath) {
        ret = -PAL_ERROR_NOMEM;
        goto out;
    }
    (void)strcpy_static(normpath, URI_PREFIX_FILE, normpath_size);

    if (!strstartswith(uri, URI_PREFIX_FILE)) {
        log_error("Invalid URI [%s]: Trusted files must start with 'file:'\n", uri);
        ret = -PAL_ERROR_INVAL;
        goto out;
    }
    size_t len = normpath_size - strlen(normpath);
    ret = get_norm_path(uri + URI_PREFIX_FILE_LEN, normpath + URI_PREFIX_FILE_LEN, &len);
    if (ret < 0) {
        log_error("Path (%s) normalization failed: %s\n", uri + URI_PREFIX_FILE_LEN,
                  pal_strerror(ret));
        goto out;
    }

    ret = register_trusted_file(normpath, trusted_checksum_str, /*check_duplicates=*/false);
out:
    free(normpath);
    free(trusted_checksum_str);
    free(fullkey);
    return ret;
}

int init_trusted_files(void) {
    int ret;

    /* read loader.preload string from manifest and register its files as trusted */
    char* preload_str = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "loader.preload", &preload_str);
    if (ret < 0) {
        log_error("Cannot parse \'loader.preload\' "
                  "(the value must be put in double quotes!)\n");
        return -PAL_ERROR_INVAL;
    }

    if (preload_str) {
        int npreload = 0;
        char key[20];
        const char* start;
        const char* end;
        size_t len = strlen(preload_str);

        for (start = preload_str; start < preload_str + len; start = end + 1) {
            for (end = start; end < preload_str + len && *end && *end != ','; end++)
                ;
            if (end > start) {
                char uri[end - start + 1];
                memcpy(uri, start, end - start);
                uri[end - start] = 0;
                snprintf(key, 20, "preload%d", npreload++);

                ret = init_trusted_file(key, uri);
                if (ret < 0) {
                    free(preload_str);
                    return ret;
                }
            }
        }

        free(preload_str);
    }

    /* read sgx.trusted_files entries from manifest and register them */
    toml_table_t* manifest_sgx = toml_table_in(g_pal_state.manifest_root, "sgx");
    if (!manifest_sgx)
        goto no_trusted;

    toml_table_t* toml_trusted_files = toml_table_in(manifest_sgx, "trusted_files");
    if (!toml_trusted_files)
        goto no_trusted;

    ssize_t toml_trusted_files_cnt = toml_table_nkval(toml_trusted_files);
    if (toml_trusted_files_cnt <= 0)
        goto no_trusted;

    for (ssize_t i = 0; i < toml_trusted_files_cnt; i++) {
        const char* toml_trusted_file_key = toml_key_in(toml_trusted_files, i);
        assert(toml_trusted_file_key);
        toml_raw_t toml_trusted_file_raw = toml_raw_in(toml_trusted_files, toml_trusted_file_key);
        assert(toml_trusted_file_raw);

        char* toml_trusted_file_str = NULL;
        ret = toml_rtos(toml_trusted_file_raw, &toml_trusted_file_str);
        if (ret < 0) {
            log_error("Invalid trusted file in manifest: \'%s\'\n", toml_trusted_file_key);
            continue;
        }

        ret = init_trusted_file(toml_trusted_file_key, toml_trusted_file_str);
        if (ret < 0) {
            free(toml_trusted_file_str);
            return ret;
        }

        free(toml_trusted_file_str);
    }

no_trusted:
    ret = 0;
    char* norm_path = NULL;

    /* read sgx.allowed_files entries from manifest and register them */
    if (!manifest_sgx)
        goto no_allowed;

    toml_table_t* toml_allowed_files = toml_table_in(manifest_sgx, "allowed_files");
    if (!toml_allowed_files)
        goto no_allowed;

    ssize_t toml_allowed_files_cnt = toml_table_nkval(toml_allowed_files);
    if (toml_allowed_files_cnt <= 0)
        goto no_allowed;

    const size_t norm_path_size = URI_MAX;
    norm_path = malloc(norm_path_size);
    if (!norm_path) {
        ret = -PAL_ERROR_NOMEM;
        goto no_allowed;
    }

    for (ssize_t i = 0; i < toml_allowed_files_cnt; i++) {
        const char* toml_allowed_file_key = toml_key_in(toml_allowed_files, i);
        assert(toml_allowed_file_key);
        toml_raw_t toml_allowed_file_raw = toml_raw_in(toml_allowed_files, toml_allowed_file_key);
        assert(toml_allowed_file_raw);

        char* toml_allowed_file_str = NULL;
        ret = toml_rtos(toml_allowed_file_raw, &toml_allowed_file_str);
        if (ret < 0) {
            log_error("Invalid allowed file in manifest: \'%s\'\n", toml_allowed_file_key);
            continue;
        }

        if (!strstartswith(toml_allowed_file_str, URI_PREFIX_FILE)) {
            log_error("Invalid URI [%s]: Allowed files must start with 'file:'\n",
                      toml_allowed_file_str);
            free(toml_allowed_file_str);
            ret = -PAL_ERROR_INVAL;
            goto no_allowed;
        }
        assert(norm_path_size > URI_PREFIX_FILE_LEN);
        memcpy(norm_path, URI_PREFIX_FILE, URI_PREFIX_FILE_LEN);

        size_t norm_path_len = norm_path_size - URI_PREFIX_FILE_LEN;

        ret = get_norm_path(toml_allowed_file_str + URI_PREFIX_FILE_LEN,
                            norm_path + URI_PREFIX_FILE_LEN, &norm_path_len);

        if (ret < 0) {
            log_error("Path (%s) normalization failed: %s\n",
                      toml_allowed_file_str + URI_PREFIX_FILE_LEN, pal_strerror(ret));
            free(toml_allowed_file_str);
            goto no_allowed;
        }
        free(toml_allowed_file_str);

        register_trusted_file(norm_path, NULL, /*check_duplicates=*/false);
    }

    ret = 0;

no_allowed:
    free(norm_path);
    return ret;
}

int init_file_check_policy(void) {
    int ret;

    char* file_check_policy_str = NULL;
    ret = toml_string_in(g_pal_state.manifest_root, "sgx.file_check_policy",
                         &file_check_policy_str);
    if (ret < 0) {
        log_error("Cannot parse \'sgx.file_check_policy\' "
                  "(the value must be put in double quotes!)\n");
        return -PAL_ERROR_INVAL;
    }

    if (!file_check_policy_str)
        return 0;

    if (!strcmp(file_check_policy_str, "strict")) {
        set_file_check_policy(FILE_CHECK_POLICY_STRICT);
    } else if (!strcmp(file_check_policy_str, "allow_all_but_log")) {
        set_file_check_policy(FILE_CHECK_POLICY_ALLOW_ALL_BUT_LOG);
    } else {
        log_error("Unknown value for \'sgx.file_check_policy\' "
                  "(allowed: `strict`, `allow_all_but_log`)'\n");
        free(file_check_policy_str);
        return -PAL_ERROR_INVAL;
    }

    log_debug("File check policy: %s\n", file_check_policy_str);
    free(file_check_policy_str);
    return 0;
}

int init_enclave(void) {
    // Get report to initialize info (MR_ENCLAVE, etc.) about this enclave from
    // a trusted source.

    // Since this report is only read by ourselves we can
    // leave targetinfo zeroed.
    __sgx_mem_aligned sgx_target_info_t targetinfo = {0};
    __sgx_mem_aligned sgx_report_data_t reportdata = {0};
    __sgx_mem_aligned sgx_report_t report;

    int ret = sgx_report(&targetinfo, &reportdata, &report);
    if (ret) {
        log_error("failed to get self report: %d\n", ret);
        return -PAL_ERROR_INVAL;
    }

    memcpy(&g_pal_sec.mr_enclave, &report.body.mr_enclave, sizeof(g_pal_sec.mr_enclave));
    memcpy(&g_pal_sec.mr_signer, &report.body.mr_signer, sizeof(g_pal_sec.mr_signer));
    g_pal_sec.enclave_attributes = report.body.attributes;

    return 0;
}

int _DkStreamKeyExchange(PAL_HANDLE stream, PAL_SESSION_KEY* key) {
    uint8_t pub[DH_SIZE];
    uint8_t agree[DH_SIZE];
    PAL_NUM pubsz, agreesz;
    LIB_DH_CONTEXT context;
    int64_t bytes;
    int64_t ret;

    assert(IS_HANDLE_TYPE(stream, process));

    ret = lib_DhInit(&context);
    if (ret < 0) {
        log_error("Key Exchange: DH Init failed: %ld\n", ret);
        goto out_no_final;
    }

    pubsz = sizeof(pub);
    ret = lib_DhCreatePublic(&context, pub, &pubsz);
    if (ret < 0) {
        log_error("Key Exchange: DH CreatePublic failed: %ld\n", ret);
        goto out;
    }

    assert(pubsz > 0 && pubsz <= DH_SIZE);
    if (pubsz < DH_SIZE) {
        /* Insert leading zero bytes if necessary. These values are big-
         * endian, so we either need to know the length of the bignum or
         * zero-pad at the beginning instead of the end. This code chooses
         * to do the latter. */
        memmove(pub + (DH_SIZE - pubsz), pub, pubsz);
        memset(pub, 0, DH_SIZE - pubsz);
    }

    for (bytes = 0, ret = 0; bytes < DH_SIZE; bytes += ret) {
        ret = _DkStreamWrite(stream, 0, DH_SIZE - bytes, pub + bytes, NULL, 0);
        if (ret < 0) {
            if (ret == -PAL_ERROR_INTERRUPTED || ret == -PAL_ERROR_TRYAGAIN) {
                ret = 0;
                continue;
            }
            log_error("Failed to exchange the secret key via RPC: %ld\n", ret);
            goto out;
        }
    }

    for (bytes = 0, ret = 0; bytes < DH_SIZE; bytes += ret) {
        ret = _DkStreamRead(stream, 0, DH_SIZE - bytes, pub + bytes, NULL, 0);
        if (ret < 0) {
            if (ret == -PAL_ERROR_INTERRUPTED || ret == -PAL_ERROR_TRYAGAIN) {
                ret = 0;
                continue;
            }
            log_error("Failed to exchange the secret key via RPC: %ld\n", ret);
            goto out;
        }
    }

    agreesz = sizeof(agree);
    ret = lib_DhCalcSecret(&context, pub, DH_SIZE, agree, &agreesz);
    if (ret < 0) {
        log_error("Key Exchange: DH CalcSecret failed: %ld\n", ret);
        goto out;
    }

    assert(agreesz > 0 && agreesz <= sizeof(agree));

    ret = lib_HKDF_SHA256(agree, agreesz, /*salt=*/NULL, /*salt_size=*/0, /*info=*/NULL,
                          /*info_size=*/0, (uint8_t*)key, sizeof(*key));
    if (ret < 0) {
        log_error("Failed to derive the session key: %ld\n", ret);
        goto out;
    }

    log_debug("Key exchange succeeded: %s\n", ALLOCA_BYTES2HEXSTR(*key));
    ret = 0;
out:
    lib_DhFinal(&context);
out_no_final:
    return ret;
}

/*
 * Initalize the request of local report exchange.
 *
 * We refer to this enclave as A and to the other enclave as B, e.g., A is this
 * parent enclave and B is the child enclave in the fork case (for more info,
 * see comments in db_process.c).
 */
int _DkStreamReportRequest(PAL_HANDLE stream, sgx_report_data_t* sgx_report_data) {
    __sgx_mem_aligned sgx_target_info_t target_info;
    __sgx_mem_aligned sgx_report_t report;
    uint64_t bytes;
    int64_t ret;

    assert(IS_HANDLE_TYPE(stream, process));

    /* A -> B: targetinfo[A] */
    memset(&target_info, 0, sizeof(target_info));
    memcpy(&target_info.mr_enclave, &g_pal_sec.mr_enclave, sizeof(sgx_measurement_t));
    memcpy(&target_info.attributes, &g_pal_sec.enclave_attributes, sizeof(sgx_attributes_t));

    for (bytes = 0, ret = 0; bytes < SGX_TARGETINFO_FILLED_SIZE; bytes += ret) {
        ret = _DkStreamWrite(stream, 0, SGX_TARGETINFO_FILLED_SIZE - bytes,
                             ((void*)&target_info) + bytes, NULL, 0);
        if (ret < 0) {
            if (ret == -PAL_ERROR_INTERRUPTED || ret == -PAL_ERROR_TRYAGAIN) {
                ret = 0;
                continue;
            }
            log_error("Failed to send target info via RPC: %ld\n", ret);
            goto out;
        }
    }

    /* B -> A: report[B -> A] */
    for (bytes = 0, ret = 0; bytes < sizeof(report); bytes += ret) {
        ret = _DkStreamRead(stream, 0, sizeof(report) - bytes, ((void*)&report) + bytes, NULL, 0);
        if (ret < 0) {
            if (ret == -PAL_ERROR_INTERRUPTED || ret == -PAL_ERROR_TRYAGAIN) {
                ret = 0;
                continue;
            }
            log_error("Failed to receive local report via RPC: %ld\n", ret);
            goto out;
        }
    }

    log_debug("Received local report (mr_enclave = %s)\n",
              ALLOCA_BYTES2HEXSTR(report.body.mr_enclave.m));

    /* Verify report[B -> A] */
    ret = sgx_verify_report(&report);
    if (ret < 0) {
        log_error("Failed to verify local report: %ld\n", ret);
        goto out;
    }

    if (!is_remote_enclave_ok(&stream->process.session_key, &report.body.mr_enclave,
                              &report.body.report_data)) {
        log_error("Not an allowed enclave (mr_enclave = %s)\n",
                  ALLOCA_BYTES2HEXSTR(report.body.mr_enclave.m));
        ret = -PAL_ERROR_DENIED;
        goto out;
    }

    log_debug("Local attestation succeeded!\n");

    /* A -> B: report[A -> B] */
    memcpy(&target_info.mr_enclave, &report.body.mr_enclave, sizeof(sgx_measurement_t));
    memcpy(&target_info.attributes, &report.body.attributes, sizeof(sgx_attributes_t));

    ret = sgx_get_report(&target_info, sgx_report_data, &report);
    if (ret < 0) {
        log_error("Failed to get local report from CPU: %ld\n", ret);
        goto out;
    }

    for (bytes = 0, ret = 0; bytes < sizeof(report); bytes += ret) {
        ret = _DkStreamWrite(stream, 0, sizeof(report) - bytes, ((void*)&report) + bytes, NULL, 0);
        if (ret < 0) {
            if (ret == -PAL_ERROR_INTERRUPTED || ret == -PAL_ERROR_TRYAGAIN) {
                ret = 0;
                continue;
            }
            log_error("Failed to send local report via RPC: %ld\n", ret);
            goto out;
        }
    }

    return 0;

out:
    _DkStreamDelete(stream, 0);
    return ret;
}

/*
 * Respond to the request of local report exchange.
 *
 * We refer to this enclave as B and to the other enclave as A, e.g., B is this
 * child enclave and A is the parent enclave in the fork case (for more info,
 * see comments in db_process.c).
 */
int _DkStreamReportRespond(PAL_HANDLE stream, sgx_report_data_t* sgx_report_data) {
    __sgx_mem_aligned sgx_target_info_t target_info;
    __sgx_mem_aligned sgx_report_t report;
    uint64_t bytes;
    int64_t ret;

    assert(IS_HANDLE_TYPE(stream, process));

    memset(&target_info, 0, sizeof(target_info));

    /* A -> B: targetinfo[A] */
    for (bytes = 0, ret = 0; bytes < SGX_TARGETINFO_FILLED_SIZE; bytes += ret) {
        ret = _DkStreamRead(stream, 0, SGX_TARGETINFO_FILLED_SIZE - bytes,
                            ((void*)&target_info) + bytes, NULL, 0);
        if (ret < 0) {
            if (ret == -PAL_ERROR_INTERRUPTED || ret == -PAL_ERROR_TRYAGAIN) {
                ret = 0;
                continue;
            }
            log_error("Failed to receive target info via RPC: %ld\n", ret);
            goto out;
        }
    }

    /* B -> A: report[B -> A] */
    ret = sgx_get_report(&target_info, sgx_report_data, &report);
    if (ret < 0) {
        log_error("Failed to get local report from CPU: %ld\n", ret);
        goto out;
    }

    for (bytes = 0, ret = 0; bytes < sizeof(report); bytes += ret) {
        ret = _DkStreamWrite(stream, 0, sizeof(report) - bytes, ((void*)&report) + bytes, NULL, 0);
        if (ret < 0) {
            if (ret == -PAL_ERROR_INTERRUPTED || ret == -PAL_ERROR_TRYAGAIN) {
                ret = 0;
                continue;
            }
            log_error("Failed to send local report via PRC: %ld\n", ret);
            goto out;
        }
    }

    /* A -> B: report[A -> B] */
    for (bytes = 0, ret = 0; bytes < sizeof(report); bytes += ret) {
        ret = _DkStreamRead(stream, 0, sizeof(report) - bytes, ((void*)&report) + bytes, NULL, 0);
        if (ret < 0) {
            if (ret == -PAL_ERROR_INTERRUPTED || ret == -PAL_ERROR_TRYAGAIN) {
                ret = 0;
                continue;
            }
            log_error("Failed to receive local report via RPC: %ld\n", ret);
            goto out;
        }
    }

    log_debug("Received local report (mr_enclave = %s)\n",
            ALLOCA_BYTES2HEXSTR(report.body.mr_enclave.m));

    /* Verify report[A -> B] */
    ret = sgx_verify_report(&report);
    if (ret < 0) {
        log_error("Failed to verify local report: %ld\n", ret);
        goto out;
    }

    if (!is_remote_enclave_ok(&stream->process.session_key, &report.body.mr_enclave,
                              &report.body.report_data)) {
        log_error("Not an allowed enclave (mr_enclave = %s)\n",
                  ALLOCA_BYTES2HEXSTR(report.body.mr_enclave.m));
        ret = -PAL_ERROR_DENIED;
        goto out;
    }

    log_debug("Local attestation succeeded!\n");
    return 0;

out:
    _DkStreamDelete(stream, 0);
    return ret;
}

int _DkStreamSecureInit(PAL_HANDLE stream, bool is_server, PAL_SESSION_KEY* session_key,
                        LIB_SSL_CONTEXT** out_ssl_ctx, const uint8_t* buf_load_ssl_ctx,
                        size_t buf_size) {
    int stream_fd;

    if (IS_HANDLE_TYPE(stream, process))
        stream_fd = stream->process.stream;
    else if (IS_HANDLE_TYPE(stream, pipe) || IS_HANDLE_TYPE(stream, pipecli))
        stream_fd = stream->pipe.fd;
    else
        return -PAL_ERROR_BADHANDLE;

    LIB_SSL_CONTEXT* ssl_ctx = malloc(sizeof(*ssl_ctx));
    if (!ssl_ctx)
        return -PAL_ERROR_NOMEM;

    /* mbedTLS init routines are not thread safe, so we use a spinlock to protect them */
    static spinlock_t ssl_init_lock = INIT_SPINLOCK_UNLOCKED;

    spinlock_lock(&ssl_init_lock);
    int ret = lib_SSLInit(ssl_ctx, stream_fd, is_server,
                          (const uint8_t*)session_key, sizeof(*session_key),
                          ocall_read, ocall_write, buf_load_ssl_ctx, buf_size);
    spinlock_unlock(&ssl_init_lock);

    if (ret != 0) {
        free(ssl_ctx);
        return ret;
    }

    if (!buf_load_ssl_ctx) {
        /* TLS context was not restored from the buffer, need to perform handshake */
        ret = lib_SSLHandshake(ssl_ctx);
        if (ret != 0) {
            free(ssl_ctx);
            return ret;
        }
    }

    *out_ssl_ctx = ssl_ctx;
    return 0;
}

int _DkStreamSecureFree(LIB_SSL_CONTEXT* ssl_ctx) {
    lib_SSLFree(ssl_ctx);
    free(ssl_ctx);
    return 0;
}

int _DkStreamSecureRead(LIB_SSL_CONTEXT* ssl_ctx, uint8_t* buf, size_t len, bool is_blocking) {
    int ret = lib_SSLRead(ssl_ctx, buf, len);
    if (is_blocking && ret == -PAL_ERROR_TRYAGAIN) {
        /* mbedTLS wrappers collapse host errors `EAGAIN` and `EINTR` into one error PAL
         * (`PAL_ERROR_TRYAGAIN`). We use the fact that blocking reads do not return `EAGAIN` to
         * split it back. */
        return -PAL_ERROR_INTERRUPTED;
    }
    return ret;
}

int _DkStreamSecureWrite(LIB_SSL_CONTEXT* ssl_ctx, const uint8_t* buf, size_t len,
                         bool is_blocking) {
    int ret = lib_SSLWrite(ssl_ctx, buf, len);
    if (is_blocking && ret == -PAL_ERROR_TRYAGAIN) {
        /* See the explanation in `_DkStreamSecureRead`. */
        return -PAL_ERROR_INTERRUPTED;
    }
    return ret;
}

int _DkStreamSecureSave(LIB_SSL_CONTEXT* ssl_ctx, const uint8_t** obuf, size_t* olen) {
    assert(obuf);
    assert(olen);

    int ret;

    /* figure out the required buffer size */
    ret = lib_SSLSave(ssl_ctx, NULL, 0, olen);
    if (ret != 0 && ret != -PAL_ERROR_NOMEM)
        return ret;

    /* create the required buffer */
    size_t len   = *olen;
    uint8_t* buf = malloc(len);
    if (!buf)
        return -PAL_ERROR_NOMEM;

    /* now have buffer with sufficient size to save serialized context */
    ret = lib_SSLSave(ssl_ctx, buf, len, olen);
    if (ret != 0 || len != *olen) {
        free(buf);
        return -PAL_ERROR_DENIED;
    }

    *obuf = buf;
    return 0;
}
