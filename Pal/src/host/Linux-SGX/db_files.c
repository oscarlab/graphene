/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * This file contains operands to handle streams with URIs that start with "file:" or "dir:".
 */

#include <asm/fcntl.h>
#include <asm/stat.h>
#include <linux/fs.h>
#include <linux/types.h>

#include "api.h"
#include "pal.h"
#include "pal_defs.h"
#include "pal_error.h"
#include "pal_flags_conv.h"
#include "pal_internal.h"
#include "pal_linux.h"
#include "pal_linux_defs.h"
#include "pal_linux_error.h"
#include "perm.h"
#include "stat.h"

#include "enclave_pages.h"

/* this macro is used to emulate mmap() via pread() in chunks of 128MB (mmapped files may be many
 * GBs in size, and a pread OCALL could fail with -ENOMEM, so we cap to reasonably small size) */
#define MAX_READ_SIZE (PRESET_PAGESIZE * 1024 * 32)

static int pf_file_open(struct protected_file* pf, const char* path, PAL_HANDLE handle, int access,
                        int share, int create, int options) {
    int ret;
    int fd = -1;
    pf_status_t pfs;

    /* whether to re-initialize the PF */
    bool pf_create = (create & PAL_CREATE_ALWAYS) || (create & PAL_CREATE_TRY);

    pf_file_mode_t pf_mode = 0;
    if ((access & PAL_ACCESS_RDWR) == PAL_ACCESS_RDWR)
        pf_mode = PF_FILE_MODE_READ | PF_FILE_MODE_WRITE;
    else if ((access & PAL_ACCESS_WRONLY) == PAL_ACCESS_WRONLY)
        pf_mode = PF_FILE_MODE_WRITE;
    else
        pf_mode = PF_FILE_MODE_READ;

    if (!pf->context) {
        /* there is no associated PF context (and thus no underlying host file descriptor), open a
         * file at host level and register a PF context */
        assert(pf->refcount == 0);
        assert(pf->host_fd == -1);

        /* always open underlying host file as read-write; PF logic restricts the actual allowed
         * operations based on pf_mode */
        fd = ocall_open(path, O_RDWR |
                              PAL_CREATE_TO_LINUX_OPEN(create) |
                              PAL_OPTION_TO_LINUX_OPEN(options),
                        share);
        if (fd < 0) {
            ret = unix_to_pal_error(fd);
            goto fail;
        }

        pf->host_fd = fd;

        /* get real file size */
        struct stat st;
        ret = ocall_fstat(fd, &st);
        if (ret < 0) {
            log_error("file_open(%s): fstat failed: %d\n", path, ret);
            ret = unix_to_pal_error(ret);
            goto fail;
        }

        struct protected_file* ret_pf = load_protected_file(path, &pf->host_fd, st.st_size, pf_mode,
                                                            pf_create, pf);
        if (!ret_pf) {
            log_error("load_protected_file(%s, %d) failed\n", path, pf->host_fd);
            ret = -PAL_ERROR_DENIED;
            goto fail;
        }
    } else {
        /* there is already an associated PF context (and thus an already-opened host file
         * descriptor), we only need to check and extend the PF access mode if needed */
        assert(pf->refcount > 0);

        pf_file_mode_t old_pf_mode;
        pfs = pf_get_mode(pf->context, &old_pf_mode);
        if (PF_FAILURE(pfs)) {
            log_error("pf_file_open(%s): pf_get_mode failed\n", path);
            ret = -PAL_ERROR_DENIED;
            goto fail;
        }

        /* disallow opening more than one writable handle to a PF */
        if ((pf_mode & PF_FILE_MODE_WRITE) && (old_pf_mode & PF_FILE_MODE_WRITE)) {
            log_error("pf_file_open(%s): disallowing concurrent writable handle\n", path);
            ret = -PAL_ERROR_DENIED;
            goto fail;
        }

        pfs = pf_set_mode(pf->context, old_pf_mode | pf_mode);
        __UNUSED(pfs);
        assert(PF_SUCCESS(pfs));
    }


    if (handle) {
        handle->file.seekable = true;
        handle->file.realpath = (PAL_STR)path;
        handle->file.fd       = pf->host_fd;
    }

    pf->refcount++;
    return 0;

fail:
    if (pf->context && pf->refcount == 0) {
        /* note the refcount check: there could be an already-opened PF, so we shouldn't unload */
        unload_protected_file(pf);
    }
    if (fd >= 0)
        ocall_close(fd);
    return ret;
}

static int file_open(PAL_HANDLE* handle, const char* type, const char* uri, int access, int share,
                     int create, int options) {
    if (strcmp(type, URI_TYPE_FILE))
        return -PAL_ERROR_INVAL;

    int ret;
    size_t uri_size = strlen(uri) + 1;

    /* allocate file handle (including enough space to hold its URI string) */
    PAL_HANDLE hdl = calloc(1, HANDLE_SIZE(file) + uri_size);
    if (!hdl)
        return -PAL_ERROR_NOMEM;

    SET_HANDLE_TYPE(hdl, file);
    HANDLE_HDR(hdl)->flags |= RFD(0) | WFD(0);

    char* path = (char*)hdl + HANDLE_SIZE(file);
    if ((ret = get_norm_path(uri, path, &uri_size)) < 0) {
        log_error("Could not normalize path (%s): %s\n", uri, pal_strerror(ret));
        free(hdl);
        return ret;
    }

    struct protected_file* pf = get_protected_file(path);
    if (pf) {
        ret = pf_file_open(pf, path, hdl, access, share, create, options);
        if (ret < 0) {
            free(hdl);
            return ret;
        }

        *handle = hdl;
        return 0;
    }

    /* not a protected file: must be trusted or allowed file */
    int fd = ocall_open(path, PAL_ACCESS_TO_LINUX_OPEN(access)  |
                              PAL_CREATE_TO_LINUX_OPEN(create)  |
                              PAL_OPTION_TO_LINUX_OPEN(options),
                        share);
    if (fd < 0) {
        ret = unix_to_pal_error(fd);
        goto fail;
    }

    struct stat st;
    ret = ocall_fstat(fd, &st);
    if (ret < 0) {
        log_error("file_open(%s): fstat failed: %d\n", path, ret);
        ret = unix_to_pal_error(ret);
        goto fail;
    }

    hdl->file.seekable = !S_ISFIFO(st.st_mode);
    hdl->file.realpath = (PAL_STR)path;
    hdl->file.fd       = fd;

    sgx_chunk_hash_t* chunk_hashes;
    uint64_t total;
    void* umem;
    ret = load_trusted_file(hdl, &chunk_hashes, &total, create, &umem);
    if (ret < 0) {
        log_error("Accessing file:%s is denied (%s). This file is not trusted or allowed."
                  " Trusted files should be regular files (seekable).\n", hdl->file.realpath,
                  pal_strerror(ret));
        goto fail;
    }

    hdl->file.chunk_hashes = (PAL_PTR)chunk_hashes;
    hdl->file.total = total;
    hdl->file.umem  = umem;

    *handle = hdl;
    return 0;

fail:
    free(hdl);
    if (fd >= 0)
        ocall_close(fd);
    return ret;
}

static int64_t pf_file_read(struct protected_file* pf, PAL_HANDLE handle, uint64_t offset,
                            uint64_t count, void* buffer) {
    int fd = handle->file.fd;

    if (!pf->context) {
        log_error("pf_file_read(PF fd %d): PF not initialized\n", fd);
        return -PAL_ERROR_BADHANDLE;
    }

    size_t bytes_read = 0;
    pf_status_t pfs = pf_read(pf->context, offset, count, buffer, &bytes_read);

    if (PF_FAILURE(pfs)) {
        log_error("pf_file_read(PF fd %d): pf_read failed: %s\n", fd, pf_strerror(pfs));
        return -PAL_ERROR_DENIED;
    }

    return bytes_read;
}

static int64_t file_read(PAL_HANDLE handle, uint64_t offset, uint64_t count, void* buffer) {
    struct protected_file* pf = find_protected_file_handle(handle);

    if (pf)
        return pf_file_read(pf, handle, offset, count, buffer);

    int64_t ret;
    sgx_chunk_hash_t* chunk_hashes = (sgx_chunk_hash_t*)handle->file.chunk_hashes;

    if (!chunk_hashes) {
        if (handle->file.seekable) {
            ret = ocall_pread(handle->file.fd, buffer, count, offset);
        } else {
            ret = ocall_read(handle->file.fd, buffer, count);
        }

        if (ret < 0)
            return unix_to_pal_error(ret);

        return ret;
    }

    /* case of trusted file: already mmaped in umem, copy from there and verify hash */
    uint64_t total = handle->file.total;
    if (offset >= total)
        return 0;

    off_t end = MIN(offset + count, total);
    off_t aligned_offset = ALIGN_DOWN(offset, TRUSTED_CHUNK_SIZE);
    off_t aligned_end    = ALIGN_UP(end, TRUSTED_CHUNK_SIZE);

    ret = copy_and_verify_trusted_file(handle->file.realpath, buffer, handle->file.umem,
                                       aligned_offset, aligned_end, offset, end, chunk_hashes,
                                       total);
    if (ret < 0)
        return ret;

    return end - offset;
}

static int64_t pf_file_write(struct protected_file* pf, PAL_HANDLE handle, uint64_t offset,
                             uint64_t count, const void* buffer) {
    int fd = handle->file.fd;

    if (!pf->context) {
        log_error("pf_file_write(PF fd %d): PF not initialized\n", fd);
        return -PAL_ERROR_BADHANDLE;
    }

    pf_status_t pf_ret = pf_write(pf->context, offset, count, buffer);

    if (PF_FAILURE(pf_ret)) {
        log_error("pf_file_write(PF fd %d): pf_write failed: %s\n", fd, pf_strerror(pf_ret));
        return -PAL_ERROR_DENIED;
    }

    return count;
}

static int64_t file_write(PAL_HANDLE handle, uint64_t offset, uint64_t count, const void* buffer) {
    struct protected_file* pf = find_protected_file_handle(handle);

    if (pf)
        return pf_file_write(pf, handle, offset, count, buffer);

    int64_t ret;
    sgx_chunk_hash_t* chunk_hashes = (sgx_chunk_hash_t*)handle->file.chunk_hashes;

    if (!chunk_hashes) {
        if (handle->file.seekable) {
            ret = ocall_pwrite(handle->file.fd, buffer, count, offset);
        } else {
            ret = ocall_write(handle->file.fd, buffer, count);
        }

        if (ret < 0)
            return unix_to_pal_error(ret);

        return ret;
    }

    /* case of trusted file: disallow writing completely */
    log_error("Writing to a trusted file (%s) is disallowed!\n", handle->file.realpath);
    return -PAL_ERROR_DENIED;
}

static int pf_file_close(struct protected_file* pf) {
    int ret;

    if (pf->refcount == 0) {
        log_error("pf_file_close(PF fd %d): refcount == 0\n", pf->host_fd);
        return -PAL_ERROR_INVAL;
    }

    /* NOTE: ideally, we want to narrow access mode for cases when a read-write PF handle is closed
     *       and only a read-only/write-only PF handles are left for this PF context; but this is a
     *       rare corner case */

    pf->refcount--;
    if (pf->refcount > 0)
        return 0;

    /* this is last opened reference to protected file, clean up */
    ret = unload_protected_file(pf);
    if (ret < 0) {
        log_error("pf_file_close(PF fd %d): unload_protected_file failed\n", pf->host_fd);
        return ret;
    }

    ret = ocall_close(pf->host_fd);
    if (ret < 0) {
        log_error("pf_file_close(PF fd %d): ocall_close failed\n", pf->host_fd);
        return ret;
    }

    pf->host_fd = -1;
    return 0;
}

static int file_close(PAL_HANDLE handle) {
    /* initial realpath is part of handle object and will be freed with it */
    if (handle->file.realpath && handle->file.realpath != (void*)handle + HANDLE_SIZE(file))
        free((void*)handle->file.realpath);

    struct protected_file* pf = find_protected_file_handle(handle);
    if (pf)
        return pf_file_close(pf);

    if (handle->file.chunk_hashes && handle->file.total) {
        /* case of trusted file: the whole file was mmapped in untrusted memory */
        ocall_munmap_untrusted(handle->file.umem, handle->file.total);
    }

    ocall_close(handle->file.fd);
    return 0;
}

static int file_delete(PAL_HANDLE handle, int access) {
    if (access)
        return -PAL_ERROR_INVAL;

    int ret = ocall_delete(handle->file.realpath);
    return ret < 0 ? unix_to_pal_error(ret) : ret;
}

static int pf_file_map(struct protected_file* pf, PAL_HANDLE handle, void** addr, int prot,
                       uint64_t offset, uint64_t size) {
    int ret = 0;
    void* allocated_enclave_pages = NULL;
    int fd = handle->file.fd;

    if (size == 0)
        return -PAL_ERROR_INVAL;

    assert(WITHIN_MASK(prot, PAL_PROT_MASK));
    if ((prot & PAL_PROT_READ) && (prot & PAL_PROT_WRITE)) {
        log_error("pf_file_map(PF fd %d): trying to map with R+W access\n", fd);
        return -PAL_ERROR_NOTSUPPORT;
    }

    if (!pf->context) {
        log_error("pf_file_map(PF fd %d): PF not initialized\n", fd);
        return -PAL_ERROR_BADHANDLE;
    }

    uint64_t pf_size;
    pf_status_t pfs = pf_get_size(pf->context, &pf_size);
    __UNUSED(pfs);
    assert(PF_SUCCESS(pfs));

    log_debug("pf_file_map(PF fd %d): pf %p, addr %p, prot %d, offset %lu, size %lu\n", fd, pf,
              *addr, prot, offset, size);

    if (*addr == NULL) {
        /* LibOS didn't provide address at which to map, can happen on sendfile() */
        allocated_enclave_pages = get_enclave_pages(/*addr=*/NULL, size, /*is_pal_internal=*/false);
        if (!allocated_enclave_pages)
            return -PAL_ERROR_NOMEM;

        *addr = allocated_enclave_pages;
    }

    if (prot & PAL_PROT_WRITE) {
        struct pf_map* map = calloc(1, sizeof(*map));
        if (!map) {
            ret = -PAL_ERROR_NOMEM;
            goto out;
        }

        map->pf     = pf;
        map->size   = size;
        map->offset = offset;
        map->buffer = *addr;

        pf_lock();
        LISTP_ADD_TAIL(map, &g_pf_map_list, list);
        pf_unlock();
    }

    if (prot & PAL_PROT_READ) {
        /* we don't check this on writes since file size may be extended then */
        if (offset >= pf_size) {
            log_error("pf_file_map(PF fd %d): offset (%lu) >= file size (%lu)\n", fd, offset,
                      pf_size);
            ret = -PAL_ERROR_INVAL;
            goto out;
        }

        uint64_t copy_size = MIN(size, pf_size - offset);

        size_t bytes_read = 0;
        pf_status_t pf_ret = pf_read(pf->context, offset, copy_size, *addr, &bytes_read);
        if (bytes_read != copy_size) {
            /* mapped region must be read completely from file, otherwise it's an error */
            pf_ret = PF_STATUS_CORRUPTED;
        }
        if (PF_FAILURE(pf_ret)) {
            log_error("pf_file_map(PF fd %d): pf_read failed: %s\n", fd, pf_strerror(pf_ret));
            ret = -PAL_ERROR_DENIED;
            goto out;
        }
        memset(*addr + copy_size, 0, size - copy_size);
    }

    /* Writes will be flushed to the PF on close. */
    ret = 0;
out:
    if (ret < 0 && allocated_enclave_pages) {
        free_enclave_pages(allocated_enclave_pages, size);
        *addr = NULL;
    }
    return ret;
}

static int file_map(PAL_HANDLE handle, void** addr, int prot, uint64_t offset, uint64_t size) {
    assert(IS_ALLOC_ALIGNED(offset) && IS_ALLOC_ALIGNED(size));
    int ret;

    uint64_t dummy;
    if (__builtin_add_overflow(offset, size, &dummy)) {
        return -PAL_ERROR_INVAL;
    }

    if (size > SIZE_MAX) {
        /* for compatibility with 32-bit systems */
        return -PAL_ERROR_INVAL;
    }

    struct protected_file* pf = find_protected_file_handle(handle);
    if (pf)
        return pf_file_map(pf, handle, addr, prot, offset, size);

    sgx_chunk_hash_t* chunk_hashes = (sgx_chunk_hash_t*)handle->file.chunk_hashes;
    void* mem = *addr;

    /* If the file is listed in the manifest as an "allowed" file, we allow mapping the file outside
     * the enclave, if the library OS does not request a specific address. */
    if (!mem && !chunk_hashes && !(prot & PAL_PROT_WRITECOPY)) {
        ret = ocall_mmap_untrusted(&mem, size, PAL_PROT_TO_LINUX(prot), MAP_SHARED, handle->file.fd,
                                   offset);
        if (ret >= 0)
            *addr = mem;
        return ret < 0 ? unix_to_pal_error(ret) : ret;
    }

    if (!(prot & PAL_PROT_WRITECOPY) && (prot & PAL_PROT_WRITE)) {
        log_error(
            "file_map does not currently support writable pass-through mappings on SGX.  You "
            "may add the PAL_PROT_WRITECOPY (MAP_PRIVATE) flag to your file mapping to keep "
            "the writes inside the enclave but they won't be reflected outside of the "
            "enclave.\n");
        return -PAL_ERROR_DENIED;
    }

    mem = get_enclave_pages(mem, size, /*is_pal_internal=*/false);
    if (!mem)
        return -PAL_ERROR_NOMEM;

    if (chunk_hashes) {
        /* case of trusted file: already mmaped in umem, copy from there into enclave memory and
         * verify hashes along the way */
        off_t end = MIN(offset + size, handle->file.total);
        off_t aligned_offset = ALIGN_DOWN(offset, TRUSTED_CHUNK_SIZE);
        off_t aligned_end    = ALIGN_UP(end, TRUSTED_CHUNK_SIZE);
        off_t total_size     = aligned_end - aligned_offset;

        if ((uint64_t)total_size > SIZE_MAX) {
            /* for compatibility with 32-bit systems */
            ret = -PAL_ERROR_INVAL;
            goto out;
        }

        ret = copy_and_verify_trusted_file(handle->file.realpath, mem, handle->file.umem,
                                           aligned_offset, aligned_end, offset, end, chunk_hashes,
                                           handle->file.total);
        if (ret < 0) {
            log_error("file_map - copy & verify on trusted file returned %d\n", ret);
            goto out;
        }
    } else {
        /* case of allowed file: simply read from underlying file descriptor into enclave memory */
        size_t bytes_read = 0;
        while (bytes_read < size) {
            size_t read_size = MIN(size - bytes_read, MAX_READ_SIZE);
            ssize_t bytes = ocall_pread(handle->file.fd, mem + bytes_read, read_size,
                                        offset + bytes_read);
            if (bytes > 0) {
                bytes_read += bytes;
            } else if (bytes == 0) {
                break; /* EOF */
            } else if (bytes == -EINTR || bytes == -EAGAIN) {
                continue;
            } else {
                log_error("file_map - ocall_pread on allowed file returned %ld\n", bytes);
                ret = unix_to_pal_error(bytes);
                goto out;
            }
        }

        if (size - bytes_read > 0) {
            /* file ended before all requested memory was filled -- remaining memory has to be
             * zeroed */
            memset(mem + bytes_read, 0, size - bytes_read);
        }
    }

    *addr = mem;
    ret = 0;

out:
    if (ret < 0) {
        free_enclave_pages(mem, size);
    }
    return ret;
}

static int64_t pf_file_setlength(struct protected_file* pf, PAL_HANDLE handle, uint64_t length) {
    int fd = handle->file.fd;

    pf_status_t pfs = pf_set_size(pf->context, length);
    if (PF_FAILURE(pfs)) {
        log_error("pf_file_setlength(PF fd %d, %lu): pf_set_size returned %s\n", fd, length,
                  pf_strerror(pfs));
        return -PAL_ERROR_DENIED;
    }
    return length;
}

static int64_t file_setlength(PAL_HANDLE handle, uint64_t length) {
    struct protected_file* pf = find_protected_file_handle(handle);
    if (pf)
        return pf_file_setlength(pf, handle, length);

    int ret = ocall_ftruncate(handle->file.fd, length);
    if (ret < 0)
        return unix_to_pal_error(ret);

    handle->file.total = length;
    return (int64_t)length;
}

static int file_flush(PAL_HANDLE handle) {
    int fd = handle->file.fd;
    struct protected_file* pf = find_protected_file_handle(handle);
    if (pf) {
        int ret = flush_pf_maps(pf, /*buffer=*/NULL, /*remove=*/false);
        if (ret < 0) {
            log_error("file_flush(PF fd %d): flush_pf_maps returned %s\n", fd, pal_strerror(ret));
            return ret;
        }
        pf_status_t pfs = pf_flush(pf->context);
        if (PF_FAILURE(pfs)) {
            log_error("file_flush(PF fd %d): pf_flush returned %s\n", fd, pf_strerror(pfs));
            return -PAL_ERROR_DENIED;
        }
    } else {
        ocall_fsync(fd);
    }
    return 0;
}

static inline int file_stat_type(struct stat* stat) {
    if (S_ISREG(stat->st_mode))
        return pal_type_file;
    if (S_ISDIR(stat->st_mode))
        return pal_type_dir;
    if (S_ISCHR(stat->st_mode))
        return pal_type_dev;
    if (S_ISFIFO(stat->st_mode))
        return pal_type_pipe;
    if (S_ISSOCK(stat->st_mode))
        return pal_type_dev;

    return 0;
}

static inline void file_attrcopy(PAL_STREAM_ATTR* attr, struct stat* stat) {
    attr->handle_type  = file_stat_type(stat);
    attr->disconnected = PAL_FALSE;
    attr->nonblocking  = PAL_FALSE;
    attr->readable     = stataccess(stat, ACCESS_R);
    attr->writable     = stataccess(stat, ACCESS_W);
    attr->runnable     = stataccess(stat, ACCESS_X);
    attr->share_flags  = stat->st_mode;
    attr->pending_size = stat->st_size;
}

static int pf_file_attrquery(struct protected_file* pf, const char* path, PAL_STREAM_ATTR* attr) {
    int ret;

    /* protected file may not be opened (and thus have no context from which to grab the actual data
     * size), so temporarily open it */
    ret = pf_file_open(pf, path, /*handle=*/NULL, PAL_ACCESS_RDONLY, /*share=*/0, /*create=*/0,
                       /*options=*/0);
    if (ret < 0) {
        log_error("pf_file_attrquery(%s): pf_file_open failed\n", path);
        return ret;
    }

    assert(pf->context);

    uint64_t size;
    pf_status_t pfs = pf_get_size(pf->context, &size);
    __UNUSED(pfs);
    assert(PF_SUCCESS(pfs));
    attr->pending_size = size;

    ret = pf_file_close(pf);
    if (ret < 0) {
        log_error("pf_file_attrquery(%s): pf_file_close failed\n", path);
        return ret;
    }

    return 0;
}

static int file_attrquery(const char* type, const char* uri, PAL_STREAM_ATTR* attr) {
    if (strcmp(type, URI_TYPE_FILE) && strcmp(type, URI_TYPE_DIR))
        return -PAL_ERROR_INVAL;

    int ret;
    int fd = -1;
    char* path = NULL;
    size_t len = URI_MAX;

    path = malloc(len);
    ret = get_norm_path(uri, path, &len);
    if (ret < 0) {
        log_error("Could not normalize path (%s): %s\n", uri, pal_strerror(ret));
        goto out;
    }

    /* open with O_NONBLOCK to avoid blocking the current thread if it is actually a FIFO pipe */
    fd = ocall_open(uri, O_NONBLOCK, 0);
    if (fd < 0) {
        ret = unix_to_pal_error(fd);
        goto out;
    }

    struct stat stat_buf;
    ret = ocall_fstat(fd, &stat_buf);
    if (ret < 0) {
        ret = unix_to_pal_error(ret);
        goto out;
    }

    file_attrcopy(attr, &stat_buf);

    /* for protected files, replace attr's size (which is a real FS size) with PF data size */
    struct protected_file* pf = get_protected_file(path);
    if (pf && attr->handle_type != pal_type_dir) {
        ret = pf_file_attrquery(pf, path, attr);
        if (ret < 0)
            goto out;
    }

    ret = 0;

out:
    if (fd >= 0)
        ocall_close(fd);
    free(path);
    return ret;
}

static int file_attrquerybyhdl(PAL_HANDLE handle, PAL_STREAM_ATTR* attr) {
    int fd = handle->file.fd;
    struct stat stat_buf;

    int ret = ocall_fstat(fd, &stat_buf);
    if (ret < 0)
        return unix_to_pal_error(ret);

    file_attrcopy(attr, &stat_buf);

    if (attr->handle_type != pal_type_dir) {
        /* For protected files return the data size, not real FS size */
        struct protected_file* pf = find_protected_file_handle(handle);
        if (pf) {
            /* protected files should be regular files (seekable) */
            if (!handle->file.seekable)
                return -PAL_ERROR_DENIED;

            uint64_t size;
            pf_status_t pfs = pf_get_size(pf->context, &size);
            __UNUSED(pfs);
            assert(PF_SUCCESS(pfs));
            attr->pending_size = size;
        }
    }
    return 0;
}

static int file_attrsetbyhdl(PAL_HANDLE handle, PAL_STREAM_ATTR* attr) {
    int fd  = handle->file.fd;
    int ret = ocall_fchmod(fd, attr->share_flags | PERM_rw_______);
    if (ret < 0)
        return unix_to_pal_error(ret);

    return 0;
}

static int file_rename(PAL_HANDLE handle, const char* type, const char* uri) {
    if (strcmp(type, URI_TYPE_FILE))
        return -PAL_ERROR_INVAL;

    char* tmp = strdup(uri);
    if (!tmp)
        return -PAL_ERROR_NOMEM;

    int ret = ocall_rename(handle->file.realpath, uri);
    if (ret < 0) {
        free(tmp);
        return unix_to_pal_error(ret);
    }

    /* initial realpath is part of handle object and will be freed with it */
    if (handle->file.realpath && handle->file.realpath != (void*)handle + HANDLE_SIZE(file)) {
        free((void*)handle->file.realpath);
    }

    handle->file.realpath = tmp;
    return 0;
}

static int file_getname(PAL_HANDLE handle, char* buffer, size_t count) {
    if (!handle->file.realpath)
        return 0;

    int len   = strlen(handle->file.realpath);
    char* tmp = strcpy_static(buffer, URI_PREFIX_FILE, count);

    if (!tmp || buffer + count < tmp + len + 1)
        return -PAL_ERROR_TOOLONG;

    memcpy(tmp, handle->file.realpath, len + 1);
    return tmp + len - buffer;
}

static const char* file_getrealpath(PAL_HANDLE handle) {
    return handle->file.realpath;
}

struct handle_ops g_file_ops = {
    .getname        = &file_getname,
    .getrealpath    = &file_getrealpath,
    .open           = &file_open,
    .read           = &file_read,
    .write          = &file_write,
    .close          = &file_close,
    .delete         = &file_delete,
    .map            = &file_map,
    .setlength      = &file_setlength,
    .flush          = &file_flush,
    .attrquery      = &file_attrquery,
    .attrquerybyhdl = &file_attrquerybyhdl,
    .attrsetbyhdl   = &file_attrsetbyhdl,
    .rename         = &file_rename,
};

/* 'open' operation for directory stream. Directory stream does not have a
   specific type prefix, its URI looks the same file streams, plus it
   ended with slashes. dir_open will be called by file_open. */
static int dir_open(PAL_HANDLE* handle, const char* type, const char* uri, int access, int share,
                    int create, int options) {
    if (strcmp(type, URI_TYPE_DIR))
        return -PAL_ERROR_INVAL;
    if (!WITHIN_MASK(access, PAL_ACCESS_MASK))
        return -PAL_ERROR_INVAL;

    int ret;

    if (create & PAL_CREATE_TRY || create & PAL_CREATE_ALWAYS) {
        ret = ocall_mkdir(uri, share);

        if (ret < 0) {
            if (ret == -EEXIST && create & PAL_CREATE_ALWAYS)
                return -PAL_ERROR_STREAMEXIST;
            if (ret != -EEXIST)
                return unix_to_pal_error(ret);
            assert(ret == -EEXIST && create & PAL_CREATE_TRY);
        }
    }

    ret = ocall_open(uri, O_DIRECTORY | PAL_OPTION_TO_LINUX_OPEN(options), 0);
    if (ret < 0)
        return unix_to_pal_error(ret);

    int len        = strlen(uri);
    PAL_HANDLE hdl = malloc(HANDLE_SIZE(dir) + len + 1);
    SET_HANDLE_TYPE(hdl, dir);
    HANDLE_HDR(hdl)->flags |= RFD(0);
    hdl->dir.fd = ret;
    char* path  = (void*)hdl + HANDLE_SIZE(dir);
    memcpy(path, uri, len + 1);
    hdl->dir.realpath    = (PAL_STR)path;
    hdl->dir.buf         = (PAL_PTR)NULL;
    hdl->dir.ptr         = (PAL_PTR)NULL;
    hdl->dir.end         = (PAL_PTR)NULL;
    hdl->dir.endofstream = PAL_FALSE;
    *handle              = hdl;
    return 0;
}

#define DIRBUF_SIZE 1024
static inline bool is_dot_or_dotdot(const char* name) {
    return (name[0] == '.' && !name[1]) || (name[0] == '.' && name[1] == '.' && !name[2]);
}

/* 'read' operation for directory stream. Directory stream will not
   need a 'write' operation. */
static int64_t dir_read(PAL_HANDLE handle, uint64_t offset, size_t count, void* _buf) {
    size_t bytes_written = 0;
    char* buf            = (char*)_buf;

    if (offset) {
        return -PAL_ERROR_INVAL;
    }

    if (handle->dir.endofstream == PAL_TRUE) {
        return 0;
    }

    while (1) {
        while ((char*)handle->dir.ptr < (char*)handle->dir.end) {
            struct linux_dirent64* dirent = (struct linux_dirent64*)handle->dir.ptr;

            if (is_dot_or_dotdot(dirent->d_name)) {
                goto skip;
            }

            bool is_dir = dirent->d_type == DT_DIR;
            size_t len  = strlen(dirent->d_name);

            if (len + 1 + (is_dir ? 1 : 0) > count) {
                goto out;
            }

            memcpy(buf, dirent->d_name, len);
            if (is_dir) {
                buf[len++] = '/';
            }
            buf[len++] = '\0';

            buf += len;
            bytes_written += len;
            count -= len;
        skip:
            handle->dir.ptr = (char*)handle->dir.ptr + dirent->d_reclen;
        }

        if (!count) {
            /* No space left, returning */
            goto out;
        }

        if (!handle->dir.buf) {
            handle->dir.buf = (PAL_PTR)malloc(DIRBUF_SIZE);
            if (!handle->dir.buf) {
                return -PAL_ERROR_NOMEM;
            }
        }

        int size = ocall_getdents(handle->dir.fd, handle->dir.buf, DIRBUF_SIZE);
        if (size < 0) {
            /*
             * If something was written just return that and pretend no error
             * was seen - it will be caught next time.
             */
            if (bytes_written) {
                return bytes_written;
            }
            return unix_to_pal_error(size);
        }

        if (!size) {
            handle->dir.endofstream = PAL_TRUE;
            goto out;
        }

        handle->dir.ptr = handle->dir.buf;
        handle->dir.end = (char*)handle->dir.buf + size;
    }

out:
    return (int64_t)bytes_written;
}

/* 'close' operation of directory streams */
static int dir_close(PAL_HANDLE handle) {
    int fd = handle->dir.fd;

    ocall_close(fd);

    if (handle->dir.buf) {
        free((void*)handle->dir.buf);
        handle->dir.buf = handle->dir.ptr = handle->dir.end = (PAL_PTR)NULL;
    }

    /* initial realpath is part of handle object and will be freed with it */
    if (handle->dir.realpath && handle->dir.realpath != (void*)handle + HANDLE_SIZE(dir))
        free((void*)handle->dir.realpath);

    return 0;
}

/* 'delete' operation of directoy streams */
static int dir_delete(PAL_HANDLE handle, int access) {
    if (access)
        return -PAL_ERROR_INVAL;

    int ret = dir_close(handle);
    if (ret < 0)
        return ret;

    ret = ocall_delete(handle->dir.realpath);
    return ret < 0 ? unix_to_pal_error(ret) : ret;
}

static int dir_rename(PAL_HANDLE handle, const char* type, const char* uri) {
    if (strcmp(type, URI_TYPE_DIR))
        return -PAL_ERROR_INVAL;

    char* tmp = strdup(uri);
    if (!tmp)
        return -PAL_ERROR_NOMEM;

    int ret = ocall_rename(handle->dir.realpath, uri);
    if (ret < 0) {
        free(tmp);
        return unix_to_pal_error(ret);
    }

    /* initial realpath is part of handle object and will be freed with it */
    if (handle->dir.realpath && handle->dir.realpath != (void*)handle + HANDLE_SIZE(dir)) {
        free((void*)handle->dir.realpath);
    }

    handle->dir.realpath = tmp;
    return 0;
}

static int dir_getname(PAL_HANDLE handle, char* buffer, size_t count) {
    if (!handle->dir.realpath)
        return 0;

    size_t len = strlen(handle->dir.realpath);
    char* tmp  = strcpy_static(buffer, URI_PREFIX_DIR, count);

    if (!tmp || buffer + count < tmp + len + 1)
        return -PAL_ERROR_TOOLONG;

    memcpy(tmp, handle->dir.realpath, len + 1);
    return tmp + len - buffer;

    if (len + 6 >= count)
        return -PAL_ERROR_TOOLONG;
}

static const char* dir_getrealpath(PAL_HANDLE handle) {
    return handle->dir.realpath;
}

struct handle_ops g_dir_ops = {
    .getname        = &dir_getname,
    .getrealpath    = &dir_getrealpath,
    .open           = &dir_open,
    .read           = &dir_read,
    .close          = &dir_close,
    .delete         = &dir_delete,
    .attrquery      = &file_attrquery,
    .attrquerybyhdl = &file_attrquerybyhdl,
    .attrsetbyhdl   = &file_attrsetbyhdl,
    .rename         = &dir_rename,
};
