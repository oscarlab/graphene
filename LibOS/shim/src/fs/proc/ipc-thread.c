/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */
/* Copyright (C) 2020 Intel Corporation */

/*!
 * This file contains the implementation of `/proc/[remote-pid]` dir. Currently only adding remote
 * PIDs to the list of `/proc/[pids]` and checking PID existence ("match path") are implemented.
 */

#include <asm/unistd.h>
#include <errno.h>

#include "pal.h"
#include "pal_error.h"
#include "perm.h"
#include "shim_fs.h"
#include "shim_handle.h"
#include "shim_internal.h"
#include "shim_ipc.h"
#include "shim_lock.h"
#include "shim_table.h"
#include "shim_thread.h"
#include "shim_utils.h"
#include "stat.h"

static struct pid_status_cache {
    size_t status_num;
    struct pid_status* status;
} g_pid_status_cache;

static struct shim_lock g_pid_status_lock;

/* returns PID of the remote process found in relpath and pointer to the rest of relpath string
 * (e.g. "42/cwd" returns 42 in `*pid_ptr` and pointer to "cwd" in `rest` if process 42 exists) */
static int get_pid_from_relpath(const char* relpath, IDTYPE* pid_ptr, char** rest) {
    if (*relpath == '\0' || *relpath == '/')
        return -ENOENT;

    char* pid_end = NULL;
    IDTYPE pid = (IDTYPE)strtol(relpath, &pid_end, /*base=*/10);

    if (!pid_end || (*pid_end != '\0' && *pid_end != '/'))
        return -ENOENT;

    if (!create_lock_runtime(&g_pid_status_lock))
        return -ENOMEM;

    lock(&g_pid_status_lock);

    if (!g_pid_status_cache.status_num || !g_pid_status_cache.status) {
        unlock(&g_pid_status_lock);
        return -ENOENT;
    }

    bool found_pid = false;
    for (size_t i = 0; i < g_pid_status_cache.status_num; i++) {
        if (g_pid_status_cache.status[i].pid == pid) {
            found_pid = true;
            break;
        }
    }

    unlock(&g_pid_status_lock);

    if (!found_pid)
        return -ENOENT;

    *pid_ptr = pid;
    if (rest)
        *rest = *pid_end == '\0' ? NULL : pid_end + 1;
    return 0;
}

/* returns dentry corresponding to PID's "root"/"cwd"/"exe" in relpath (e.g. "[pid]/root")
 * FIXME: currently only a stub because there is no shared multi-process file system in Graphene */
static int get_ipc_genericlink_dentry(const char* relpath, struct shim_dentry** dent_ptr) {
    int ret;
    assert(dent_ptr);

    IDTYPE pid = 0;
    char* rest = NULL;
    ret = get_pid_from_relpath(relpath, &pid, &rest);
    if (ret < 0)
        return ret;

    if (!rest)
        return -ENOENT;

    struct shim_dentry* dent = NULL;

    enum pid_meta_code ipc_code;
    if (strstartswith(rest, "root")) {
        ipc_code = PID_META_ROOT;
    } else if (strstartswith(rest, "cwd")) {
        ipc_code = PID_META_CWD;
    } else if (strstartswith(rest, "exe")) {
        ipc_code = PID_META_EXEC;
    } else {
        return -ENOENT;
    }

#if 0
    /* this doesn't work because there is no shared multi-process file system in Graphene,
     * thus path_lookupat() on a returned `ipc_data` path doesn't make much sense */
    void* ipc_data = NULL;
    ret = ipc_pid_getmeta_send(pid, ipc_code, &ipc_data);
    if (ret < 0)
        return ret;

    ret = path_lookupat(NULL, (char*)ipc_data, 0, &dent, NULL);
    if (ret < 0) {
        free(ipc_data);
        return ret;
    }

    free(ipc_data);
    get_dentry(dent);
#else
    __UNUSED(pid);
    __UNUSED(ipc_code);
    __UNUSED(dent);
    return -ENOENT;
#endif

    *dent_ptr = dent;
    return 0;
}

static int proc_ipc_thread_genericlink_open(struct shim_handle* hdl, const char* relpath,
                                            int flags) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_ipc_genericlink_dentry(relpath, &dent);
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

static int proc_ipc_thread_genericlink_mode(const char* relpath, mode_t* mode) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_ipc_genericlink_dentry(relpath, &dent);
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

static int proc_ipc_thread_genericlink_stat(const char* relpath, struct stat* buf) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_ipc_genericlink_dentry(relpath, &dent);
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

static int proc_ipc_thread_genericlink_follow(const char* relpath, struct shim_qstr* link) {
    int ret;

    struct shim_dentry* dent = NULL;
    ret = get_ipc_genericlink_dentry(relpath, &dent);
    if (ret < 0)
        return ret;

    if (!dentry_get_path_into_qstr(dent, link)) {
        put_dentry(dent);
        return -ENOMEM;
    }

    put_dentry(dent);
    return 0;
}

static const struct pseudo_fs_ops proc_ipc_thread_genericlink_fs_ops = {
    .open        = &proc_ipc_thread_genericlink_open,
    .mode        = &proc_ipc_thread_genericlink_mode,
    .stat        = &proc_ipc_thread_genericlink_stat,
    .follow_link = &proc_ipc_thread_genericlink_follow,
};

/* return 0 if prefix of relpath (in format "[pid]") is a valid PID, or negative error code
 * otherwise */
static int proc_match_ipc_thread(const char* relpath) {
    IDTYPE dummy_pid;
    return get_pid_from_relpath(relpath, &dummy_pid, /*rest=*/NULL);
}

/* return an array of dirents with all other-processes PIDs; also save it in g_pid_status_cache */
static int proc_list_ipc_threads(const char* relpath, struct shim_dirent** buf, size_t size) {
    __UNUSED(relpath);
    int ret;

    if (!create_lock_runtime(&g_pid_status_lock))
        return -ENOMEM;

    /* free previously cached list of PIDs; we always query all PIDs anew in this function */
    lock(&g_pid_status_lock);
    free(g_pid_status_cache.status);
    g_pid_status_cache.status     = NULL;
    g_pid_status_cache.status_num = 0;
    unlock(&g_pid_status_lock);

    struct pid_status* status = NULL;
    ret = get_all_pid_status(&status);
    if (ret < 0)
        return ret;

    size_t status_num = ret;
    size_t bytes = 0;
    struct shim_dirent* dirent = *buf;

    for (size_t i = 0; i < status_num; i++) {
        if (status[i].pid != status[i].tgid)
            continue;

        char pid_str[16];
        ssize_t pid_str_size = snprintf(pid_str, sizeof(pid_str), "%u", status[i].pid) + 1;

        size_t total_dirent_size = sizeof(struct shim_dirent) + pid_str_size;
        bytes += total_dirent_size;
        if (bytes > size) {
            free(status);
            return -ENOMEM;
        }

        dirent->next = (struct shim_dirent*)((char*)(dirent) + total_dirent_size);
        dirent->ino  = 1;
        dirent->type = LINUX_DT_DIR;
        memcpy(dirent->name, pid_str, pid_str_size);
        dirent = dirent->next;
    }

    lock(&g_pid_status_lock);
    g_pid_status_cache.status     = status;
    g_pid_status_cache.status_num = status_num;
    unlock(&g_pid_status_lock);

    *buf = dirent; /* upon return, buf must point past all added entries */
    return 0;
}

const struct pseudo_name_ops proc_ipc_thread_name_ops = {
    .match_path   = &proc_match_ipc_thread,
    .list_dirents = &proc_list_ipc_threads,
};

static int proc_ipc_thread_dir_open(struct shim_handle* hdl, const char* relpath, int flags) {
    __UNUSED(hdl);
    int ret;

    IDTYPE pid = 0;
    ret = get_pid_from_relpath(relpath, &pid, /*rest=*/NULL);
    if (ret < 0)
        return ret;

    if (flags & (O_WRONLY | O_RDWR))
        return -EISDIR;

    return 0;
}

static int proc_ipc_thread_dir_mode(const char* relpath, mode_t* mode) {
    int ret;

    IDTYPE pid = 0;
    ret = get_pid_from_relpath(relpath, &pid, /*rest=*/NULL);
    if (ret < 0)
        return ret;

    *mode = PERM_r_x______;
    return 0;
}

static int proc_ipc_thread_dir_stat(const char* relpath, struct stat* buf) {
    int ret;

    IDTYPE pid = 0;
    ret = get_pid_from_relpath(relpath, &pid, /*rest=*/NULL);
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

const struct pseudo_fs_ops proc_ipc_thread_fs_ops = {
    .open = &proc_ipc_thread_dir_open,
    .mode = &proc_ipc_thread_dir_mode,
    .stat = &proc_ipc_thread_dir_stat,
};

const struct pseudo_dir proc_ipc_thread_dir = {
    .size = 3,
    .ent  = {
        {.name = "cwd",  .fs_ops = &proc_ipc_thread_genericlink_fs_ops, .type = LINUX_DT_LNK},
        {.name = "exe",  .fs_ops = &proc_ipc_thread_genericlink_fs_ops, .type = LINUX_DT_LNK},
        {.name = "root", .fs_ops = &proc_ipc_thread_genericlink_fs_ops, .type = LINUX_DT_LNK},
    }
};
