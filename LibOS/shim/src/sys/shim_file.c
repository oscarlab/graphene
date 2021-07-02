/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * Implementation of system calls "unlink", "unlinkat", "mkdir", "mkdirat", "rmdir", "umask",
 * "chmod", "fchmod", "fchmodat", "rename", "renameat" and "sendfile".
 */

#include <asm/mman.h>
#include <errno.h>
#include <linux/fcntl.h>

#include "pal.h"
#include "pal_error.h"
#include "shim_fs.h"
#include "shim_handle.h"
#include "shim_internal.h"
#include "shim_lock.h"
#include "shim_process.h"
#include "shim_table.h"
#include "shim_utils.h"
#include "stat.h"

#define BUF_SIZE 2048 /* read/write in 2KB chunks for sendfile(); buf is allocated on stack! */

/* The kernel would look up the parent directory, and remove the child from the inode. But we are
 * working with the PAL, so we open the file, truncate and close it. */
long shim_do_unlink(const char* file) {
    return shim_do_unlinkat(AT_FDCWD, file, 0);
}

long shim_do_unlinkat(int dfd, const char* pathname, int flag) {
    if (!is_user_string_readable(pathname))
        return -EFAULT;

    if (flag & ~AT_REMOVEDIR)
        return -EINVAL;

    struct shim_dentry* dir = NULL;
    struct shim_dentry* dent = NULL;
    int ret = 0;

    if (*pathname != '/' && (ret = get_dirfd_dentry(dfd, &dir)) < 0)
        return ret;

    if ((ret = path_lookupat(dir, pathname, LOOKUP_NO_FOLLOW, &dent)) < 0)
        goto out;

    if (!dent->parent) {
        ret = -EACCES;
        goto out;
    }

    if (flag & AT_REMOVEDIR) {
        if (!(dent->state & DENTRY_ISDIRECTORY)) {
            ret = -ENOTDIR;
            goto out;
        }
    } else {
        if (dent->state & DENTRY_ISDIRECTORY) {
            ret = -EISDIR;
            goto out;
        }
    }

    if (dent->fs && dent->fs->d_ops && dent->fs->d_ops->unlink) {
        if ((ret = dent->fs->d_ops->unlink(dent->parent, dent)) < 0) {
            goto out;
        }
    } else {
        dent->state |= DENTRY_PERSIST;
    }

    if (flag & AT_REMOVEDIR)
        dent->state &= ~DENTRY_ISDIRECTORY;

    dent->state |= DENTRY_NEGATIVE;
out:
    if (dir)
        put_dentry(dir);
    if (dent) {
        put_dentry(dent);
    }
    return ret;
}

long shim_do_mkdir(const char* pathname, int mode) {
    return shim_do_mkdirat(AT_FDCWD, pathname, mode);
}

long shim_do_mkdirat(int dfd, const char* pathname, int mode) {
    if (!is_user_string_readable(pathname))
        return -EFAULT;

    struct shim_dentry* dir = NULL;
    int ret = 0;

    if (*pathname != '/' && (ret = get_dirfd_dentry(dfd, &dir)) < 0)
        return ret;

    ret = open_namei(NULL, dir, pathname, O_CREAT | O_EXCL | O_DIRECTORY, mode, NULL);

    if (dir)
        put_dentry(dir);
    return ret;
}

long shim_do_rmdir(const char* pathname) {
    int ret = 0;
    struct shim_dentry* dent = NULL;

    if (!is_user_string_readable(pathname))
        return -EFAULT;

    if ((ret = path_lookupat(/*start=*/NULL, pathname, LOOKUP_NO_FOLLOW | LOOKUP_DIRECTORY,
                             &dent)) < 0)
        return ret;

    if (!dent->parent) {
        ret = -EACCES;
        goto out;
    }

    if (!(dent->state & DENTRY_ISDIRECTORY)) {
        ret = -ENOTDIR;
        goto out;
    }

    if (dent->fs && dent->fs->d_ops && dent->fs->d_ops->unlink) {
        if ((ret = dent->fs->d_ops->unlink(dent->parent, dent)) < 0)
            goto out;
    } else {
        dent->state |= DENTRY_PERSIST;
    }

    dent->state &= ~DENTRY_ISDIRECTORY;
    dent->state |= DENTRY_NEGATIVE;
out:
    put_dentry(dent);
    return ret;
}

long shim_do_umask(mode_t mask) {
    lock(&g_process.fs_lock);
    mode_t old = g_process.umask;
    g_process.umask = mask & 0777;
    unlock(&g_process.fs_lock);
    return old;
}

long shim_do_chmod(const char* path, mode_t mode) {
    return shim_do_fchmodat(AT_FDCWD, path, mode);
}

long shim_do_fchmodat(int dfd, const char* filename, mode_t mode) {
    if (!is_user_string_readable(filename))
        return -EFAULT;

    /* This isn't documented, but that's what Linux does. */
    mode &= 07777;

    struct shim_dentry* dir = NULL;
    struct shim_dentry* dent = NULL;
    int ret = 0;

    if (*filename != '/' && (ret = get_dirfd_dentry(dfd, &dir)) < 0)
        return ret;

    if ((ret = path_lookupat(dir, filename, LOOKUP_FOLLOW, &dent)) < 0)
        goto out;

    if (dent->fs && dent->fs->d_ops && dent->fs->d_ops->chmod) {
        if ((ret = dent->fs->d_ops->chmod(dent, mode)) < 0)
            goto out_dent;
    } else {
        dent->state |= DENTRY_PERSIST;
    }

    dent->perm = mode;
out_dent:
    put_dentry(dent);
out:
    if (dir)
        put_dentry(dir);
    return ret;
}

long shim_do_fchmod(int fd, mode_t mode) {
    struct shim_handle* hdl = get_fd_handle(fd, NULL, NULL);
    if (!hdl)
        return -EBADF;

    /* This isn't documented, but that's what Linux does. */
    mode &= 07777;

    struct shim_dentry* dent = hdl->dentry;
    int ret = 0;

    if (!dent) {
        ret = -EINVAL;
        goto out;
    }

    if (dent->fs && dent->fs->d_ops && dent->fs->d_ops->chmod) {
        if ((ret = dent->fs->d_ops->chmod(dent, mode)) < 0)
            goto out;
    } else {
        dent->state |= DENTRY_PERSIST;
    }

    dent->perm = mode;
out:
    put_handle(hdl);
    return ret;
}

long shim_do_chown(const char* path, uid_t uid, gid_t gid) {
    return shim_do_fchownat(AT_FDCWD, path, uid, gid, 0);
}

long shim_do_fchownat(int dfd, const char* filename, uid_t uid, gid_t gid, int flags) {
    __UNUSED(flags);
    __UNUSED(uid);
    __UNUSED(gid);

    if (!is_user_string_readable(filename))
        return -EFAULT;

    struct shim_dentry* dir = NULL;
    struct shim_dentry* dent = NULL;
    int ret = 0;

    if (*filename != '/' && (ret = get_dirfd_dentry(dfd, &dir)) < 0)
        return ret;

    if ((ret = path_lookupat(dir, filename, LOOKUP_FOLLOW, &dent)) < 0)
        goto out;

    /* XXX: do nothing now */
    put_dentry(dent);
out:
    if (dir)
        put_dentry(dir);
    return ret;
}

long shim_do_fchown(int fd, uid_t uid, gid_t gid) {
    __UNUSED(uid);
    __UNUSED(gid);

    struct shim_handle* hdl = get_fd_handle(fd, NULL, NULL);
    if (!hdl)
        return -EBADF;

    /* XXX: do nothing now */
    return 0;
}

static int do_rename(struct shim_dentry* old_dent, struct shim_dentry* new_dent) {
    if ((old_dent->type != S_IFREG) ||
            (!(new_dent->state & DENTRY_NEGATIVE) && (new_dent->type != S_IFREG))) {
        /* Current implementation of fs does not allow for renaming anything but regular files */
        return -ENOSYS;
    }

    if (old_dent->fs != new_dent->fs) {
        /* Disallow cross mount renames */
        return -EXDEV;
    }

    if (!old_dent->fs || !old_dent->fs->d_ops || !old_dent->fs->d_ops->rename) {
        return -EPERM;
    }

    if (old_dent->state & DENTRY_ISDIRECTORY) {
        if (!(new_dent->state & DENTRY_NEGATIVE)) {
            if (!(new_dent->state & DENTRY_ISDIRECTORY)) {
                return -ENOTDIR;
            }
            if (new_dent->nchildren > 0) {
                return -ENOTEMPTY;
            }
        } else {
            /* destination is a negative dentry and needs to be marked as a directory, since source
             * is a directory */
            new_dent->state |= DENTRY_ISDIRECTORY;
        }
    } else if (new_dent->state & DENTRY_ISDIRECTORY) {
        return -EISDIR;
    }

    if (dentry_is_ancestor(old_dent, new_dent) || dentry_is_ancestor(new_dent, old_dent)) {
        return -EINVAL;
    }

    /* TODO: Add appropriate checks for hardlinks once they get implemented. */

    int ret = old_dent->fs->d_ops->rename(old_dent, new_dent);
    if (!ret) {
        old_dent->state |= DENTRY_NEGATIVE;
        new_dent->state &= ~DENTRY_NEGATIVE;
    }

    return ret;
}

long shim_do_rename(const char* oldpath, const char* newpath) {
    return shim_do_renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath);
}

long shim_do_renameat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath) {
    struct shim_dentry* old_dir_dent = NULL;
    struct shim_dentry* old_dent     = NULL;
    struct shim_dentry* new_dir_dent = NULL;
    struct shim_dentry* new_dent     = NULL;
    int ret = 0;

    if (!is_user_string_readable(oldpath) || !is_user_string_readable(newpath)) {
        return -EFAULT;
    }

    if (*oldpath != '/' && (ret = get_dirfd_dentry(olddirfd, &old_dir_dent)) < 0) {
        goto out;
    }

    if ((ret = path_lookupat(old_dir_dent, oldpath, LOOKUP_NO_FOLLOW, &old_dent)) < 0) {
        goto out;
    }

    if (old_dent->state & DENTRY_NEGATIVE) {
        ret = -ENOENT;
        goto out;
    }

    if (*newpath != '/' && (ret = get_dirfd_dentry(newdirfd, &new_dir_dent)) < 0) {
        goto out;
    }

    ret = path_lookupat(new_dir_dent, newpath, LOOKUP_NO_FOLLOW | LOOKUP_CREATE, &new_dent);
    if (ret < 0)
        goto out;

    // Both dentries should have a ref count of at least 2 at this point
    assert(REF_GET(old_dent->ref_count) >= 2);
    assert(REF_GET(new_dent->ref_count) >= 2);

    ret = do_rename(old_dent, new_dent);

out:
    if (old_dir_dent)
        put_dentry(old_dir_dent);
    if (old_dent)
        put_dentry(old_dent);
    if (new_dir_dent)
        put_dentry(new_dir_dent);
    if (new_dent)
        put_dentry(new_dent);
    return ret;
}

long shim_do_sendfile(int ofd, int ifd, off_t* offset, size_t count) {
    int ret;

    if (offset && !is_user_memory_writable(offset, sizeof(*offset)))
        return -EFAULT;

    struct shim_handle* hdli = get_fd_handle(ifd, NULL, NULL);
    if (!hdli)
        return -EBADF;

    struct shim_handle* hdlo = get_fd_handle(ofd, NULL, NULL);
    if (!hdlo) {
        put_handle(hdli);
        return -EBADF;
    }

    if (!hdli->fs || !hdli->fs->fs_ops || !hdlo->fs || !hdlo->fs->fs_ops) {
        ret = -EINVAL;
        goto out;
    }

    if (hdlo->flags & O_APPEND) {
        /* Linux errors out if output fd has the O_APPEND flag set; comply with this behavior */
        ret = -EINVAL;
        goto out;
    }

    /* FIXME: This sendfile() emulation is very simple and not particularly efficient: it reads from
     *        input FD in BUF_SIZE chunks (allocated on stack so must be small-sized) and writes
     *        into output FD. Mmap-based emulation may be more efficient but adds complexity (not
     *        all handle types provide mmap callback). */
    char buf[BUF_SIZE];
    size_t copied = 0;

    if (!count) {
        ret = 0;
        goto out;
    }

    off_t old_offset = 0;
    off_t tmp_offset = 0;

    if (offset) {
        if (!hdli->fs->fs_ops->seek) {
            ret = -ESPIPE;
            goto out;
        }

        old_offset = hdli->fs->fs_ops->seek(hdli, 0, SEEK_CUR);
        if (old_offset < 0) {
            ret = old_offset;
            goto out;
        }

        tmp_offset = hdli->fs->fs_ops->seek(hdli, *offset, SEEK_SET);
        if (tmp_offset < 0) {
            ret = tmp_offset;
            goto out;
        }
    }

    while (copied < count) {
        size_t to_copy = count - copied > BUF_SIZE ? BUF_SIZE : count - copied;

        ssize_t x;
        while (true) {
            x = hdli->fs->fs_ops->read(hdli, buf, to_copy);
            if (x < 0) {
                if (x == -EINTR) {
                    continue;
                }
                ret = x;
                goto out;
            }
            break;
        }
        assert(x <= (ssize_t)to_copy);

        if (x == 0) {
            /* no more data in input FD, let's return however many bytes copied up until now */
            break;
        }

        ssize_t y;
        while (true) {
            y = hdlo->fs->fs_ops->write(hdlo, buf, x);
            if (y < 0) {
                if (y == -EINTR) {
                    continue;
                }
                ret = y;
                goto out;
            }
            break;
        }
        assert(y <= x);

        copied += y;

        if (y < x) {
            /* written less bytes to output fd than read from input fd -> out of sync now; don't try
             * to be smart and simply return however many bytes we copied up until now */
            break;
        }
    }

    if (offset) {
        /* manpage: "if offset != NULL, then sendfile() does not modify file offset of ifd..." */
        tmp_offset = hdli->fs->fs_ops->seek(hdli, old_offset, SEEK_SET);
        if (tmp_offset < 0) {
            ret = tmp_offset;
            goto out;
        }

        /* "...and the file offset will be updated by the call" */
        *offset = *offset + copied;
    }

    ret = 0;
out:
    put_handle(hdli);
    put_handle(hdlo);
    return ret < 0 ? ret : (long)copied;
}

long shim_do_chroot(const char* filename) {
    int ret = 0;
    struct shim_dentry* dent = NULL;
    if ((ret = path_lookupat(/*start=*/NULL, filename, LOOKUP_FOLLOW | LOOKUP_DIRECTORY, &dent)) < 0)
        goto out;

    if (!dent) {
        ret = -ENOENT;
        goto out;
    }

    lock(&g_process.fs_lock);
    put_dentry(g_process.root);
    g_process.root = dent;
    unlock(&g_process.fs_lock);
out:
    return ret;
}
