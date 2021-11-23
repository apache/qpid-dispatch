#ifndef __dispatch_buffer_field_h__
#define __dispatch_buffer_field_h__ 1
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

/** @file
 * Data fields spanning multiple buffers
 * @internal
 * @defgroup buffer_field buffer_field
 * @{
 */

#include "qpid/dispatch/buffer.h"


/* descriptor for a sequence of bytes in a buffer list
 */
typedef struct qd_buffer_field_t qd_buffer_field_t;
struct qd_buffer_field_t {
    qd_buffer_t   *buffer;     // hold start of data
    const uint8_t *cursor;     // first octet of data
    size_t         remaining;  // length of data
};

///@}

#endif
