#ifndef __dispatch_buffer_h__
#define __dispatch_buffer_h__ 1
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
 * Buffer chains.
 * @internal
 * @defgroup buffer buffer
 * @{
 */

#include <qpid/dispatch/ctools.h>

typedef struct qd_buffer_t qd_buffer_t;

DEQ_DECLARE(qd_buffer_t, qd_buffer_list_t);

/** A raw byte buffer .*/
struct qd_buffer_t {
    DEQ_LINKS(qd_buffer_t);
    unsigned int size;          ///< Size of data content
};

/**
 * Set the initial buffer capacity to be allocated by future calls to qp_buffer.
 */
void qd_buffer_set_size(size_t size);

/**
 * Create a buffer with capacity set by last call to qd_buffer_set_size(), and data
 * content size of 0 bytes.
 */
qd_buffer_t *qd_buffer(void);

/**
 * Free a buffer
 * @param buf A pointer to an allocated buffer
 */
void qd_buffer_free(qd_buffer_t *buf);

/**
 * Return a pointer to the start of the buffer.
 * @param buf A pointer to an allocated buffer
 * @return A pointer to the first octet in the buffer
 */
unsigned char *qd_buffer_base(qd_buffer_t *buf);

/**
 * Return a pointer to the first unused byte in the buffer.
 * @param buf A pointer to an allocated buffer
 * @return A pointer to the first free octet in the buffer, the insert point for new data.
 */
unsigned char *qd_buffer_cursor(qd_buffer_t *buf);

/**
 * Return remaining capacity at end of buffer.
 * @param buf A pointer to an allocated buffer
 * @return The number of octets in the buffer's free space, how many octets may be inserted.
 */
size_t qd_buffer_capacity(qd_buffer_t *buf);

/**
 * Return the size of the buffers data content.
 * @param buf A pointer to an allocated buffer
 * @return The number of octets of data in the buffer
 */
size_t qd_buffer_size(qd_buffer_t *buf);

/**
 * Notify the buffer that octets have been inserted at the buffer's cursor.  This will advance the
 * cursor by len octets.
 *
 * @param buf A pointer to an allocated buffer
 * @param len The number of octets that have been appended to the buffer
 */
void qd_buffer_insert(qd_buffer_t *buf, size_t len);

///@}

#endif
