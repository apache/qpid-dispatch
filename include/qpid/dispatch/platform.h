#ifndef __dispatch_platform_h__
#define __dispatch_platform_h__ 1
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

/**
 * Platform specific utility functions
 */

/**
 * Determine the amount of usable 'fast' memory (RAM only, not including swap)
 * installed.  If resource constraints are in use (e.g setrlimit(),
 * containerization, etc) for the qdrouterd process this routine should return
 * the minimum of (physical memory, memory resource limit).
 *
 * This value is used by the router to detect and react to low memory
 * conditions.
 *
 * Returns the size of fast memory in bytes or zero if the size cannot be
 * determined.
 */
uintmax_t qd_platform_memory_size(void);

#endif
