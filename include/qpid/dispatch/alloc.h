#ifndef ALLOC_H
#define ALLOC_H
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

#include "config.h"

#include <string.h>

#if !defined(NDEBUG)
#define QD_MEMORY_DEBUG 1
// when debugging fill allocated/deallocated memory
// to catch uninitialized access or use after free
#define QD_MEMORY_FREE 0x99
#define QD_MEMORY_INIT 0x11
#define QD_MEMORY_FILL(P,C,S) do { if (P) { memset((P),(C),(S)); } } while (0)
#else
#define QD_MEMORY_FILL(P,C,S)
#endif

#include "alloc_pool.h"

#endif // ALLOC_H
