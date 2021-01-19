/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University
 * Copyright (C) 2020 Intel Labs
 */

/*!
 * \file
 *
 * This file contains common code for pseudo-filesystems (e.g., /dev and /proc).
 */

#include "shim_fs.h"
#include "stat.h"

/*!
 * \brief Find entry corresponding to path, starting from \p root_ent.
 *
 * Generic function for pseudo-filesystems. Example usage for the `/proc` FS is
 * `pseudo_findent("/proc/3/cwd", proc_root_ent, &cwd_ent_for_third_thread)`.
 *
 * \param[in]  path       Path to the requested entry.
 * \param[in]  root_ent   Root entry to start search from (e.g., `proc_root_ent`).
 * \param[out] found_ent  Pointer to found entry.
 * \return                0 if entry was found, negative Linux error code otherwise.
 */
static int pseudo_findent(const char* path, const struct pseudo_ent* root_ent,
                          const struct pseudo_ent** found_ent) {
    assert(path);

    size_t token_len = 0;
    const char* token = path;
    const char* next_token = NULL;
    const struct pseudo_ent* ent = root_ent;

    while (*token == '/')
        token++;

    while (token && *token) {
        char* slash_ptr = strchr(token, '/');
        if (!slash_ptr) {
            next_token = NULL;
            token_len  = strlen(token);
        } else {
            next_token = slash_ptr + 1; /* set it past '/' */
            token_len  = slash_ptr - token;
        }

        const struct pseudo_dir* dir = ent->dir;

        for (ent = dir->ent; ent < dir->ent + dir->size; ent++) {
            if (ent->name && !memcmp(ent->name, token, token_len)) {
                /* directory entry has a hardcoded name that matches current token: found ent */
                break;
            }

            if (ent->name_ops && ent->name_ops->match_path) {
                int ret = ent->name_ops->match_path(path);
                if (ret == 0) {
                    /* directory entry has a calculated at runtime name (via match_path) that
                     * matches current path prefix (not just token!): found ent */
                    break;
                } else if (ret != -ENOENT) {
                    /* actual failure in match_path() */
                    return ret;
                }
            }
        }

        if (ent == dir->ent + dir->size) {
            /* traversed all entries in directory but couldn't find entry matching the token */
            return -ENOENT;
        }

        if (!ent->dir && next_token) {
            /* still tokens left (subdirs left), but current entry doesn't have subdirs/files */
            return -ENOENT;
        }

        token = next_token;
    }

    *found_ent = ent;
    return 0;
}

/*! Populate supplied buffer with dirents (see pseudo_readdir() for details). */
static int populate_dirent(const char* path, const struct pseudo_dir* dir, struct shim_dirent* buf,
                           size_t buf_size) {
    if (!dir->size)
        return 0;

    HASHTYPE dir_hash = hash_path(path, strlen(path));

    struct shim_dirent* dirent_in_buf = buf;
    size_t total_size = 0;

    for (const struct pseudo_ent* ent = dir->ent; ent < dir->ent + dir->size; ent++) {
        if (ent->name) {
            /* directory entry has a hardcoded name */
            size_t name_size   = strlen(ent->name) + 1;
            size_t dirent_size = sizeof(struct shim_dirent) + name_size;

            total_size += dirent_size;
            if (total_size > buf_size)
                return -ENOMEM;

            memcpy(dirent_in_buf->name, ent->name, name_size);
            dirent_in_buf->next = (void*)dirent_in_buf + dirent_size;
            dirent_in_buf->ino  = rehash_name(dir_hash, ent->name, name_size - 1);
            dirent_in_buf->type = ent->dir ? LINUX_DT_DIR : ent->type;

            dirent_in_buf = dirent_in_buf->next;
        } else if (ent->name_ops && ent->name_ops->list_dirents) {
            /* directory entry has a list of entries calculated at runtime (via list_dirents) */
            struct shim_dirent* old_dirent_in_buf = dirent_in_buf;
            int ret = ent->name_ops->list_dirents(path, &dirent_in_buf, buf_size - total_size);
            if (ret < 0)
                return ret;

            /* dirent_in_buf now contains the address past all added entries */
            total_size += (void*)dirent_in_buf - (void*)old_dirent_in_buf;
        }
    }

    /* above logic set the last dirent's `next` to point past the buffer, find this last dirent
     * and unset its `next` */
    dirent_in_buf = buf;
    while ((void*)dirent_in_buf->next < (void*)buf + total_size)
        dirent_in_buf = dirent_in_buf->next;

    dirent_in_buf->next = NULL;
    return 0;
}

/*! Generic callback to mount a pseudo-filesystem. */
int pseudo_mount(const char* uri, void** mount_data) {
    __UNUSED(uri);
    __UNUSED(mount_data);
    return 0;
}

/*! Generic callback to unmount a pseudo-filesystem. */
int pseudo_unmount(void* mount_data) {
    __UNUSED(mount_data);
    return 0;
}

/*! Generic callback to obtain mode of a directory in a pseudo-filesystem. */
int pseudo_dir_mode(const char* name, mode_t* mode) {
    __UNUSED(name);
    *mode = DIR_RX_MODE | S_IFDIR;
    return 0;
}

/*! Generic callback to obtain stat of a directory in a pseudo-filesystem. */
int pseudo_dir_stat(const char* name, struct stat* buf) {
    __UNUSED(name);
    memset(buf, 0, sizeof(*buf));

    buf->st_dev     = 1;    /* dummy ID of device containing file */
    buf->st_ino     = 1;    /* dummy inode number */
    buf->st_size    = 4096; /* dummy total size, in bytes */
    buf->st_blksize = 4096; /* dummy bulk size, in bytes */
    buf->st_mode    = DIR_RX_MODE | S_IFDIR;
    return 0;
}

/*! Generic callback to open a directory in a pseudo-filesystem. */
int pseudo_dir_open(struct shim_handle* hdl, const char* name, int flags) {
    __UNUSED(name);

    if (flags & (O_WRONLY | O_RDWR)) {
        /* cannot write in pseudo-directory */
        return -EISDIR;
    }

    if (*name == '\0') {
        hdl->type     = TYPE_DIR;
        hdl->flags    = flags & ~O_ACCMODE;
        hdl->acc_mode = 0;
    }

    /* pseudo-dirs are emulated completely in LibOS, so nothing to open */
    return 0;
}

/*! Generic callback to obtain a mode of an entry in a pseudo-filesystem. */
int pseudo_mode(struct shim_dentry* dent, mode_t* mode, const struct pseudo_ent* root_ent) {
    if (qstrempty(&dent->rel_path)) {
        /* root of pseudo-FS */
        return pseudo_dir_mode(/*name=*/NULL, mode);
    }

    const char* rel_path = qstrgetstr(&dent->rel_path);
    const struct pseudo_ent* ent = NULL;

    int ret = pseudo_findent(rel_path, root_ent, &ent);
    if (ret < 0)
        return ret;

    if (!ent->fs_ops || !ent->fs_ops->mode)
        return -EACCES;

    return ent->fs_ops->mode(rel_path, mode);
}

/*! Generic callback to check if an entry exists in a pseudo-filesystem (and populate \p dent). */
int pseudo_lookup(struct shim_dentry* dent, const struct pseudo_ent* root_ent) {
    if (qstrempty(&dent->rel_path)) {
        /* root of pseudo-FS */
        dent->ino    = 1;
        dent->state |= DENTRY_ISDIRECTORY;
        return 0;
    }

    const char* rel_path = qstrgetstr(&dent->rel_path);
    const struct pseudo_ent* ent = NULL;

    int ret = pseudo_findent(rel_path, root_ent, &ent);
    if (ret < 0)
        return ret;

    if (ent->dir)
        dent->state |= DENTRY_ISDIRECTORY;

    if (ent->fs_ops && ent->fs_ops->follow_link)
        dent->state |= DENTRY_ISLINK;

    return 0;
}

/*! Generic callback to open an entry in a pseudo-filesystem (and populate \p hdl). */
int pseudo_open(struct shim_handle* hdl, struct shim_dentry* dent, int flags,
                const struct pseudo_ent* root_ent) {
    const char* rel_path = qstrgetstr(&dent->rel_path);
    const struct pseudo_ent* ent = NULL;

    int ret = pseudo_findent(rel_path, root_ent, &ent);
    if (ret < 0)
        return ret;

    if (!ent->fs_ops || !ent->fs_ops->open)
        return -EACCES;

    hdl->type     = ent->dir ? TYPE_DIR : (ent->type == LINUX_DT_CHR ? TYPE_DEV : TYPE_FILE);
    hdl->flags    = flags & ~O_ACCMODE;
    hdl->acc_mode = ACC_MODE(flags & O_ACCMODE);

    return ent->fs_ops->open(hdl, rel_path, flags);
}

/*!
 * \brief Create and populate buffer with dirents.
 *
 * Generic function for pseudo-filesystems. Example usage for the `/proc` FS is
 * `pseudo_readdir("/proc/3", &dirents_of_third_thread, proc_root_ent)` -- this
 * returns a dirent list with "root", "cwd", "exe", "fd", etc.
 *
 * \param[in]  dent       Dentry with path to the requested directory.
 * \param[out] dirent     Pointer to newly created buffer with dirents.
 * \param[in]  root_ent   Root entry to start search from (e.g., `proc_root_ent`).
 * \return                0 if populated the buffer, negative Linux error code otherwise.
 */
int pseudo_readdir(struct shim_dentry* dent, struct shim_dirent** dirent,
                   const struct pseudo_ent* root_ent) {
    int ret;
    const char* path = qstrgetstr(&dent->rel_path);
    const struct pseudo_ent* ent = NULL;

    ret = pseudo_findent(path, root_ent, &ent);
    if (ret < 0)
        return ret;

    if (!ent->dir)
        return -ENOTDIR;

    struct shim_dirent* buf;
    size_t buf_size = MAX_PATH;

    while (true) {
        buf = malloc(buf_size);

        ret = populate_dirent(path, ent->dir, buf, buf_size);
        if (!ret) {
            /* successfully listed all entries */
            break;
        } else if (ret == -ENOMEM) {
            /* reallocate bigger buffer and try again */
            free(buf);
            buf_size *= 2;
            continue;
        } else {
            /* unrecoverable error */
            free(buf);
            return ret;
        }
    }

    *dirent = buf;
    return 0;
}

/*! Generic callback to obtain stat of an entry in a pseudo-filesystem. */
int pseudo_stat(struct shim_dentry* dent, struct stat* buf, const struct pseudo_ent* root_ent) {
    if (qstrempty(&dent->rel_path)) {
        /* root of pseudo-FS */
        return pseudo_dir_stat(/*name=*/NULL, buf);
    }

    const char* rel_path = qstrgetstr(&dent->rel_path);
    const struct pseudo_ent* ent = NULL;

    int ret = pseudo_findent(rel_path, root_ent, &ent);
    if (ret < 0)
        return ret;

    if (!ent->fs_ops || !ent->fs_ops->stat)
        return -EACCES;

    return ent->fs_ops->stat(rel_path, buf);
}

/*! Generic callback to obtain stat of an entry in a pseudo-filesystem via its open \p hdl. */
int pseudo_hstat(struct shim_handle* hdl, struct stat* buf, const struct pseudo_ent* root_ent) {
    struct shim_dentry* dent = hdl->dentry;
    assert(dent);
    return pseudo_stat(dent, buf, root_ent);
}

/*! Generic callback to obtain a target string of a link entry in a pseudo-filesystem. */
int pseudo_follow_link(struct shim_dentry* dent, struct shim_qstr* link,
                       const struct pseudo_ent* root_ent) {
    const char* rel_path = qstrgetstr(&dent->rel_path);
    const struct pseudo_ent* ent = NULL;

    int ret = pseudo_findent(rel_path, root_ent, &ent);
    if (ret < 0)
        return ret;

    if (!ent->fs_ops || !ent->fs_ops->follow_link)
        return -EINVAL;

    return ent->fs_ops->follow_link(rel_path, link);
}
