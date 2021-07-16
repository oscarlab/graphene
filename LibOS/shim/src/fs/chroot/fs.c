/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * This file contains code for implementation of 'chroot' filesystem.
 */

#include <asm/fcntl.h>
#include <asm/mman.h>
#include <asm/unistd.h>
#include <errno.h>
#include <linux/fcntl.h>

#include "pal.h"
#include "pal_error.h"
#include "shim_flags_conv.h"
#include "shim_fs.h"
#include "shim_handle.h"
#include "shim_internal.h"
#include "shim_lock.h"
#include "shim_thread.h"
#include "shim_utils.h"
#include "shim_vma.h"
#include "stat.h"

struct mount_data {
    size_t data_size;
    enum shim_file_type base_type;
    unsigned long dev;
    size_t root_uri_len;
    char root_uri[];
};

struct file_sync_data {
    off_t size;
    off_t marker;
};

static void file_sync_lock(struct shim_file_handle* file, int state) {
    struct file_sync_data data;
    bool updated = sync_lock(file->sync, state, &data, sizeof(data));
    if (updated) {
        file->size = data.size;
        file->marker = data.marker;
    }
}

static void file_sync_unlock(struct shim_file_handle* file) {
    struct file_sync_data data = {
        .size = file->size,
        .marker = file->marker,
    };
    sync_unlock(file->sync, &data, sizeof(data));
}

#define DENTRY_MOUNT_DATA(d) ((struct mount_data*)(d)->mount->data)

static int chroot_mount(const char* uri, void** mount_data) {
    enum shim_file_type type;

    if (strstartswith(uri, URI_PREFIX_FILE)) {
        type = FILE_UNKNOWN;
        uri += 5;
    } else if (strstartswith(uri, URI_PREFIX_DEV)) {
        type = strstartswith(uri + static_strlen(URI_PREFIX_DEV), "tty")
               ? FILE_TTY
               : FILE_DEV;
        uri += 4;
    } else {
        return -EINVAL;
    }

    if (!(*uri))
        uri = ".";

    size_t uri_len = strlen(uri);
    size_t data_size = uri_len + 1 + sizeof(struct mount_data);

    struct mount_data* mdata = (struct mount_data*)malloc(data_size);

    mdata->data_size    = data_size;
    mdata->base_type    = type;
    mdata->dev          = hash_str(uri);
    mdata->root_uri_len = uri_len;
    memcpy(mdata->root_uri, uri, uri_len + 1);

    *mount_data = mdata;
    return 0;
}

static int chroot_unmount(void* mount_data) {
    free(mount_data);
    return 0;
}

static int alloc_concat_uri(int type, const char* root, size_t root_len, const char* path,
                            size_t path_len, char** out, size_t* out_len) {
    const char* prefix = NULL;

    switch (type) {
        case FILE_UNKNOWN:
        case FILE_REGULAR:
            prefix = URI_PREFIX_FILE;
            break;

        case FILE_DIR:
            prefix = URI_PREFIX_DIR;
            break;

        case FILE_DEV:
        case FILE_TTY:
            prefix = URI_PREFIX_DEV;
            break;

        default:
            return -EINVAL;
    }

    size_t prefix_len = strlen(prefix);
    size_t alloc_size = prefix_len + root_len + 1 + path_len + 1; // one for '/', one for '\0'
    char* buf = malloc(alloc_size);
    if (!buf) {
        return -ENOMEM;
    }

    *out = buf;
    *out_len = alloc_size - 1; // return length does not include trailing '\0'

    memcpy(buf, prefix, prefix_len);
    buf += prefix_len;
    memcpy(buf, root, root_len);
    buf += root_len;
    if (path_len) {
        *buf++ = '/';
        memcpy(buf, path, path_len);
        buf += path_len;
    }
    *buf = '\0';

    return 0;
}

/* simply just create data, sometimes it is individually called when the
   handle is not linked to a dentry */
static struct shim_file_data* __create_data(void) {
    struct shim_file_data* data = calloc(1, sizeof(struct shim_file_data));
    if (!data)
        return NULL;

    if (!create_lock(&data->lock)) {
        free(data);
        return NULL;
    }
    return data;
}

static void __destroy_data(struct shim_file_data* data) {
    qstrfree(&data->host_uri);
    destroy_lock(&data->lock);
    free(data);
}

static int make_uri(struct shim_dentry* dent) {
    struct mount_data* mdata = DENTRY_MOUNT_DATA(dent);
    assert(mdata);

    struct shim_file_data* data = FILE_DENTRY_DATA(dent);
    char* uri = NULL;
    size_t uri_len = 0;

    char* rel_path;
    size_t rel_path_size;
    int ret = dentry_rel_path(dent, &rel_path, &rel_path_size);
    if (ret < 0) {
        return ret;
    }

    ret = alloc_concat_uri(data->type, mdata->root_uri, mdata->root_uri_len,
                           rel_path, rel_path_size - 1, &uri, &uri_len);
    free(rel_path);
    if (ret < 0) {
        return ret;
    }

    qstrsetstr(&data->host_uri, uri, uri_len);
    free(uri);
    return 0;
}

/* create a data in the dentry and compose it's uri. dent->lock needs to
   be held */
static int create_data(struct shim_dentry* dent, const char* uri, size_t len) {
    assert(locked(&dent->lock));

    if (dent->data)
        return 0;

    struct shim_file_data* data = __create_data();
    if (!data)
        return -ENOMEM;

    dent->data = data;

    struct mount_data* mdata = DENTRY_MOUNT_DATA(dent);
    assert(mdata);
    data->type = (dent->state & DENTRY_ISDIRECTORY) ? FILE_DIR : mdata->base_type;
    data->queried = false;

    if (uri) {
        qstrsetstr(&data->host_uri, uri, len);
    } else {
        int ret = make_uri(dent);
        if (ret < 0)
            return ret;
    }

    __atomic_store_n(&data->version.counter, 0, __ATOMIC_SEQ_CST);
    return 0;
}

static int chroot_readdir(struct shim_dentry* dent, readdir_callback_t callback, void* arg);

static int __query_attr(struct shim_dentry* dent, struct shim_file_data* data,
                        PAL_HANDLE pal_handle) {
    PAL_STREAM_ATTR pal_attr;
    enum shim_file_type old_type = data->type;

    int ret;
    if (pal_handle) {
        ret = DkStreamAttributesQueryByHandle(pal_handle, &pal_attr);
    } else {
        ret = DkStreamAttributesQuery(qstrgetstr(&data->host_uri), &pal_attr);
    }
    if (ret < 0) {
        return pal_to_unix_errno(ret);
    }

    mode_t type;
    /* need to correct the data type */
    switch (pal_attr.handle_type) {
        case pal_type_file:
            data->type = FILE_REGULAR;
            type = S_IFREG;
            break;
        case pal_type_dir:
            data->type = FILE_DIR;
            type = S_IFDIR;
            break;
        case pal_type_dev:
            if (strstartswith(qstrgetstr(&data->host_uri) + static_strlen(URI_PREFIX_DEV), "tty")) {
                data->type = FILE_TTY;
            } else {
                data->type = FILE_DEV;
            }
            type = S_IFCHR;
            break;
        default:
            log_error("unknown PAL handle type: %d", pal_attr.handle_type);
            BUG();
    }

    mode_t perm = (pal_attr.readable ? S_IRUSR : 0) |
                  (pal_attr.writable ? S_IWUSR : 0) |
                  (pal_attr.runnable ? S_IXUSR : 0);
    if (dent) {
        dent->type = type;
        dent->perm = perm;
    }

    __atomic_store_n(&data->size.counter, pal_attr.pending_size, __ATOMIC_SEQ_CST);

    if (data->type == FILE_DIR) {
        /* Move up the uri update; need to convert manifest-level file:
         * directives to 'dir:' uris */
        if (old_type != FILE_DIR) {
            dent->state |= DENTRY_ISDIRECTORY;
            if ((ret = make_uri(dent)) < 0)
                return ret;
        }
    }

    /*
     * Pretend `nlink` is 2 for directories (to account for "." and ".."), 1 for other files.
     *
     * Applications are unlikely to depend on exact value of `nlink`, and for us, it's inconvenient
     * to keep track of the exact value (we would have to list the directory, and also take into
     * account synthetic files created by Graphene, such as named pipes and sockets).
     *
     * TODO: Make this a default for filesystems that don't provide `nlink`?
     */
    data->nlink = data->type == FILE_DIR ? 2 : 1;

    data->queried = true;

    return 0;
}

static inline int try_create_data(struct shim_dentry* dent, const char* uri, size_t len,
                                  struct shim_file_data** dataptr) {
    struct shim_file_data* data = FILE_DENTRY_DATA(dent);

    if (!data) {
        lock(&dent->lock);
        int ret = create_data(dent, uri, len);
        data = FILE_DENTRY_DATA(dent);
        unlock(&dent->lock);
        if (ret < 0) {
            return ret;
        }
    }

    *dataptr = data;
    return 0;
}

static int query_dentry(struct shim_dentry* dent, PAL_HANDLE pal_handle, mode_t* mode,
                        struct stat* stat) {
    int ret = 0;

    struct shim_file_data* data;
    if ((ret = try_create_data(dent, NULL, 0, &data)) < 0)
        return ret;

    lock(&data->lock);

    if (!data->queried && (ret = __query_attr(dent, data, pal_handle)) < 0) {
        unlock(&data->lock);
        return ret;
    }

    if (mode)
        *mode = dent->type | dent->perm;

    if (stat) {
        struct mount_data* mdata = DENTRY_MOUNT_DATA(dent);

        memset(stat, 0, sizeof(struct stat));

        stat->st_mode  = dent->type | dent->perm;
        stat->st_dev   = (dev_t)mdata->dev;
        stat->st_size  = (off_t)__atomic_load_n(&data->size.counter, __ATOMIC_SEQ_CST);
        stat->st_atime = (time_t)data->atime;
        stat->st_mtime = (time_t)data->mtime;
        stat->st_ctime = (time_t)data->ctime;
        stat->st_nlink = data->nlink;
    }

    unlock(&data->lock);
    return 0;
}

static int chroot_mode(struct shim_dentry* dent, mode_t* mode) {
    return query_dentry(dent, NULL, mode, NULL);
}

static int chroot_stat(struct shim_dentry* dent, struct stat* statbuf) {
    return query_dentry(dent, NULL, NULL, statbuf);
}

static int chroot_lookup(struct shim_dentry* dent) {
    return query_dentry(dent, NULL, NULL, NULL);
}

static int __chroot_open(struct shim_dentry* dent, const char* uri, int flags, mode_t mode,
                         struct shim_handle* hdl, struct shim_file_data* data) {
    int ret = 0;

    if (!uri) {
        uri = qstrgetstr(&data->host_uri);
    }

    int version = __atomic_load_n(&data->version.counter, __ATOMIC_SEQ_CST);
    int oldmode = LINUX_OPEN_FLAGS_TO_PAL_ACCESS(flags);
    int accmode = oldmode;
    int create  = LINUX_OPEN_FLAGS_TO_PAL_CREATE(flags);
    int options = LINUX_OPEN_FLAGS_TO_PAL_OPTIONS(flags);

    if ((data->type == FILE_REGULAR || data->type == FILE_UNKNOWN) && accmode == O_WRONLY)
        accmode = O_RDWR;

    PAL_HANDLE palhdl;

    if (hdl && hdl->pal_handle) {
        palhdl = hdl->pal_handle;
    } else {
        ret = DkStreamOpen(uri, accmode, mode, create, options, &palhdl);

        if (ret < 0) {
            if (ret == -PAL_ERROR_DENIED && accmode != oldmode)
                ret = DkStreamOpen(uri, oldmode, mode, create, options, &palhdl);

            if (ret < 0)
                return pal_to_unix_errno(ret);
        }
    }

    if (!data->queried) {
        lock(&data->lock);
        ret = __query_attr(dent, data, palhdl);
        unlock(&data->lock);
        if (ret < 0)
            return ret;
    }

    if (!hdl) {
        DkObjectClose(palhdl);
        return 0;
    }

    hdl->pal_handle        = palhdl;
    hdl->type              = TYPE_FILE;
    hdl->info.file.type    = data->type;
    hdl->info.file.version = version;
    hdl->info.file.size    = __atomic_load_n(&data->size.counter, __ATOMIC_SEQ_CST);
    hdl->info.file.data    = data;

    /* files obtained from checkpoint system will have sync handle initialized already */
    if (!hdl->info.file.sync) {
        ret = sync_create(&hdl->info.file.sync, /*id=*/0);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int chroot_open(struct shim_handle* hdl, struct shim_dentry* dent, int flags) {
    int ret = 0;
    struct shim_file_data* data;
    if ((ret = try_create_data(dent, NULL, 0, &data)) < 0)
        return ret;

    if ((ret = __chroot_open(dent, NULL, flags, dent->perm, hdl, data)) < 0)
        return ret;

    assert(hdl->type == TYPE_FILE);
    struct shim_file_handle* file = &hdl->info.file;
    off_t size = __atomic_load_n(&data->size.counter, __ATOMIC_SEQ_CST);

    /* initialize hdl, does not need a lock because no one is sharing */
    hdl->type     = TYPE_FILE;
    hdl->flags    = flags;
    hdl->acc_mode = ACC_MODE(flags & O_ACCMODE);
    file->marker = (flags & O_APPEND) ? size : 0;
    file->size   = size;
    qstrcopy(&hdl->uri, &data->host_uri);

    return 0;
}

static int chroot_creat(struct shim_handle* hdl, struct shim_dentry* dir, struct shim_dentry* dent,
                        int flags, mode_t mode) {
    __UNUSED(dir);

    int ret = 0;
    struct shim_file_data* data;
    if ((ret = try_create_data(dent, NULL, 0, &data)) < 0)
        return ret;

    if ((ret = __chroot_open(dent, NULL, flags | O_CREAT | O_EXCL, mode, hdl, data)) < 0)
        return ret;

    if (!hdl)
        return 0;

    assert(hdl->type == TYPE_FILE);
    struct shim_file_handle* file = &hdl->info.file;
    off_t size = __atomic_load_n(&data->size.counter, __ATOMIC_SEQ_CST);

    /* initialize hdl, does not need a lock because no one is sharing */
    hdl->type     = TYPE_FILE;
    hdl->flags    = flags;
    hdl->acc_mode = ACC_MODE(flags & O_ACCMODE);
    file->marker = (flags & O_APPEND) ? size : 0;
    file->size   = size;
    qstrcopy(&hdl->uri, &data->host_uri);

    return 0;
}

static int chroot_mkdir(struct shim_dentry* dir, struct shim_dentry* dent, mode_t mode) {
    __UNUSED(dir);

    int ret = 0;
    struct shim_file_data* data;
    if ((ret = try_create_data(dent, NULL, 0, &data)) < 0)
        return ret;

    if (data->type != FILE_DIR) {
        data->type = FILE_DIR;
        int ret = make_uri(dent);
        if (ret < 0)
            return ret;
    }

    ret = __chroot_open(dent, NULL, O_CREAT | O_EXCL, mode, NULL, data);

    return ret;
}

#define NEED_RECREATE(hdl) (!FILE_HANDLE_DATA(hdl))

static int chroot_recreate(struct shim_handle* hdl) {
    lock(&hdl->lock);

    assert(hdl->type == TYPE_FILE);
    struct shim_file_data* data = FILE_HANDLE_DATA(hdl);
    int ret = 0;

    /* quickly bail out if the data is created */
    if (data)
        goto out;

    const char* uri = qstrgetstr(&hdl->uri);
    size_t len = hdl->uri.len;

    if (hdl->dentry) {
        if ((ret = try_create_data(hdl->dentry, uri, len, &data)) < 0)
            goto out;
    } else {
        data = __create_data();
        if (!data) {
            ret = -ENOMEM;
            goto out;
        }
        qstrsetstr(&data->host_uri, uri, len);
    }

    /*
     * when recreating a file handle after migration, the file should
     * not be created again.
     */
    ret = __chroot_open(hdl->dentry, uri, hdl->flags & ~(O_CREAT | O_EXCL), 0, hdl, data);

out:
    unlock(&hdl->lock);
    return ret;
}

static inline bool check_version(struct shim_handle* hdl) {
    assert(hdl->type == TYPE_FILE);
    return __atomic_load_n(&FILE_HANDLE_DATA(hdl)->version.counter, __ATOMIC_SEQ_CST)
           == hdl->info.file.version;
}

static void chroot_update_size(struct shim_handle* hdl, struct shim_file_handle* file,
                               struct shim_file_data* data) {
    if (check_version(hdl)) {
        off_t size;
        do {
            if ((size = __atomic_load_n(&data->size.counter, __ATOMIC_SEQ_CST)) >= file->size) {
                file->size = size;
                break;
            }
        } while (!__atomic_compare_exchange_n(&data->size.counter, &size, file->size,
                                              /*weak=*/false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));
    }
}

static int chroot_hstat(struct shim_handle* hdl, struct stat* stat) {
    int ret;
    if (NEED_RECREATE(hdl) && (ret = chroot_recreate(hdl)) < 0)
        return ret;

    if (!check_version(hdl) || !hdl->dentry) {
        struct shim_file_handle* file = &hdl->info.file;
        struct shim_dentry* dent = hdl->dentry;
        struct mount_data* mdata = dent ? DENTRY_MOUNT_DATA(dent) : NULL;

        if (stat) {
            memset(stat, 0, sizeof(struct stat));
            stat->st_dev  = mdata ? (dev_t)mdata->dev : 0;
            stat->st_size = file->size;
            stat->st_mode |= (file->type == FILE_REGULAR) ? S_IFREG : S_IFCHR;
        }

        return 0;
    }

    return query_dentry(hdl->dentry, hdl->pal_handle, NULL, stat);
}

static int chroot_flush(struct shim_handle* hdl) {
    return pal_to_unix_errno(DkStreamFlush(hdl->pal_handle));
}

static int chroot_close(struct shim_handle* hdl) {
    __UNUSED(hdl);
    return 0;
}

static ssize_t chroot_read(struct shim_handle* hdl, void* buf, size_t count) {
    ssize_t ret = 0;

    if (count == 0)
        goto out;

    if (NEED_RECREATE(hdl) && (ret = chroot_recreate(hdl)) < 0) {
        goto out;
    }

    if (!(hdl->acc_mode & MAY_READ)) {
        ret = -EBADF;
        goto out;
    }

    struct shim_file_handle* file = &hdl->info.file;

    off_t dummy_off_t;
    if (file->type != FILE_TTY && file->type != FILE_DEV &&
            __builtin_add_overflow(file->marker, count, &dummy_off_t)) {
        ret = -EFBIG;
        goto out;
    }

    lock(&hdl->lock);
    file_sync_lock(file, SYNC_STATE_EXCLUSIVE);

    ret = DkStreamRead(hdl->pal_handle, file->marker, &count, buf, NULL, 0);
    if (ret < 0) {
        ret = pal_to_unix_errno(ret);
    } else {
        if (__builtin_add_overflow(count, 0, &ret)) {
            BUG();
        }
        if (file->type != FILE_TTY && file->type != FILE_DEV &&
                __builtin_add_overflow(file->marker, count, &file->marker)) {
            BUG();
        }
    }

    file_sync_unlock(file);
    unlock(&hdl->lock);
out:
    return ret;
}

static ssize_t chroot_write(struct shim_handle* hdl, const void* buf, size_t count) {
    ssize_t ret;

    if (count == 0)
        return 0;

    if (NEED_RECREATE(hdl) && (ret = chroot_recreate(hdl)) < 0) {
        goto out;
    }

    if (!(hdl->acc_mode & MAY_WRITE)) {
        ret = -EBADF;
        goto out;
    }

    assert(hdl->type == TYPE_FILE);
    struct shim_file_handle* file = &hdl->info.file;

    off_t dummy_off_t;
    if (file->type != FILE_TTY && file->type != FILE_DEV &&
            __builtin_add_overflow(file->marker, count, &dummy_off_t)) {
        ret = -EFBIG;
        goto out;
    }

    lock(&hdl->lock);
    file_sync_lock(file, SYNC_STATE_EXCLUSIVE);

    ret = DkStreamWrite(hdl->pal_handle, file->marker, &count, (void*)buf, NULL);
    if (ret < 0) {
        ret = pal_to_unix_errno(ret);
    } else {
        if (__builtin_add_overflow(count, 0, &ret)) {
            BUG();
        }
        if (file->type != FILE_TTY && file->type != FILE_DEV &&
                __builtin_add_overflow(file->marker, count, &file->marker)) {
            BUG();
        }
        if (file->marker > file->size) {
            file->size = file->marker;
            chroot_update_size(hdl, file, FILE_HANDLE_DATA(hdl));
        }
    }

    file_sync_unlock(file);
    unlock(&hdl->lock);
out:
    return ret;
}

static int chroot_mmap(struct shim_handle* hdl, void** addr, size_t size, int prot, int flags,
                       uint64_t offset) {
    int ret;
    if (NEED_RECREATE(hdl) && (ret = chroot_recreate(hdl)) < 0)
        return ret;

    int pal_prot = LINUX_PROT_TO_PAL(prot, flags);

#if MAP_FILE == 0
    if (flags & MAP_ANONYMOUS)
#else
    if (!(flags & MAP_FILE))
#endif
        return -EINVAL;

    return pal_to_unix_errno(DkStreamMap(hdl->pal_handle, addr, pal_prot, offset, size));
}

static off_t chroot_seek(struct shim_handle* hdl, off_t offset, int whence) {
    off_t ret = -EINVAL;

    if (NEED_RECREATE(hdl) && (ret = chroot_recreate(hdl)) < 0)
        return ret;

    assert(hdl->type == TYPE_FILE);
    struct shim_file_handle* file = &hdl->info.file;
    lock(&hdl->lock);
    file_sync_lock(file, SYNC_STATE_EXCLUSIVE);

    /* TODO: this function emulates lseek() completely inside the LibOS, but some device files
     *       may report size == 0 during fstat() and may provide device-specific lseek() logic;
     *       this emulation breaks for such device-specific cases */
    off_t marker = file->marker;
    off_t size = file->size;

    if (check_version(hdl)) {
        struct shim_file_data* data = FILE_HANDLE_DATA(hdl);
        if (data->type != FILE_REGULAR && data->type != FILE_DEV) {
            ret = -ESPIPE;
            goto out;
        }
    }

    switch (whence) {
        case SEEK_SET:
            if (offset < 0)
                goto out;
            marker = offset;
            break;

        case SEEK_CUR:
            marker += offset;
            break;

        case SEEK_END:
            marker = size + offset;
            break;
    }

    ret = file->marker = marker;

out:
    file_sync_unlock(file);
    unlock(&hdl->lock);
    return ret;
}

static int chroot_truncate(struct shim_handle* hdl, off_t len) {
    int ret = 0;

    if (NEED_RECREATE(hdl) && (ret = chroot_recreate(hdl)) < 0)
        return ret;

    if (!(hdl->acc_mode & MAY_WRITE))
        return -EINVAL;

    struct shim_file_handle* file = &hdl->info.file;
    lock(&hdl->lock);
    file_sync_lock(file, SYNC_STATE_EXCLUSIVE);

    file->size = len;

    if (check_version(hdl)) {
        struct shim_file_data* data = FILE_HANDLE_DATA(hdl);
        __atomic_store_n(&data->size.counter, len, __ATOMIC_SEQ_CST);
    }

    ret = DkStreamSetLength(hdl->pal_handle, len);
    if (ret < 0) {
        ret = pal_to_unix_errno(ret);
        goto out;
    }

    if (file->marker > len)
        file->marker = len;

out:
    file_sync_unlock(file);
    unlock(&hdl->lock);
    return ret;
}

static int chroot_dput(struct shim_dentry* dent) {
    struct shim_file_data* data = FILE_DENTRY_DATA(dent);

    if (data) {
        __destroy_data(data);
        dent->data = NULL;
    }

    return 0;
}

static int chroot_readdir(struct shim_dentry* dent, readdir_callback_t callback, void* arg) {
    struct shim_file_data* data = NULL;
    int ret = 0;
    PAL_HANDLE pal_hdl = NULL;
    size_t buf_size = READDIR_BUF_SIZE;
    char* buf = NULL;

    if ((ret = try_create_data(dent, NULL, 0, &data)) < 0)
        return ret;

    const char* uri = qstrgetstr(&data->host_uri);
    assert(strstartswith(uri, URI_PREFIX_DIR));

    ret = DkStreamOpen(uri, PAL_ACCESS_RDONLY, 0, 0, 0, &pal_hdl);
    if (ret < 0) {
        return pal_to_unix_errno(ret);
    }

    buf = malloc(buf_size);
    if (!buf) {
        ret = -ENOMEM;
        goto out;
    }

    while (1) {
        /* DkStreamRead for directory will return as many entries as fits into the buffer. */
        size_t bytes = buf_size;
        ret = DkStreamRead(pal_hdl, 0, &bytes, buf, NULL, 0);
        if (ret < 0) {
            ret = pal_to_unix_errno(ret);
            goto out;
        } else if (bytes == 0) {
            /* End of directory listing */
            break;
        }
        /* Last entry must be null-terminated */
        assert(buf[bytes - 1] == '\0');

        size_t start = 0;
        while (start < bytes - 1) {
            size_t end = start;
            while (buf[end] != '\0')
                end++;

            if (end == start) {
                log_error("chroot_readdir: empty name returned from PAL");
                die_or_inf_loop();
            }

            /* By the PAL convention, if a name ends with '/', it is a directory. However, we ignore
             * that distinction here and pass the name without '/' to the callback. */
            if (buf[end - 1] == '/')
                buf[end - 1] = '\0';

            if ((ret = callback(&buf[start], arg)) < 0)
                goto out;

            start = end + 1;
        }
    }

    ret = 0;
out:
    free(buf);
    DkObjectClose(pal_hdl);
    return ret;
}

static void chroot_hput(struct shim_handle* hdl) {
    if (hdl->info.file.sync) {
        sync_destroy(hdl->info.file.sync);
        hdl->info.file.sync = NULL;
    }
}

static int chroot_checkout(struct shim_handle* hdl) {
    if (hdl->type == TYPE_FILE) {
        struct shim_file_data* data = FILE_HANDLE_DATA(hdl);
        if (data)
            hdl->info.file.data = NULL;
    }

    if (hdl->pal_handle) {
        /*
         * if the file still exists in the host, no need to send
         * the handle over RPC; otherwise, send it.
         */
        PAL_STREAM_ATTR attr;
        if (DkStreamAttributesQuery(qstrgetstr(&hdl->uri), &attr) == 0)
            hdl->pal_handle = NULL;
    }

    return 0;
}

static ssize_t chroot_checkpoint(void** checkpoint, void* mount_data) {
    struct mount_data* mdata = mount_data;

    *checkpoint = mount_data;
    return mdata->root_uri_len + sizeof(struct mount_data) + 1;
}

static int chroot_migrate(void* checkpoint, void** mount_data) {
    struct mount_data* mdata = checkpoint;
    size_t alloc_size = mdata->root_uri_len + sizeof(struct mount_data) + 1;

    void* new_data = malloc(alloc_size);
    if (!new_data)
        return -ENOMEM;

    memcpy(new_data, mdata, alloc_size);
    *mount_data = new_data;

    return 0;
}

static int chroot_unlink(struct shim_dentry* dir, struct shim_dentry* dent) {
    __UNUSED(dir);

    int ret;
    struct shim_file_data* data;
    if ((ret = try_create_data(dent, NULL, 0, &data)) < 0)
        return ret;

    PAL_HANDLE pal_hdl = NULL;
    ret = DkStreamOpen(qstrgetstr(&data->host_uri), 0, 0, 0, 0, &pal_hdl);
    if (ret < 0) {
        return pal_to_unix_errno(ret);
    }

    ret = DkStreamDelete(pal_hdl, 0);
    DkObjectClose(pal_hdl);
    if (ret < 0) {
        return pal_to_unix_errno(ret);
    }

    data->queried = false;

    __atomic_add_fetch(&data->version.counter, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&data->size.counter, 0, __ATOMIC_SEQ_CST);

    return 0;
}

static off_t chroot_poll(struct shim_handle* hdl, int poll_type) {
    int ret;
    if (NEED_RECREATE(hdl) && (ret = chroot_recreate(hdl)) < 0)
        return ret;

    struct shim_file_data* data = FILE_HANDLE_DATA(hdl);
    off_t size = __atomic_load_n(&data->size.counter, __ATOMIC_SEQ_CST);

    if (poll_type == FS_POLL_SZ)
        return size;

    struct shim_file_handle* file = &hdl->info.file;
    lock(&hdl->lock);
    file_sync_lock(file, SYNC_STATE_EXCLUSIVE);

    if (check_version(hdl) && file->size < size)
        file->size = size;

    off_t marker = file->marker;

    if (file->type == FILE_REGULAR) {
        ret = poll_type & FS_POLL_WR;
        if ((poll_type & FS_POLL_RD) && file->size > marker)
            ret |= FS_POLL_RD;
        goto out;
    }

    ret = -EAGAIN;

out:
    file_sync_unlock(file);
    unlock(&hdl->lock);
    return ret;
}

static int chroot_rename(struct shim_dentry* old, struct shim_dentry* new) {
    int ret;

    struct shim_file_data* old_data;
    if ((ret = try_create_data(old, NULL, 0, &old_data)) < 0) {
        return ret;
    }

    struct shim_file_data* new_data;
    if ((ret = try_create_data(new, NULL, 0, &new_data)) < 0) {
        return ret;
    }

    PAL_HANDLE pal_hdl = NULL;
    ret = DkStreamOpen(qstrgetstr(&old_data->host_uri), 0, 0, 0, PAL_OPTION_RENAME, &pal_hdl);
    if (ret < 0) {
        return pal_to_unix_errno(ret);
    }

    ret = DkStreamChangeName(pal_hdl, qstrgetstr(&new_data->host_uri));
    if (ret < 0) {
        DkObjectClose(pal_hdl);
        return pal_to_unix_errno(ret);
    }

    new->perm = old->perm;
    new->type = old->type;
    old_data->queried = false;

    DkObjectClose(pal_hdl);

    __atomic_add_fetch(&old_data->version.counter, 1, __ATOMIC_SEQ_CST);
    __atomic_store_n(&old_data->size.counter, 0, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&new_data->version.counter, 1, __ATOMIC_SEQ_CST);

    return 0;
}

static int chroot_chmod(struct shim_dentry* dent, mode_t mode) {
    int ret;
    struct shim_file_data* data;
    if ((ret = try_create_data(dent, NULL, 0, &data)) < 0)
        return ret;

    PAL_HANDLE pal_hdl = NULL;
    ret = DkStreamOpen(qstrgetstr(&data->host_uri), 0, 0, 0, 0, &pal_hdl);
    if (ret < 0) {
        return pal_to_unix_errno(ret);
    }

    PAL_STREAM_ATTR attr = {.share_flags = mode};

    ret = DkStreamAttributesSetByHandle(pal_hdl, &attr);
    if (ret < 0) {
        DkObjectClose(pal_hdl);
        return pal_to_unix_errno(ret);
    }

    DkObjectClose(pal_hdl);
    return 0;
}

struct shim_fs_ops chroot_fs_ops = {
    .mount      = &chroot_mount,
    .unmount    = &chroot_unmount,
    .flush      = &chroot_flush,
    .close      = &chroot_close,
    .read       = &chroot_read,
    .write      = &chroot_write,
    .mmap       = &chroot_mmap,
    .seek       = &chroot_seek,
    .hstat      = &chroot_hstat,
    .truncate   = &chroot_truncate,
    .hput       = &chroot_hput,
    .checkout   = &chroot_checkout,
    .checkpoint = &chroot_checkpoint,
    .migrate    = &chroot_migrate,
    .poll       = &chroot_poll,
};

struct shim_d_ops chroot_d_ops = {
    .open    = &chroot_open,
    .mode    = &chroot_mode,
    .lookup  = &chroot_lookup,
    .creat   = &chroot_creat,
    .mkdir   = &chroot_mkdir,
    .stat    = &chroot_stat,
    .dput    = &chroot_dput,
    .readdir = &chroot_readdir,
    .unlink  = &chroot_unlink,
    .rename  = &chroot_rename,
    .chmod   = &chroot_chmod,
};

struct shim_fs chroot_builtin_fs = {
    .name   = "chroot",
    .fs_ops = &chroot_fs_ops,
    .d_ops  = &chroot_d_ops,
};
