#ifndef __dispatch_ctools_h__
#define __dispatch_ctools_h__ 1
/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/**@file
 * Double-ended queues and other useful macros.
 */

#include <stdlib.h>
#include <assert.h>

#define CT_ASSERT(exp) { assert(exp); }

#define NEW(t)             (t*)  malloc(sizeof(t))
#define NEW_ARRAY(t,n)     (t*)  malloc(sizeof(t)*(n))
#define NEW_PTR_ARRAY(t,n) (t**) malloc(sizeof(t*)*(n))

#define ZERO(p)            memset(p, 0, sizeof(*p))

#define DEQ_DECLARE(i,d) typedef struct { \
    i      *head;       \
    i      *tail;       \
    i      *scratch;    \
    size_t  size;       \
    } d

#define DEQ_LINKS(t) t *prev; t *next
#define DEQ_EMPTY {0,0,0,0}

#define DEQ_INIT(d) do { (d).head = 0; (d).tail = 0; (d).scratch = 0; (d).size = 0; } while (0)
#define DEQ_IS_EMPTY(d) ((d).head == 0)
#define DEQ_ITEM_INIT(i) do { (i)->next = 0; (i)->prev = 0; } while(0)
#define DEQ_HEAD(d) ((d).head)
#define DEQ_TAIL(d) ((d).tail)
#define DEQ_SIZE(d) ((d).size)
#define DEQ_NEXT(i) (i)->next
#define DEQ_PREV(i) (i)->prev
/**
 *@pre ptr points to first element of deq
 *@post ptr points to first element of deq that passes test, or 0. Test should involve ptr.
 */
#define DEQ_FIND(ptr,test) while((ptr) && !(test)) ptr = DEQ_NEXT(ptr);

#define DEQ_INSERT_HEAD(d,i)      \
do {                              \
    CT_ASSERT((i)->next == 0);    \
    CT_ASSERT((i)->prev == 0);    \
    if ((d).head) {               \
        (i)->next = (d).head;     \
        (d).head->prev = i;       \
    } else {                      \
        (d).tail = i;             \
        (i)->next = 0;            \
        CT_ASSERT((d).size == 0); \
    }                             \
    (i)->prev = 0;                \
    (d).head = i;                 \
    (d).size++;                   \
} while (0)

#define DEQ_INSERT_TAIL(d,i)      \
do {                              \
    CT_ASSERT((i)->next == 0);    \
    CT_ASSERT((i)->prev == 0);    \
    if ((d).tail) {               \
        (i)->prev = (d).tail;     \
        (d).tail->next = i;       \
    } else {                      \
        (d).head = i;             \
        (i)->prev = 0;            \
        CT_ASSERT((d).size == 0); \
    }                             \
    (i)->next = 0;                \
    (d).tail = i;                 \
    (d).size++;                   \
} while (0)

#define DEQ_REMOVE_HEAD(d)        \
do {                              \
    CT_ASSERT((d).head);          \
    if ((d).head) {               \
        (d).scratch = (d).head;   \
        (d).head = (d).head->next;  \
        if ((d).head == 0) {      \
            (d).tail = 0;         \
            CT_ASSERT((d).size == 1); \
        } else                  \
            (d).head->prev = 0; \
        (d).size--;             \
        (d).scratch->next = 0;  \
        (d).scratch->prev = 0;  \
    }                           \
} while (0)

#define DEQ_REMOVE_TAIL(d)      \
do {                            \
    CT_ASSERT((d).tail);        \
    if ((d).tail) {             \
        (d).scratch = (d).tail; \
        (d).tail = (d).tail->prev;  \
        if ((d).tail == 0) {    \
            (d).head = 0;       \
            CT_ASSERT((d).size == 1); \
        } else                  \
            (d).tail->next = 0; \
        (d).size--;             \
        (d).scratch->next = 0;  \
        (d).scratch->prev = 0;  \
    }                           \
} while (0)

#define DEQ_INSERT_AFTER(d,i,a) \
do {                            \
    CT_ASSERT((i)->next == 0);  \
    CT_ASSERT((i)->prev == 0);  \
    CT_ASSERT(a);               \
    if ((a)->next)              \
        (a)->next->prev = (i);  \
    else                        \
        (d).tail = (i);         \
    (i)->next = (a)->next;      \
    (i)->prev = (a);            \
    (a)->next = (i);            \
    (d).size++;                 \
} while (0)

#define DEQ_REMOVE(d,i)                        \
do {                                           \
    if ((i)->next)                             \
        (i)->next->prev = (i)->prev;           \
    else                                       \
        (d).tail = (i)->prev;                  \
    if ((i)->prev)                             \
        (i)->prev->next = (i)->next;           \
    else                                       \
        (d).head = (i)->next;                  \
    (d).size--;                                \
    (i)->next = 0;                             \
    (i)->prev = 0;                             \
    CT_ASSERT((d).size || (!(d).head && !(d).tail)); \
} while (0)

#define DEQ_APPEND(d1,d2)               \
do {                                    \
    if (!(d1).head)                     \
        (d1) = (d2);                    \
    else if ((d2).head) {               \
        (d1).tail->next = (d2).head;    \
        (d2).head->prev = (d1).tail;    \
        (d1).tail = (d2).tail;          \
        (d1).size += (d2).size;         \
    }                                   \
    DEQ_INIT(d2);                       \
} while (0)

#endif
