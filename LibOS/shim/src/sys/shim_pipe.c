/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * Implementation of system calls "pipe", "pipe2", "socketpair", "mknod", and "mknodat".
 */

#include <asm/fcntl.h>
#include <errno.h>

#include "pal.h"
#include "pal_error.h"
#include "perm.h"
#include "shim_flags_conv.h"
#include "shim_fs.h"
#include "shim_handle.h"
#include "shim_internal.h"
#include "shim_table.h"
#include "shim_types.h"
#include "shim_utils.h"
#include "stat.h"

static int create_pipes(struct shim_handle* srv, struct shim_handle* cli, int flags, char* name,
                        struct shim_qstr* qstr) {
    int ret = 0;
    char uri[PIPE_URI_SIZE];

    PAL_HANDLE hdl0 = NULL; /* server pipe (temporary, waits for connect from hdl2) */
    PAL_HANDLE hdl1 = NULL; /* one pipe end (accepted connect from hdl2) */
    PAL_HANDLE hdl2 = NULL; /* other pipe end (connects to hdl0 and talks to hdl1) */

    if ((ret = create_pipe(name, uri, PIPE_URI_SIZE, &hdl0, qstr,
                           /*use_vmid_for_name=*/false)) < 0) {
        log_error("pipe creation failure");
        return ret;
    }

    ret = DkStreamOpen(uri, 0, 0, 0, LINUX_OPEN_FLAGS_TO_PAL_OPTIONS(flags), &hdl2);
    if (ret < 0) {
        ret = pal_to_unix_errno(ret);
        log_error("pipe connection failure");
        goto out;
    }

    ret = DkStreamWaitForClient(hdl0, &hdl1);
    if (ret < 0) {
        ret = pal_to_unix_errno(ret);
        log_error("pipe acceptance failure");
        goto out;
    }

    PAL_HANDLE tmp = srv->pal_handle;
    srv->pal_handle = hdl1;

    if (flags & O_NONBLOCK) {
        ret = set_handle_nonblocking(cli, /*on=*/true);
        if (ret < 0)
            goto out;

        ret = set_handle_nonblocking(srv, /*on=*/true);
        if (ret < 0) {
            /* Restore original handle, if any. */
            srv->pal_handle = tmp;
            goto out;
        }
    }

    cli->pal_handle = hdl2;
    ret = 0;

out:
    if (ret < 0) {
        if (hdl1)
            DkObjectClose(hdl1);
        if (hdl2)
            DkObjectClose(hdl2);
    }
    DkStreamDelete(hdl0, 0); // TODO: handle errors
    DkObjectClose(hdl0);
    return ret;
}

static void undo_set_fd_handle(int fd) {
    if (fd >= 0) {
        struct shim_handle* hdl = detach_fd_handle(fd, NULL, NULL);
        if (hdl)
            put_handle(hdl);
    }
}

long shim_do_pipe2(int* filedes, int flags) {
    int ret = 0;

    if (flags & O_DIRECT) {
        log_warning("shim_do_pipe2(): ignoring not supported O_DIRECT flag");
        flags &= ~O_DIRECT;
    }

    if (flags & ~(O_NONBLOCK | O_CLOEXEC)) {
        return -EINVAL;
    }

    if (!is_user_memory_writable(filedes, 2 * sizeof(int)))
        return -EFAULT;

    int vfd1 = -1;
    int vfd2 = -1;

    struct shim_handle* hdl1 = get_new_handle();
    struct shim_handle* hdl2 = get_new_handle();

    if (!hdl1 || !hdl2) {
        ret = -ENOMEM;
        goto out;
    }

    hdl1->type = TYPE_PIPE;
    hdl1->fs = &pipe_builtin_fs;
    hdl1->flags = O_RDONLY;
    hdl1->acc_mode = MAY_READ;

    hdl2->type = TYPE_PIPE;
    hdl2->fs = &pipe_builtin_fs;
    hdl2->flags = O_WRONLY;
    hdl2->acc_mode = MAY_WRITE;

    hdl1->info.pipe.ready_for_ops = true;
    hdl2->info.pipe.ready_for_ops = true;

    ret = create_pipes(hdl1, hdl2, flags, hdl1->info.pipe.name, &hdl1->uri);
    if (ret < 0)
        goto out;

    memcpy(hdl2->info.pipe.name, hdl1->info.pipe.name, sizeof(hdl2->info.pipe.name));
    qstrcopy(&hdl2->uri, &hdl1->uri);

    vfd1 = set_new_fd_handle(hdl1, flags & O_CLOEXEC ? FD_CLOEXEC : 0, NULL);
    if (vfd1 < 0) {
        ret = vfd1;
        goto out;
    }

    vfd2 = set_new_fd_handle(hdl2, flags & O_CLOEXEC ? FD_CLOEXEC : 0, NULL);
    if (vfd2 < 0) {
        ret = vfd2;
        goto out;
    }

    filedes[0] = vfd1;
    filedes[1] = vfd2;

    ret = 0;
out:
    if (ret < 0) {
        undo_set_fd_handle(vfd1);
        undo_set_fd_handle(vfd2);
    }
    if (hdl1)
        put_handle(hdl1);
    if (hdl2)
        put_handle(hdl2);
    return ret;
}

long shim_do_pipe(int* filedes) {
    return shim_do_pipe2(filedes, 0);
}

long shim_do_socketpair(int domain, int type, int protocol, int* sv) {
    int ret = 0;

    if (domain != AF_UNIX)
        return -EAFNOSUPPORT;

    if ((type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) != SOCK_STREAM)
        return -EPROTONOSUPPORT;

    if (!is_user_memory_writable(sv, 2 * sizeof(int)))
        return -EFAULT;

    int vfd1 = -1;
    int vfd2 = -1;

    struct shim_handle* hdl1 = get_new_handle();
    struct shim_handle* hdl2 = get_new_handle();

    if (!hdl1 || !hdl2) {
        ret = -ENOMEM;
        goto out;
    }


    hdl1->type = TYPE_SOCK;
    hdl1->fs = &socket_builtin_fs;
    hdl1->flags = O_RDONLY;
    hdl1->acc_mode = MAY_READ | MAY_WRITE;

    struct shim_sock_handle* sock1 = &hdl1->info.sock;
    sock1->domain     = domain;
    sock1->sock_type  = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    sock1->protocol   = protocol;
    sock1->sock_state = SOCK_ACCEPTED;

    hdl2->type = TYPE_SOCK;
    hdl2->fs = &socket_builtin_fs;
    hdl2->flags = O_WRONLY;
    hdl2->acc_mode = MAY_READ | MAY_WRITE;

    struct shim_sock_handle* sock2 = &hdl2->info.sock;
    sock2->domain     = domain;
    sock2->sock_type  = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    sock2->protocol   = protocol;
    sock2->sock_state = SOCK_CONNECTED;

    ret = create_pipes(hdl1, hdl2, type & SOCK_NONBLOCK ? O_NONBLOCK : 0, sock1->addr.un.name,
                       &hdl1->uri);
    if (ret < 0)
        goto out;

    memcpy(sock2->addr.un.name, sock1->addr.un.name, sizeof(sock2->addr.un.name));
    qstrcopy(&hdl2->uri, &hdl1->uri);

    vfd1 = set_new_fd_handle(hdl1, type & SOCK_CLOEXEC ? FD_CLOEXEC : 0, NULL);
    if (vfd1 < 0) {
        ret = vfd1;
        goto out;
    }

    vfd2 = set_new_fd_handle(hdl2, type & SOCK_CLOEXEC ? FD_CLOEXEC : 0, NULL);
    if (vfd2 < 0) {
        ret = vfd2;
        goto out;
    }

    sv[0] = vfd1;
    sv[1] = vfd2;

    ret = 0;
out:
    if (ret < 0) {
        undo_set_fd_handle(vfd1);
        undo_set_fd_handle(vfd2);
    }
    if (hdl1)
        put_handle(hdl1);
    if (hdl2)
        put_handle(hdl2);
    return ret;
}

long shim_do_mknodat(int dirfd, const char* pathname, mode_t mode, dev_t dev) {
    int ret = 0;
    __UNUSED(dev);

    /* corner case of regular file: emulate via open() + close() */
    if (!(mode & S_IFMT) || S_ISREG(mode)) {
        mode &= ~S_IFREG;
        /* FIXME: Graphene assumes that file is at least readable by owner, in particular, see
         *        unlink() emulation that uses DkStreamOpen(). We change empty mode to readable
         *        by user here to allow a consequent unlink. Was detected on LTP mknod tests. */
        int fd = shim_do_openat(dirfd, pathname, O_CREAT | O_EXCL, mode ?: PERM_r________);
        if (fd < 0)
            return fd;
        return shim_do_close(fd);
    }

    int vfd1 = -1;
    int vfd2 = -1;

    struct shim_handle* hdl1 = NULL;
    struct shim_handle* hdl2 = NULL;

    if (!S_ISFIFO(mode))
        return -EINVAL;

    if (!is_user_string_readable(pathname))
        return -EFAULT;

    if (pathname[0] == '\0')
        return -ENOENT;

    /* add named pipe as a pseudo entry to file system (relative to dfd) */
    struct shim_dentry* dir  = NULL;
    struct shim_dentry* dent = NULL;

    if (*pathname != '/' && (ret = get_dirfd_dentry(dirfd, &dir)) < 0)
        goto out;

    ret = path_lookupat(dir, pathname, LOOKUP_NO_FOLLOW | LOOKUP_CREATE, &dent);
    if (ret < 0) {
        goto out;
    }

    if (!(dent->state & DENTRY_NEGATIVE)) {
        ret = -EEXIST;
        goto out;
    }

    dent->fs = &fifo_builtin_fs;
    dent->type = mode & S_IFMT;
    dent->perm = mode & ~S_IFMT;

    /* create two pipe ends */
    hdl1 = get_new_handle();
    hdl2 = get_new_handle();

    if (!hdl1 || !hdl2) {
        ret = -ENOMEM;
        goto out;
    }

    hdl1->type = TYPE_PIPE;
    hdl1->fs =  &fifo_builtin_fs;
    hdl1->flags = O_RDONLY;
    hdl1->acc_mode = MAY_READ;
    get_dentry(dent);
    hdl1->dentry = dent;

    hdl2->type = TYPE_PIPE;
    hdl2->fs = &fifo_builtin_fs;
    hdl2->flags = O_WRONLY;
    hdl2->acc_mode = MAY_WRITE;
    get_dentry(dent);
    hdl2->dentry = dent;

    /* FIFO must be open'ed to start read/write operations, mark as not ready */
    hdl1->info.pipe.ready_for_ops = false;
    hdl2->info.pipe.ready_for_ops = false;

    /* FIFO pipes are created in blocking mode; they will be changed to non-blocking if open()'ed
     * in non-blocking mode later (see fifo_open) */
    ret = create_pipes(hdl1, hdl2, /*flags=*/0, hdl1->info.pipe.name, &hdl1->uri);
    if (ret < 0)
        goto out;

    memcpy(hdl2->info.pipe.name, hdl1->info.pipe.name, sizeof(hdl2->info.pipe.name));
    qstrcopy(&hdl2->uri, &hdl1->uri);

    /* assign virtual FDs to both handles; ideally FDs must be assigned during open(<named-pipe>)
     * but then checkpointing after mknod() would not migrate the prepared hdl1 and hdl2; also FDs
     * are easier to bind to dentry created here */
    vfd1 = set_new_fd_handle(hdl1, /*fd_flags=*/0, NULL);
    if (vfd1 < 0) {
        ret = vfd1;
        goto out;
    }

    vfd2 = set_new_fd_handle(hdl2, /*fd_flags=*/0, NULL);
    if (vfd2 < 0) {
        ret = vfd2;
        goto out;
    }

    /* mark pseudo entry in file system as valid and stash FDs in data */
    dent->state &= ~DENTRY_NEGATIVE;
    dent->state |= DENTRY_VALID;

    static_assert(sizeof(vfd1) == sizeof(uint32_t) && sizeof(vfd2) == sizeof(uint32_t),
                  "FDs must be 4B in size");
    static_assert(sizeof(dent->data) >= sizeof(uint64_t),
                  "dentry's data must be at least 8B in size");
    dent->data = (void*)((uint64_t)vfd2 << 32 | (uint64_t)vfd1);

    ret = 0;
out:
    if (ret < 0) {
        undo_set_fd_handle(vfd1);
        undo_set_fd_handle(vfd2);
    }
    if (dir)
        put_dentry(dir);
    if (dent)
        put_dentry(dent);
    if (hdl1)
        put_handle(hdl1);
    if (hdl2)
        put_handle(hdl2);
    return ret;
}

long shim_do_mknod(const char* pathname, mode_t mode, dev_t dev) {
    return shim_do_mknodat(AT_FDCWD, pathname, mode, dev);
}
