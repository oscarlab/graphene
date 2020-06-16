/* Copyright (C) 2014 Stony Brook University
   This file is part of Graphene Library OS.

   Graphene Library OS is free software: you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   Graphene Library OS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/*
 * db_event.c
 *
 * This file contains implementation of Drawbridge event synchronization APIs.
 */

#include <atomic.h>
#include <linux/futex.h>
#include <linux/time.h>

#include "api.h"
#include "pal.h"
#include "pal_debug.h"
#include "pal_defs.h"
#include "pal_error.h"
#include "pal_internal.h"
#include "pal_linux.h"
#include "pal_linux_defs.h"
#include "pal_linux_error.h"

int _DkEventCreate(PAL_HANDLE* event, bool initialState, bool isnotification) {
    PAL_HANDLE ev = malloc(HANDLE_SIZE(event));
    SET_HANDLE_TYPE(ev, event);
    ev->event.isnotification = isnotification;
    ev->event.signaled       = malloc_untrusted(sizeof(uint32_t));
    if (!ev->event.signaled) {
        free(ev);
        return -PAL_ERROR_NOMEM;
    }
    __atomic_store_n(ev->event.signaled, initialState ? 1 : 0, __ATOMIC_RELAXED);
    atomic_set(&ev->event.nwaiters, 0);
    *event = ev;
    return 0;
}

int _DkEventSet(PAL_HANDLE event, int wakeup) {
    int ret = 0;

    if (event->event.isnotification) {
        // Leave it signaled, wake all
        uint32_t t = 0;
        if (__atomic_compare_exchange_n(event->event.signaled, &t, 1, /*weak=*/true,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            int nwaiters = atomic_read(&event->event.nwaiters);
            if (nwaiters) {
                if (wakeup != -1 && nwaiters > wakeup)
                    nwaiters = wakeup;

                ret = ocall_futex(event->event.signaled, FUTEX_WAKE, nwaiters, -1);

                if (IS_ERR(ret)) {
                    __atomic_store_n(event->event.signaled, 0, __ATOMIC_SEQ_CST);
                    ret = unix_to_pal_error(ERRNO(ret));
                }
            }
        }
    } else {
        // Only one thread wakes up, leave unsignaled
        ret = ocall_futex(event->event.signaled, FUTEX_WAKE, 1, -1);
        if (IS_ERR(ret))
            return unix_to_pal_error(ERRNO(ret));
    }

    return ret;
}

int _DkEventWaitTimeout(PAL_HANDLE event, int64_t timeout_us) {
    int ret = 0;

    if (timeout_us < 0)
        return _DkEventWait(event);

    if (!event->event.isnotification ||
        !__atomic_load_n(event->event.signaled, __ATOMIC_SEQ_CST)) {

        atomic_inc(&event->event.nwaiters);

        do {
            ret = ocall_futex(event->event.signaled, FUTEX_WAIT, 0, timeout_us);

            if (IS_ERR(ret)) {
                if (ERRNO(ret) == EWOULDBLOCK) {
                    ret = 0;
                } else {
                    ret = unix_to_pal_error(ERRNO(ret));
                    break;
                }
            }
        } while (event->event.isnotification &&
                 !__atomic_load_n(event->event.signaled, __ATOMIC_SEQ_CST));

        atomic_dec(&event->event.nwaiters);
    }

    return ret;
}

int _DkEventWait(PAL_HANDLE event) {
    int ret = 0;

    if (!event->event.isnotification ||
        !__atomic_load_n(event->event.signaled, __ATOMIC_SEQ_CST)) {

        atomic_inc(&event->event.nwaiters);

        do {
            ret = ocall_futex(event->event.signaled, FUTEX_WAIT, 0, -1);
            if (IS_ERR(ret)) {
                if (ERRNO(ret) == EWOULDBLOCK) {
                    ret = 0;
                } else {
                    ret = unix_to_pal_error(ERRNO(ret));
                    break;
                }
            }
        } while (event->event.isnotification &&
                 !__atomic_load_n(event->event.signaled, __ATOMIC_SEQ_CST));

        atomic_dec(&event->event.nwaiters);
    }

    return ret;
}

int _DkEventClear(PAL_HANDLE event) {
    __atomic_store_n(event->event.signaled, 0, __ATOMIC_SEQ_CST);
    return 0;
}

static int event_close(PAL_HANDLE handle) {
    _DkEventSet(handle, -1);
    free_untrusted(handle->event.signaled);
    return 0;
}

static int event_wait(PAL_HANDLE handle, int64_t timeout_us) {
    return _DkEventWaitTimeout(handle, timeout_us);
}

struct handle_ops event_ops = {
    .close = &event_close,
    .wait  = &event_wait,
};
