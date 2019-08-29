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
#include <qpid/dispatch/atomic.h>

typedef struct qd_buffer_t qd_buffer_t;

DEQ_DECLARE(qd_buffer_t, qd_buffer_list_t);

extern size_t BUFFER_SIZE;

/** A raw byte buffer .*/
struct qd_buffer_t {
    DEQ_LINKS(qd_buffer_t);
    unsigned int size;          ///< Size of data content
    sys_atomic_t bfanout;        // The number of receivers for this buffer
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
static inline unsigned char *qd_buffer_base(const qd_buffer_t *buf)
{
    return (unsigned char*) &buf[1];
}

/**
 * Return a pointer to the first unused byte in the buffer.
 * @param buf A pointer to an allocated buffer
 * @return A pointer to the first free octet in the buffer, the insert point for new data.
 */
static inline unsigned char *qd_buffer_cursor(const qd_buffer_t *buf)
{
    return ((unsigned char*) &buf[1]) + buf->size;
}

/**
 * Return remaining capacity at end of buffer.
 * @param buf A pointer to an allocated buffer
 * @return The number of octets in the buffer's free space, how many octets may be inserted.
 */
static inline size_t qd_buffer_capacity(const qd_buffer_t *buf)
{
    return BUFFER_SIZE - buf->size;
}

/**
 * Return the size of the buffers data content.
 * @param buf A pointer to an allocated buffer
 * @return The number of octets of data in the buffer
 */
static inline size_t qd_buffer_size(const qd_buffer_t *buf)
{
    return buf->size;
}

/**
 * Notify the buffer that octets have been inserted at the buffer's cursor.  This will advance the
 * cursor by len octets.
 *
 * @param buf A pointer to an allocated buffer
 * @param len The number of octets that have been appended to the buffer
 */
static inline void qd_buffer_insert(qd_buffer_t *buf, size_t len)
{
    buf->size += len;
    assert(buf->size <= BUFFER_SIZE);
}

/**
 * Create a new buffer list by cloning an existing one.
 *
 * @param dst A pointer to a list to contain the new buffers
 * @param src A pointer to an existing buffer list
 * @return the number of bytes of data in the new chain
 */
unsigned int qd_buffer_list_clone(qd_buffer_list_t *dst, const qd_buffer_list_t *src);

/**
 * Free all the buffers contained in a buffer list
 *
 * @param list A pointer to a list containing buffers.  On return this list
 * will be set to an empty list.
 */
void qd_buffer_list_free_buffers(qd_buffer_list_t *list);

/**
 * Return the total number of data bytes in a buffer list
 *
 * @param list A pointer to a list containing buffers.
 * @return total number of bytes of data in the buffer list
 */
unsigned int qd_buffer_list_length(const qd_buffer_list_t *list);

/**
 * Set the fanout value on the buffer.
 * @return the _old_ count before updating
 */
static inline uint32_t qd_buffer_set_fanout(qd_buffer_t *buf, uint32_t value)
{
    return sys_atomic_set(&buf->bfanout, value);
}

/**
 * Increase the fanout by 1. How many receivers should this buffer be sent to.
 * @return the _old_ count (pre increment)
 */
static inline uint32_t qd_buffer_inc_fanout(qd_buffer_t *buf)
{
    return sys_atomic_inc(&buf->bfanout);
}

/**
 * Decrease the fanout by one
 * @return the _old_ count (pre decrement)
 */
static inline uint32_t qd_buffer_dec_fanout(qd_buffer_t *buf)
{
    return sys_atomic_dec(&buf->bfanout);
}

/**
 * Advance the buffer by len. Does not manipulate the contents of the buffer
 * @param buf A pointer to an allocated buffer
 * @param len The number of octets that by which the buffer should be advanced.
 */
static inline unsigned char *qd_buffer_at(const qd_buffer_t *buf, size_t len)
{
    assert(len <= BUFFER_SIZE);
    return ((unsigned char*) &buf[1]) + len;
}


//
// API for handling data that spans one or more buffers
//

/**
 * Represents a span of data within a buffer chain.
 * The buffer and cursor fields are guaranteed to be !null if length > 0.
 */
typedef struct qd_buffered_data_t {
    qd_buffer_t   *buffer;  // head of buffer chain
    unsigned char *cursor;  // start of first byte of data in 'buffer'
    size_t         length;  // number of bytes of data
} qd_buffered_data_t;


/**
 * Allocate from the pool
 * @param head Start of buffered data
 * @param offset To start of data from qd_buffer_base(head)
 * @param length Number of bytes in field
 */
qd_buffered_data_t *qd_buffered_data(qd_buffer_t *head, size_t offset, size_t length);

/**
 * Free an allocated qd_buffer_data_t
 * @param bdata A pointer to the qd_buffered_data_t to free.  Note that the
 * underlying buffer chain is not freed.
 */
void qd_buffered_data_free(qd_buffered_data_t *bdata);

/**
 * Copy n bytes of data from start of buffer chain (cursor) into buffer.
 * Advance the cursor past the last byte copied.
 * @param bdata Buffered data
 * @param buffer Destination for copied bytes
 * @param n Number of bytes to copy
 * @return Number of bytes actually copied, which may be < n if length of
 * buffered data is shorter than n.
 */
static inline size_t qd_buffered_data_copy(qd_buffered_data_t *bdata, unsigned char *buffer, size_t n)
{}

/**
 * Compare the first n bytes in the buffered data to contents of buffer.
 * Advance the cursor past the last byte copied.  If matched advance the cursor past the nth byte.
 * @param bdata Buffered data
 * @param buffer Data to compare
 * @param n Number of bytes to compare
 * @return true if match.
 */
static inline bool qd_buffered_data_equal(qd_buffered_data_t *bdata, const unsigned char *buffer, size_t n)
{}

/**
 * Move the cursor n bytes forward (e.g. skip the first n bytes).
 * @param bdata Buffered data
 * @param n Number of bytes to skip
 * @return Actual number of bytes skipped.  May be < n if buffered data is shorter than n.
 * buffered data is shorter than n.
 */
static inline bool qd_buffered_data_equal(qd_buffered_data_t *bdata, const unsigned char *buffer, size_t n)
{}


///@}

#endif
