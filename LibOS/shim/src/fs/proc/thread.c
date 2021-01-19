/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */
/* Copyright (C) 2020 Intel Corporation */

/*!
 * This file contains the implementation of `/proc/self` and `/proc/[tid]` sub-directories.
 */

#include <asm/mman.h>
#include <asm/unistd.h>
#include <errno.h>

#include "pal.h"
#include "pal_error.h"
#include "perm.h"
#include "shim_fs.h"
#include "shim_handle.h"
#include "shim_internal.h"
#include "shim_lock.h"
#include "shim_process.h"
#include "shim_table.h"
#include "shim_thread.h"
#include "shim_utils.h"
#include "stat.h"
#include "shim_vma.h"

/* returns TID of the thread found in relpath and pointer to the rest of relpath string
 * (e.g. "42/cwd" returns 42 in `*tid_ptr` and pointer to "cwd" in `rest`,
 *       "self" returns current-thread's TID in `*tid_ptr` and NULL in `rest`) */
static int get_tid_from_relpath(const char* relpath, IDTYPE* tid_ptr, char** rest) {
    if (*relpath == '\0' || *relpath == '/')
        return -ENOENT;

    char* tid_end = NULL;
    IDTYPE tid    = 0;

    if (strstartswith(relpath, "self")) {
        tid_end = (char*)relpath + 4;
        tid = get_cur_tid();
    } else {
        tid = (IDTYPE)strtol(relpath, &tid_end, /*base=*/10);
    }

    if (!tid_end || (*tid_end != '\0' && *tid_end != '/'))
        return -ENOENT;

    struct shim_thread* thread = lookup_thread(tid);
    if (!thread)
        return -ENOENT;
    put_thread(thread);

    *tid_ptr = tid;
    if (rest)
        *rest = *tid_end == '\0' ? NULL : tid_end + 1;
    return 0;
}

/* returns handle corresponding to TID and FD in relpath (in format "[tid]/fd/[fd]"); handle's
 * refcount is incremented on success; `hdl_ptr` may be NULL if caller only wants to know whether
 * the TID + FD pair in relpath actually exists */
static int get_fd_handle_from_relpath(const char* relpath, struct shim_handle** hdl_ptr) {
    IDTYPE tid = 0;
    char* rest = NULL;
    int ret = get_tid_from_relpath(relpath, &tid, &rest);
    if (ret < 0)
        return ret;

    if (!rest || !strstartswith(rest, "fd/"))
        return -ENOENT;

    rest += strlen("fd/");
    if (*rest == '\0')
        return -ENOENT;

    char* fd_end = NULL;
    FDTYPE fd = (FDTYPE)strtol(rest, &fd_end, /*base=*/10);

    if (!fd_end || (*fd_end != '\0' && *fd_end != '/'))
        return -ENOENT;

    struct shim_handle_map* handle_map = get_thread_handle_map(NULL);
    if (!handle_map)
        return -ENOENT;

    lock(&handle_map->lock);

    if (fd >= handle_map->fd_top || !handle_map->map[fd] || !handle_map->map[fd]->handle) {
        unlock(&handle_map->lock);
        return -ENOENT;
    }

    if (hdl_ptr) {
        *hdl_ptr = handle_map->map[fd]->handle;
        get_handle(*hdl_ptr);
    }

    unlock(&handle_map->lock);
    return 0;
}

/* returns dentry corresponding to TID and FD in relpath (in format "[tid]/fd/[fd]"); dentry's
 * refcount is incremented on success */
static int get_fd_dent_from_relpath(const char* relpath, struct shim_dentry** dent_ptr) {
    int ret;
    assert(dent_ptr);

    struct shim_handle* handle = NULL;
    ret = get_fd_handle_from_relpath(relpath, &handle);
    if (ret < 0)
        return ret;

    struct shim_dentry* dent = NULL;
    lock(&handle->lock);
    dent = handle->dentry;
    if (!dent) {
        unlock(&handle->lock);
        put_handle(handle);
        return -ENOENT;
    }
    get_dentry(dent);
    unlock(&handle->lock);

    *dent_ptr = dent;
    put_handle(handle);
    return 0;
}

/* returns qstr link corresponding to TID and FD in relpath (in format "[tid]/fd/[fd]") */
static int get_fd_link_from_relpath(const char* relpath, struct shim_qstr* link) {
    int ret;
    assert(link);

    struct shim_dentry* dent = NULL;
    ret = get_fd_dent_from_relpath(relpath, &dent);
    if (ret < 0)
        return ret;

    if (!dentry_get_path_into_qstr(dent, link)) {
        put_dentry(dent);
        return -ENOMEM;
    }

    put_dentry(dent);
    return 0;
}

/* returns dentry corresponding to TID's "root"/"cwd"/"exe" in relpath (e.g. "[tid]/root");
 * dentry's refcount is incremented on success */
static int get_genericlink_dentry(const char* relpath, struct shim_dentry** dent_ptr) {
    int ret;
    assert(dent_ptr);

    IDTYPE tid = 0;
    char* rest = NULL;
    ret = get_tid_from_relpath(relpath, &tid, &rest);
    if (ret < 0)
        return ret;

    if (!rest)
        return -ENOENT;

    struct shim_dentry* dent = NULL;

    lock(&g_process.fs_lock);

    if (strstartswith(rest, "root")) {
        dent = g_process.root;
    } else if (strstartswith(rest, "cwd")) {
        dent = g_process.cwd;
    } else if (strstartswith(rest, "exe")) {
        dent = g_process.exec->dentry;
    }

    if (!dent) {
        unlock(&g_process.fs_lock);
        return -ENOENT;
    }

    get_dentry(dent);
    unlock(&g_process.fs_lock);

    *dent_ptr = dent;
    return 0;
}

static int proc_thread_genericlink_open(struct shim_handle* hdl, const char* relpath, int flags) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_genericlink_dentry(relpath, &dent);
    if (ret < 0)
        return ret;

    if (!dent->fs || !dent->fs->d_ops || !dent->fs->d_ops->open) {
        put_dentry(dent);
        return -EACCES;
    }

    ret = dent->fs->d_ops->open(hdl, dent, flags);
    put_dentry(dent);
    return 0;
}

static int proc_thread_genericlink_mode(const char* relpath, mode_t* mode) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_genericlink_dentry(relpath, &dent);
    if (ret < 0)
        return ret;

    if (!dent->fs || !dent->fs->d_ops || !dent->fs->d_ops->mode) {
        put_dentry(dent);
        return -EACCES;
    }

    ret = dent->fs->d_ops->mode(dent, mode);
    put_dentry(dent);
    return ret;
}

static int proc_thread_genericlink_stat(const char* relpath, struct stat* buf) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_genericlink_dentry(relpath, &dent);
    if (ret < 0)
        return ret;

    if (!dent->fs || !dent->fs->d_ops || !dent->fs->d_ops->stat) {
        put_dentry(dent);
        return -EACCES;
    }

    ret = dent->fs->d_ops->stat(dent, buf);
    put_dentry(dent);
    return ret;
}

static int proc_thread_genericlink_follow(const char* relpath, struct shim_qstr* link) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_genericlink_dentry(relpath, &dent);
    if (ret < 0)
        return ret;

    if (!dentry_get_path_into_qstr(dent, link)) {
        put_dentry(dent);
        return -ENOMEM;
    }

    put_dentry(dent);
    return 0;
}

static const struct pseudo_fs_ops proc_thread_genericlink_fs_ops = {
    .open        = &proc_thread_genericlink_open,
    .mode        = &proc_thread_genericlink_mode,
    .stat        = &proc_thread_genericlink_stat,
    .follow_link = &proc_thread_genericlink_follow,
};

/* return 0 if prefix of relpath (in format "[tid]/fd/[fd]") is a valid TID + FD combination,
 *        negative error code otherwise */
static int proc_match_thread_fd(const char* relpath) {
    return get_fd_handle_from_relpath(relpath, /*hdl_ptr=*/NULL);
}

/* return an array of dirents for the given relpath (in format "[tid]/fd/[fd]"), or negative error
 * code otherwise */
static int proc_list_thread_fds(const char* relpath, struct shim_dirent** buf, size_t size) {
    int ret;

    IDTYPE tid = 0;
    char* rest = NULL;
    ret = get_tid_from_relpath(relpath, &tid, &rest);
    if (ret < 0)
        return ret;

    if (!rest || !strstartswith(rest, "fd"))
        return -ENOENT;

    rest += strlen("fd");
    if (*rest != '\0' && *rest != '/')
        return -ENOENT;

    /* all threads share the same handles, so ignore TID and use current thread's handle map */
    struct shim_handle_map* handle_map = get_thread_handle_map(NULL);
    if (!handle_map)
        return -ENOENT;

    size_t bytes = 0;
    struct shim_dirent* dirent = *buf;

    lock(&handle_map->lock);

    for (size_t i = 0; i < handle_map->fd_size; i++) {
        if (!handle_map->map[i] || !handle_map->map[i]->handle)
            continue;

        char fd_str[16];
        ssize_t fd_str_size = snprintf(fd_str, sizeof(fd_str), "%lu", i) + 1;

        size_t total_dirent_size = sizeof(struct shim_dirent) + fd_str_size;
        bytes += total_dirent_size;
        if (bytes > size) {
            ret = -ENOMEM;
            goto out;
        }

        dirent->next = (struct shim_dirent*)((char*)(dirent) + total_dirent_size);
        dirent->ino  = 1;
        dirent->type = LINUX_DT_LNK;
        memcpy(dirent->name, fd_str, fd_str_size);
        dirent = dirent->next;
    }

    *buf = dirent; /* upon return, buf must point past all added entries */
    ret = 0;
out:
    unlock(&handle_map->lock);
    return ret;
}

static const struct pseudo_name_ops proc_thread_fds_fd_name_ops = {
    .match_path   = &proc_match_thread_fd,
    .list_dirents = &proc_list_thread_fds,
};

static int proc_thread_fds_fd_open(struct shim_handle* hdl, const char* relpath, int flags) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_fd_dent_from_relpath(relpath, &dent);
    if (ret < 0)
        return ret;

    if (!dent->fs || !dent->fs->d_ops || !dent->fs->d_ops->open) {
        put_dentry(dent);
        return -EACCES;
    }

    ret = dent->fs->d_ops->open(hdl, dent, flags);
    put_dentry(dent);
    return ret;
}

static int proc_thread_fds_fd_mode(const char* relpath, mode_t* mode) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_fd_dent_from_relpath(relpath, &dent);
    if (ret < 0)
        return ret;

    if (!dent->fs || !dent->fs->d_ops || !dent->fs->d_ops->mode) {
        put_dentry(dent);
        return -EACCES;
    }

    ret = dent->fs->d_ops->mode(dent, mode);
    put_dentry(dent);
    return ret;
}

static int proc_thread_fds_fd_stat(const char* relpath, struct stat* buf) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_fd_dent_from_relpath(relpath, &dent);
    if (ret < 0)
        return ret;

    if (!dent->fs || !dent->fs->d_ops || !dent->fs->d_ops->stat) {
        put_dentry(dent);
        return -EACCES;
    }

    ret = dent->fs->d_ops->stat(dent, buf);
    put_dentry(dent);
    return ret;
}

static int proc_thread_fds_fd_follow(const char* relpath, struct shim_qstr* link) {
    return get_fd_link_from_relpath(relpath, link);
}

/* operations on files with paths of format "/proc/[tid]/fd/[fd]" */
static const struct pseudo_fs_ops proc_thread_fds_fd_fs_ops = {
    .open        = &proc_thread_fds_fd_open,
    .mode        = &proc_thread_fds_fd_mode,
    .stat        = &proc_thread_fds_fd_stat,
    .follow_link = &proc_thread_fds_fd_follow,
};

/* sub-directory of format "/proc/[tid]/fd/", contains opened FDs */
static const struct pseudo_dir proc_thread_fds_dir = {
    .size = 1,
    .ent =  { { .name_ops = &proc_thread_fds_fd_name_ops,
                .fs_ops = &proc_thread_fds_fd_fs_ops,
                .type = LINUX_DT_LNK } }
};

static int proc_thread_maps_open(struct shim_handle* hdl, const char* relpath, int flags) {
    int ret;

    IDTYPE tid = 0;
    ret = get_tid_from_relpath(relpath, &tid, /*rest=*/NULL);
    if (ret < 0)
        return ret;

    if (flags & (O_WRONLY | O_RDWR))
        return -EACCES;

    char* buffer = NULL;
    size_t count = 0;
    struct shim_vma_info* vmas = NULL;
    ret = dump_all_vmas(&vmas, &count, /*include_unmapped=*/false);
    if (ret < 0) {
        goto err;
    }

    size_t buffer_size = 1024; /* initial size of the VMA-info buffer, expanded if needed */
    buffer = malloc(buffer_size);
    if (!buffer) {
        ret = -ENOMEM;
        goto err;
    }

    size_t offset = 0;
    for (struct shim_vma_info* vma = vmas; vma < vmas + count; vma++) {
        size_t old_offset = offset;
        uintptr_t start   = (uintptr_t)vma->addr;
        uintptr_t end     = (uintptr_t)vma->addr + vma->length;
        char pt[3]        = {
            (vma->prot & PROT_READ) ? 'r' : '-',
            (vma->prot & PROT_WRITE) ? 'w' : '-',
            (vma->prot & PROT_EXEC) ? 'x' : '-',
        };
        char pr = (vma->flags & MAP_PRIVATE) ? 'p' : 's';

#define ADDR_FMT(addr) ((addr) > 0xffffffff ? "%lx" : "%08lx")
#define EMIT(fmt...)                                                    \
    do {                                                                \
        offset += snprintf(buffer + offset, buffer_size - offset, fmt); \
    } while (0)

    retry_emit_vma:
        if (vma->file) {
            int dev_major = 0, dev_minor = 0;
            unsigned long ino = vma->file->dentry ? vma->file->dentry->ino : 0;
            const char* name  = "[unknown]";

            if (!qstrempty(&vma->file->path))
                name = qstrgetstr(&vma->file->path);

            EMIT(ADDR_FMT(start), start);
            EMIT("-");
            EMIT(ADDR_FMT(end), end);
            EMIT(" %c%c%c%c %08lx %02d:%02d %lu %s\n", pt[0], pt[1], pt[2], pr, vma->file_offset,
                 dev_major, dev_minor, ino, name);
        } else {
            EMIT(ADDR_FMT(start), start);
            EMIT("-");
            EMIT(ADDR_FMT(end), end);
            if (vma->comment[0])
                EMIT(" %c%c%c%c 00000000 00:00 0 %s\n", pt[0], pt[1], pt[2], pr, vma->comment);
            else
                EMIT(" %c%c%c%c 00000000 00:00 0\n", pt[0], pt[1], pt[2], pr);
        }

        if (offset >= buffer_size) {
            char* new_buffer = malloc(buffer_size * 2);
            if (!new_buffer) {
                ret = -ENOMEM;
                goto err;
            }

            offset = old_offset;
            memcpy(new_buffer, buffer, old_offset);
            free(buffer);
            buffer = new_buffer;
            buffer_size *= 2;
            goto retry_emit_vma;
        }
    }

    struct shim_str_data* data = calloc(1, sizeof(struct shim_str_data));
    if (!data) {
        ret = -ENOMEM;
        goto err;
    }

    data->str = buffer;
    data->len = offset;
    hdl->type          = TYPE_STR;
    hdl->flags         = flags & ~O_RDONLY;
    hdl->acc_mode      = MAY_READ;
    hdl->info.str.data = data;
    ret = 0;

err:
    if (ret < 0) {
        free(buffer);
    }
    if (vmas) {
        free_vma_info_array(vmas, count);
    }
    return ret;
}

static int proc_thread_maps_mode(const char* relpath, mode_t* mode) {
    int ret;

    IDTYPE tid = 0;
    ret = get_tid_from_relpath(relpath, &tid, /*rest=*/NULL);
    if (ret < 0)
        return ret;

    *mode = PERM_r________;
    return 0;
}

static int proc_thread_maps_stat(const char* relpath, struct stat* buf) {
    int ret;

    IDTYPE tid = 0;
    ret = get_tid_from_relpath(relpath, &tid, /*rest=*/NULL);
    if (ret < 0)
        return ret;

    memset(buf, 0, sizeof(*buf));
    buf->st_dev  = 1;
    buf->st_ino  = 1;
    buf->st_mode = PERM_r________ | S_IFREG;
    buf->st_uid  = 0;
    buf->st_gid  = 0;
    buf->st_size = 0;
    return 0;
}

/* operations on file "/proc/[tid]/maps" */
static const struct pseudo_fs_ops proc_thread_maps_fs_ops = {
    .open = &proc_thread_maps_open,
    .mode = &proc_thread_maps_mode,
    .stat = &proc_thread_maps_stat,
};

static int proc_thread_dir_open(struct shim_handle* hdl, const char* relpath, int flags) {
    __UNUSED(hdl);
    int ret;

    IDTYPE tid = 0;
    ret = get_tid_from_relpath(relpath, &tid, /*rest=*/NULL);
    if (ret < 0)
        return ret;

    if (flags & (O_WRONLY | O_RDWR))
        return -EISDIR;

    return 0;
}

static int proc_thread_dir_mode(const char* relpath, mode_t* mode) {
    int ret;

    IDTYPE tid = 0;
    ret = get_tid_from_relpath(relpath, &tid, /*rest=*/NULL);
    if (ret < 0)
        return ret;

    *mode = PERM_r_x______;
    return 0;
}

static int proc_thread_dir_stat(const char* relpath, struct stat* buf) {
    int ret;

    IDTYPE tid = 0;
    ret = get_tid_from_relpath(relpath, &tid, /*rest=*/NULL);
    if (ret < 0)
        return ret;

    memset(buf, 0, sizeof(struct stat));
    buf->st_dev = 1;
    buf->st_ino = 1;
    buf->st_mode = PERM_r_x______ | S_IFDIR;
    buf->st_uid = 0;
    buf->st_gid = 0;
    buf->st_size = 4096;
    return 0;
}

static const struct pseudo_fs_ops proc_thread_fds_fs_ops = {
    .open = &proc_thread_dir_open,
    .mode = &proc_thread_dir_mode,
    .stat = &proc_thread_dir_stat,
};

/* return 0 if prefix of relpath (in format "[tid]") is a valid TID, or negative error code
 * otherwise */
static int proc_match_thread(const char* relpath) {
    IDTYPE dummy_tid;
    return get_tid_from_relpath(relpath, &dummy_tid, /*rest=*/NULL);
}

struct walk_thread_arg {
    char* buf;
    char* buf_end;
};

static int walk_thread_list_cb(struct shim_thread* thread, void* arg) {
    struct walk_thread_arg* args = (struct walk_thread_arg*)arg;

    char tid_str[32];
    ssize_t tid_str_size = snprintf(tid_str, sizeof(tid_str), "%u", thread->tid) + 1;

    size_t total_dirent_size = sizeof(struct shim_dirent) + tid_str_size;
    if (args->buf + total_dirent_size > args->buf_end)
        return -ENOMEM;

    struct shim_dirent* dirent = (struct shim_dirent*)args->buf;

    dirent->next = (struct shim_dirent*)(args->buf + total_dirent_size);
    dirent->ino  = 1;
    dirent->type = LINUX_DT_DIR;
    memcpy(dirent->name, tid_str, tid_str_size);

    args->buf = (char*)dirent->next;
    return 1;
}

/* return an array of dirents with all process-local TIDs */
static int proc_list_threads(const char* relpath, struct shim_dirent** buf, size_t size) {
    __UNUSED(relpath);
    int ret;

    struct walk_thread_arg args = {
        .buf     = (char*)*buf,
        .buf_end = (char*)*buf + size,
    };

    ret = walk_thread_list(&walk_thread_list_cb, &args, /*one_shot=*/false);
    if (ret < 0)
        return ret;

    *buf = (struct shim_dirent*)args.buf; /* upon return, buf must point past all added entries */
    return 0;
}

const struct pseudo_name_ops proc_thread_name_ops = {
    .match_path   = &proc_match_thread,
    .list_dirents = &proc_list_threads,
};

const struct pseudo_fs_ops proc_thread_fs_ops = {
    .open = &proc_thread_dir_open,
    .mode = &proc_thread_dir_mode,
    .stat = &proc_thread_dir_stat,
};

const struct pseudo_dir proc_thread_dir = {
    .size = 5,
    .ent  = {
        {.name = "cwd",  .fs_ops = &proc_thread_genericlink_fs_ops, .type = LINUX_DT_LNK},
        {.name = "exe",  .fs_ops = &proc_thread_genericlink_fs_ops, .type = LINUX_DT_LNK},
        {.name = "root", .fs_ops = &proc_thread_genericlink_fs_ops, .type = LINUX_DT_LNK},
        {.name = "fd",   .fs_ops = &proc_thread_fds_fs_ops, .dir = &proc_thread_fds_dir},
        {.name = "maps", .fs_ops = &proc_thread_maps_fs_ops, .type = LINUX_DT_REG},
    }};
