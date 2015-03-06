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

/**
 *@file
 *
 * Use simple malloc/free allocation in place of allocation pools.
 * Useful for debugging with tools like valgrind.
 */

#define ALLOC_DECLARE(T)                        \
    T *new_##T(void);                           \
    void free_##T(T *p);

#define ALLOC_DEFINE_CONFIG(T,S,A,C)                                    \
    T *new_##T(void) { size_t *a = (A); return (T*) malloc((S)+ (a ? *a : 0)); } \
    void free_##T(T *p) { free(p); } \

#define ALLOC_DEFINE(T) ALLOC_DEFINE_CONFIG(T, sizeof(T), 0, 0)

static inline void qd_alloc_initialize(void) {}
static inline void qd_alloc_debug_dump(const char *file) {}
static inline void qd_alloc_finalize(void) {}


#endif // ALLOC_MALLOC_H
