/* Copyright (C) 2017 University of North Carolina at Chapel Hill and
   Fortanix, Inc.
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
 * list.h
 *
 * This file defines the list API for the PAL and Library OS.
 */

#ifndef LIST_H
#define LIST_H

// Use a new list implementation

/* This list implementation stores a pointer to the next object and casts to
 * the object, rather than using offsetof().  We try to encapsulate this
 * change in a macro for declarations, which generates a type declaration for
 * each list object (giving marginally more help from the compiler
 * in detecting bugs.
 *
 * In particular, there is a small trade-off in that the association between
 * list heads and nodes is more explicit and a few more casting errors can be
 * caught by the compiler, but we add a parameter to some functions (well,
 * macros) to pass the field of the struct.
 */

/* How-to:
 *
 * Each list has a pointer (listp) type, and a node (list)type.  We assume
 * list nodes are embedded in a larger structure; the name of this structure
 * is used as part of the list type.
 *
 * To define a listp/list pair for a struct foo:
 *
 * DEFINE_LIST(foo);
 * struct foo {
 *   int x;
 *   LIST_TYPE(foo) list; // The list node
 * };
 *
 * DEFINE_LISTP(foo);
 * static LISTP_TYPE(foo) the_list = LISTP_INIT;
 *
 * -----
 *
 * From here, you can use LISTP_ADD variants to add an object from the list:
 *
 * struct foo *f = malloc(sizeof(struct foo));
 * f->x = 1;
 * INIT_LIST_HEAD(f, list); // The second parameter is the structure member
 * LISTP_ADD(f, &the_list, list);
 *
 * -----
 *
 * There are a number of add variants, some that add in a given position,
 * others that add to the head or the tail.
 *
 * You can search for an object using a variant of listp_for_each_entry. The
 * safe variants are safe against deletion.
 *
 * You can remove an object from a list using LISTP_DEL.
 *
 * In this example, we delete everything with a key bigger than 5.
 *
 * LIST_TYPE(foo) *f, *n; // n is not used, just for scratch space
 * LISTP_FOR_EACH_ENTRY_SAFE(f, n, &the_list, list) {
 *    if (f->x > 4) {
 *         LISTP_DEL(f, &the_list, list);
 *         free(f);
 *    }
 * }
 *
 *
 * LISTP_SPLICE moves an entire listp onto another, and list_move_tail takes
 * an element off of one list and places it on another.
 *
 * static LISTP_TYPE(foo) other_list; // Assume it is full of goodies
 *  // Move everything on other_list to the_list
 * LISTP_SPLICE_TAIL(&other_list, &the_list, list, foo); // the third argument
 *                                                       // is the field; the
 *                                                       // fourth is the type
 *                                                       // of the nodes (not
 *                                                       // the head pointer).
 *
 * // Use LISTP_EMPTY to test for emptiness of the list
 * assert(LISTP_EMPTY(&other_ist));
 *
 *  // Now move back anythign less than 6 back to other_list
 * LISTP_FOR_EACH_ENTRY_SAFE(f, n, &the_list, list) {
 *    if (f->x < 6)
 *         LISTP_MOVE_TAIL(f, &other_list, &the_list, list);
 * }
 *
 */

// Maybe TODO?
//
// Change the order of (node, head, field) -> (head, node, field)
// drop the listp type to reduce code changes?
// Cleaner way to express types
// Add assertion to delete (in debugging mode) that item is on list
// There are a few places where knowing the listp for deletion is cumbersome;
//    maybe drop this requirement?

#include <stdbool.h>

#ifdef DEBUG
#include <assert.h>
#define LIST_ASSERT(COND) assert(COND)
#else
#define LIST_ASSERT(COND)
#endif

#define LIST_TYPE(STRUCT_NAME) struct list_head##_##STRUCT_NAME
#define LISTP_TYPE(STRUCT_NAME) struct listp##_##STRUCT_NAME

#define MAX_SIZE (~(size_t)0)

/* Declare the enclosing struct for convenience, on
 * the assumption that this is primarily used in structure
 * definitions, and harmless if duplicated. */
#define DEFINE_LIST(STRUCT_NAME)  \
    struct STRUCT_NAME;           \
    LIST_TYPE(STRUCT_NAME) {      \
        struct STRUCT_NAME* next; \
        struct STRUCT_NAME* prev; \
    };                            \
    LIST_SORT_FUNC_PTR_DECLARE();

#define LIST_SORT_FUNC_PTR_DECLARE() typedef bool (*list_sort)(void* node1, void* node2);

/* We use LISTP for pointers to a list.  This project only really needs
 * doubly-linked lists.  We used hlists to get a single pointer for more
 * efficient hash tables, but they were still effectively doubly-linked
 * lists. */
#define DEFINE_LISTP(STRUCT)  \
    LISTP_TYPE(STRUCT) {      \
        struct STRUCT* first; \
        size_t size;          \
        list_sort sort_fptr;  \
    }

#define LISTP_INIT { NULL, 0, NULL }

#define LISTP_INITIALIZE(LISTP)        \
    do {                               \
        if ((LISTP)) {                 \
            (LISTP)->first     = NULL; \
            (LISTP)->size      = 0;    \
            (LISTP)->sort_fptr = NULL; \
        }                              \
    } while (0)

#define LISTP_SET_SORT_FUNCTION(LISTP, LIST_SORT_FUNCTION) ((LISTP)->sort_fptr = LIST_SORT_FUNCTION)

// TODO: currently sorting is O(n^2), make it better.
#define LISTP_SORT(LISTP, STRUCT_NAME)                                                          \
    do {                                                                                        \
        struct STRUCT_NAME* head  = NULL;                                                       \
        struct STRUCT_NAME* outer = NULL;                                                       \
        struct STRUCT_NAME* inner = NULL;                                                       \
        if (LISTP) {                                                                          \
            if ((LISTP)->sort_fptr) {                                                           \
                head                = LISTP_FIRST_ENTRY(LISTP, STRUCT_NAME, list);              \
                outer               = head;                                                     \
                bool adjacent_nodes = 0;                                                        \
                while (outer != NULL) {                                                         \
                    inner = LISTP_NEXT_ENTRY(outer, LISTP, list);                               \
                    while (inner != NULL) {                                                     \
                        if (!((LISTP)->sort_fptr(outer, inner))) {                              \
                            struct STRUCT_NAME* for_swap = NULL;                                \
                            adjacent_nodes =                                                    \
                                (LISTP_NEXT_ENTRY(outer, LISTP, list) == inner) ? true : false; \
                            if (!adjacent_nodes) {                                              \
                                struct STRUCT_NAME* prev_of_inner = NULL;                       \
                                struct STRUCT_NAME* prev_of_outer = NULL;                       \
                                prev_of_inner = LISTP_PREV_ENTRY(inner, LISTP, list);           \
                                prev_of_outer = LISTP_PREV_ENTRY(outer, LISTP, list);           \
                                LISTP_DEL(inner, LISTP, list);                                  \
                                LISTP_DEL(outer, LISTP, list);                                  \
                                LISTP_ADD_AFTER(inner, prev_of_outer, LISTP, list);             \
                                LISTP_ADD_AFTER(outer, prev_of_inner, LISTP, list);             \
                            } else {                                                            \
                                LISTP_DEL(outer, LISTP, list);                                  \
                                LISTP_ADD_AFTER(outer, inner, LISTP, list);                     \
                            }                                                                   \
                            for_swap = inner;                                                   \
                            inner    = outer;                                                   \
                            outer    = for_swap;                                                \
                        }                                                                       \
                        inner = LISTP_NEXT_ENTRY(inner, LISTP, list);                           \
                    }                                                                           \
                    outer = LISTP_NEXT_ENTRY(outer, LISTP, list);                               \
                }                                                                               \
            }                                                                                   \
        }                                                                                       \
    } while (0)

/* A node not on a list uses NULL; on a list, you
 * store self pointers */
#define INIT_LIST_HEAD(OBJECT, FIELD) \
    do {                              \
        (OBJECT)->FIELD.next = NULL;  \
        (OBJECT)->FIELD.prev = NULL;  \
    } while (0)

#define INIT_LISTP(OBJECT)      \
    do {                        \
        (OBJECT)->first = NULL; \
        (OBJECT)->size  = 0;    \
    } while (0)

#define LISTP_EMPTY(HEAD) ((HEAD)->first == NULL)

#define LISTP_SIZE(HEAD) ((HEAD)->size)

#define LIST_EMPTY(NODE, FIELD) ((NODE)->FIELD.next == NULL)

/* This helper takes 3 arguments - all should be containing structures,
 * and the field to use for the offset to the list node */
#define __LIST_ADD(NEW, NEXT, PREV, FIELD)       \
    do {                                         \
        __typeof__(NEW) __tmp_next = (NEXT);     \
        __typeof__(NEW) __tmp_prev = (PREV);     \
        __tmp_prev->FIELD.next     = (NEW);      \
        __tmp_next->FIELD.prev     = (NEW);      \
        (NEW)->FIELD.next          = __tmp_next; \
        (NEW)->FIELD.prev          = __tmp_prev; \
    } while (0)

/* Note: If LIST_ADD* macros are invoked directly,
 * size of list cannot be updated. Need to use LISTP_ADD* macros,
 * to make sure, size of list gets updated. */
#define LIST_ADD(NEW, HEAD, FIELD) __LIST_ADD(NEW, (HEAD)->FIELD.next, HEAD, FIELD)

#define LISTP_ADD(NEW, HEAD, FIELD)                                           \
    do {                                                                      \
        if ((HEAD)->first == NULL) {                                          \
            (HEAD)->first     = (NEW);                                        \
            (NEW)->FIELD.next = (NEW);                                        \
            (NEW)->FIELD.prev = (NEW);                                        \
            (HEAD)->size      = 1;                                            \
        } else {                                                              \
            LIST_ASSERT((HEAD)->size < MAX_SIZE);                             \
            __LIST_ADD(NEW, (HEAD)->first, (HEAD)->first->FIELD.prev, FIELD); \
            ++(HEAD)->size;                                                   \
            (HEAD)->first = (NEW);                                            \
        }                                                                     \
    } while (0)

/* If NODE is defined, add NEW after NODE; if not,
 * put NEW at the front of the list */
#define LISTP_ADD_AFTER(NEW, NODE, HEAD, FIELD)   \
    do {                                          \
        if (NODE) {                               \
            LIST_ASSERT((HEAD)->size < MAX_SIZE); \
            LIST_ADD(NEW, NODE, FIELD);           \
            ++(HEAD)->size;                       \
        } else {                                    \
            LISTP_ADD(NEW, HEAD, FIELD);  \
        }  \
    } while (0)

#define LIST_ADD_TAIL(NEW, HEAD, FIELD) __LIST_ADD(NEW, HEAD, (HEAD)->FIELD.prev, FIELD)

#define LISTP_ADD_TAIL(NEW, HEAD, FIELD)              \
    do {                                              \
        if ((HEAD)->first == NULL) {                  \
            (HEAD)->first     = (NEW);                \
            (NEW)->FIELD.next = (NEW);                \
            (NEW)->FIELD.prev = (NEW);                \
            (HEAD)->size      = 1;                    \
        } else {                                      \
            LIST_ASSERT((HEAD)->size < MAX_SIZE);     \
            LIST_ADD_TAIL(NEW, (HEAD)->first, FIELD); \
            ++(HEAD)->size;                           \
        }                                             \
    } while (0)

#define LISTP_PUSH_FRONT(NEW, LISTP, FIELD) LISTP_ADD(NEW, LISTP, FIELD)
#define LISTP_POP_FRONT(FIRST_ENTRY, LISTP, STRUCT_NAME, FIELD)                   \
    do {                                                                          \
        struct STRUCT_NAME* first = LISTP_FIRST_ENTRY(LISTP, STRUCT_NAME, FIELD); \
        if (first) {                                                              \
            LISTP_DEL(first, LISTP, FIELD);                                       \
            FIRST_ENTRY = first;                                                  \
        } else {                                                                    \
            FIRST_ENTRY = NULL;                                \
        }                                                                                   \
    } while (0)

/* Or deletion needs to know the list root */
#define LISTP_DEL(NODE, HEAD, FIELD)                           \
    do {                                                       \
        if ((HEAD)->first == (NODE)) {                         \
            if ((NODE)->FIELD.next == (NODE)) {                \
                (HEAD)->first = NULL;                          \
            } else {                                           \
                (HEAD)->first = (NODE)->FIELD.next;            \
            }                                                  \
        }                                                      \
        LIST_ASSERT((NODE)->FIELD.prev->FIELD.next == (NODE)); \
        LIST_ASSERT((NODE)->FIELD.next->FIELD.prev == (NODE)); \
        (NODE)->FIELD.prev->FIELD.next = (NODE)->FIELD.next;   \
        (NODE)->FIELD.next->FIELD.prev = (NODE)->FIELD.prev;   \
        --((HEAD)->size);                                      \
    } while (0)

#define LISTP_DEL_INIT(NODE, HEAD, FIELD) \
    do {                                  \
        LISTP_DEL(NODE, HEAD, FIELD);     \
        INIT_LIST_HEAD(NODE, FIELD);      \
    } while (0)

/* clears linked list container, does not free each list node, like list.clear(). */
#define LISTP_CLEAR(LISTP, STRUCT_NAME)                       \
    do {                                                      \
        struct STRUCT_NAME *first, *next;                     \
        LISTP_FOR_EACH_ENTRY_SAFE(first, next, LISTP, list) { \
            LISTP_DEL(first, LISTP, list);                    \
        }                                                     \
        LIST_ASSERT((LISTP)->size == 0);                                    \
    } while (0)

/* clears linked list container, and frees each list item. */
#define LISTP_CLEAR_AND_FREE_EACH_LIST_ITEM(LISTP, STRUCT_NAME) \
    do {                                                        \
        struct STRUCT_NAME *first, *next;                       \
        LISTP_FOR_EACH_ENTRY_SAFE(first, next, LISTP, list) {   \
            LISTP_DEL(first, LISTP, list);                      \
            free(first);                                        \
        }                                                       \
        LIST_ASSERT((LISTP)->size == 0);                                    \
    } while (0)

/* Keep vestigial TYPE and FIELD parameters to minimize disruption
 * when switching from Linux list implementation */
#define LISTP_FIRST_ENTRY(LISTP, TYPE, FIELD) ((LISTP)->first)

/* New API: return last entry in list */
#define LISTP_LAST_ENTRY(LISTP, TYPE, FIELD) ((LISTP)->first->FIELD.prev)

/* New API: return next entry in list */
#define LISTP_NEXT_ENTRY(NODE, LISTP, FIELD) \
    ((NODE) == (LISTP)->first->FIELD.prev ? NULL : (NODE)->FIELD.next)

/* New API: return previous entry in list */
#define LISTP_PREV_ENTRY(NODE, LISTP, FIELD) ((NODE) == (LISTP)->first ? NULL : (NODE)->FIELD.prev)

/* Vestigial - for compat with Linux list code; rename to listp?
 */
#define LIST_ENTRY(LISTP, TYPE, FIELD) (LISTP)

#define LISTP_FOR_EACH_ENTRY(CURSOR, HEAD, FIELD)                       \
    for (bool first_iter = ((CURSOR) = (HEAD)->first, !!(HEAD)->first); \
         first_iter || (CURSOR) != (HEAD)->first;                       \
         (CURSOR) = (CURSOR)->FIELD.next, first_iter = false)

#define LISTP_FOR_EACH_ENTRY_REVERSE(CURSOR, HEAD, FIELD)                             \
    for (bool first_iter =                                                            \
             ((CURSOR) = ((HEAD)->first ? (HEAD)->first->FIELD.prev : (HEAD)->first), \
             !!(HEAD)->first);                                                        \
         first_iter || ((CURSOR) && (CURSOR)->FIELD.next != (HEAD)->first);           \
         (CURSOR) = (CURSOR)->FIELD.prev, first_iter = false)

#define LISTP_FOR_EACH_ENTRY_SAFE(CURSOR, TMP, HEAD, FIELD)                                        \
    for (bool first_iter = ((CURSOR) = (HEAD)->first,                                              \
                           (TMP) = ((CURSOR) ? (CURSOR)->FIELD.next : (CURSOR)), !!(HEAD)->first); \
         (HEAD)->first &&                                                                          \
         (first_iter || (CURSOR) != (HEAD)->first);                                                \
         /* Handle the case where the first element was removed. */                                \
         first_iter = first_iter && (TMP) != (CURSOR) && (HEAD)->first == (TMP), (CURSOR) = (TMP), \
              (TMP) = (TMP)->FIELD.next)


/* Continue safe iteration with CURSOR->next */
#define LISTP_FOR_EACH_ENTRY_SAFE_CONTINUE(CURSOR, TMP, HEAD, FIELD)    \
    for ((CURSOR) = (CURSOR)->FIELD.next, (TMP) = (CURSOR)->FIELD.next; \
         (CURSOR) != (HEAD)->first && (HEAD)->first; (CURSOR) = (TMP), (TMP) = (TMP)->FIELD.next)

/* Assertion code written in Graphene project */
#define CHECK_LIST_HEAD(TYPE, HEAD, FIELD)                               \
    do {                                                                 \
        TYPE pos;                                                        \
        LISTP_FOR_EACH_ENTRY(pos, HEAD, FIELD) {                         \
            assert((pos->FIELD.prev != pos && pos->FIELD.next != pos) || \
                   (pos->FIELD.prev == pos && pos->FIELD.next == pos));  \
            assert(pos->FIELD.prev->FIELD.next == pos);                  \
            assert(pos->FIELD.next->FIELD.prev == pos);                  \
        }                                                                \
    } while (0)

// Add NEW to OLD at position first (assuming first is all we need for now)
// Can probably drop TYPE with some preprocessor smarts
/* Future TODO: Currently LISTP_SPLICE* macros are not used by PAL code.
 * Add support to make sure size of list gets updated correctly during splice operation. */
#define LISTP_SPLICE(NEW, OLD, FIELD, TYPE)                                      \
    do {                                                                         \
        if (!LISTP_EMPTY(NEW)) {                                                 \
            if (LISTP_EMPTY(OLD)) {                                              \
                (OLD)->first = (NEW)->first;                                     \
            } else {                                                             \
                struct TYPE* last_old                = (OLD)->first->FIELD.prev; \
                (OLD)->first->FIELD.prev->FIELD.next = (NEW)->first;             \
                (OLD)->first->FIELD.prev             = (NEW)->first->FIELD.prev; \
                (NEW)->first->FIELD.prev->FIELD.next = (OLD)->first;             \
                (NEW)->first->FIELD.prev             = last_old;                 \
                (OLD)->first                         = (NEW)->first;             \
            }                                                                    \
        }                                                                        \
    } while (0)

// Add NEW to OLD at last position
// Can probably drop TYPE with some preprocessor smarts
#define LISTP_SPLICE_TAIL(NEW, OLD, FIELD, TYPE)                                 \
    do {                                                                         \
        if (!LISTP_EMPTY(NEW)) {                                                 \
            if (LISTP_EMPTY(OLD)) {                                              \
                (OLD)->first = (NEW)->first;                                     \
            } else {                                                             \
                struct TYPE* last_old                = (OLD)->first->FIELD.prev; \
                last_old->FIELD.next                 = (NEW)->first;             \
                (OLD)->first->FIELD.prev             = (NEW)->first->FIELD.prev; \
                (NEW)->first->FIELD.prev->FIELD.next = (OLD)->first;             \
                (NEW)->first->FIELD.prev             = last_old;                 \
            }                                                                    \
        }                                                                        \
    } while (0)

#define LISTP_SPLICE_INIT(NEW, OLD, FIELD, TYPE) \
    do {                                         \
        LISTP_SPLICE(NEW, OLD, FIELD, TYPE);     \
        INIT_LISTP(NEW);                         \
    } while (0);

#define LISTP_SPLICE_TAIL_INIT(NEW, OLD, FIELD, TYPE) \
    do {                                              \
        LISTP_SPLICE_TAIL(NEW, OLD, FIELD, TYPE);     \
        INIT_LISTP(NEW);                              \
    } while (0);

// list_move_tail - delete from OLD, make tail of NEW
#define LISTP_MOVE_TAIL(NODE, NEW, OLD, FIELD) \
    do {                                       \
        LISTP_DEL_INIT(NODE, OLD, FIELD);      \
        LISTP_ADD_TAIL(NODE, NEW, FIELD);      \
    } while (0)

#endif  // LIST_H
