/* -*- mode:c; c-file-style:"k&r"; c-basic-offset: 4; tab-width:4; indent-tabs-mode:nil; mode:auto-fill; fill-column:78; -*- */
/* vim: set ts=4 sw=4 et tw=78 fo=cqt wm=0: */

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
 * shim_fs_hash.c
 *
 * This file contains functions to generate hash values for FS paths.
 */

#include <shim_internal.h>

static HASHTYPE __hash(const char* p, size_t len) {
    HASHTYPE hash = 0;

    for (; len >= sizeof(hash); p += sizeof(hash), len -= sizeof(hash)) {
        hash += *((HASHTYPE*) p);
        hash *= 9;
    }

    if (len) {
        HASHTYPE rest = 0;
        for (; len > 0; p++, len--) {
            rest  += (HASHTYPE) *p;
            rest <<= 8;
        }
        hash += rest;
        hash *= 9;
    }

    return hash;
}

HASHTYPE hash_path(const char* path, size_t size) {
    HASHTYPE digest = 0;

    const char* name = path;
    const char* c    = path;

    for (; c < path+size && *c; c++) {
        if (*c == '/') {
            digest ^= __hash(name, c - name);
            name = c + 1;
        }
    }

    digest ^= __hash(name, c - name);
    return digest;
}

HASHTYPE rehash_name(HASHTYPE parent_hbuf, const char* name, size_t size) {
    return parent_hbuf ^ __hash(name, size);
}

HASHTYPE rehash_path(HASHTYPE ancestor_hbuf, const char* path, size_t size) {
    return ancestor_hbuf ^ hash_path(path, size);
}
