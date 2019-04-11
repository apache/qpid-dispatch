#ifndef ALLOC_MALLOC_H
#define ALLOC_MALLOC_H
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

#include <stdint.h>
#include <string.h>
#include <qpid/dispatch/ctools.h>

/**
 *@file
 *
 * Use simple malloc/free allocation in place of allocation pools.
 * Useful for debugging with tools like valgrind.
 */

typedef struct {
    void     *ptr;
    uint32_t  seq;
} qd_alloc_safe_ptr_t;

#define ALLOC_DECLARE(T)                \
    T *new_##T(void);                   \
    void free_##T(T *p);                \
    typedef qd_alloc_safe_ptr_t T##_sp; \
    void set_safe_ptr_##T(T *p, T##_sp *sp); \
    T *safe_deref_##T(T##_sp sp)

#define ALLOC_DEFINE_CONFIG(T,S,A,C)                \
    T *new_##T(void) { size_t *a = (A);             \
        T *p = malloc((S)+ (a ? *a : 0));           \
        QD_MEMORY_FILL(p, QD_MEMORY_INIT, (S) + (a ? *a : 0)); \
        return p; }                                 \
    void free_##T(T *p) { size_t *a = (A);          \
        QD_MEMORY_FILL(p, QD_MEMORY_FREE, (S) + (a ? *a : 0)); \
        free(p); }                                  \
    void set_safe_ptr_##T(T *p, T##_sp *sp) { sp->ptr = (void*) p; sp->seq = qd_alloc_sequence((void*) p); } \
    T *safe_deref_##T(T##_sp sp) { return sp.seq == qd_alloc_sequence((void*) sp.ptr) ? (T*) sp.ptr : (T*) 0; } \
    void *unused##T

#define ALLOC_DEFINE(T) ALLOC_DEFINE_CONFIG(T, sizeof(T), 0, 0)

static inline uint32_t qd_alloc_sequence(void *p) { return 0; }
static inline void qd_nullify_safe_ptr(qd_alloc_safe_ptr_t *sp) { }
static inline void qd_alloc_initialize(void) {}
static inline void qd_alloc_debug_dump(const char *file) {}
static inline void qd_alloc_finalize(void) {}


#endif // ALLOC_MALLOC_H
