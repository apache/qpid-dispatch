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

#include <stdbool.h>

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
    size_t         length;  // total number of bytes of data in chain
} qd_buffered_data_t;


/**
 * Allocate a buffered data instance from the pool
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
{ return 0; }

/**
 * Compare the first n bytes in the buffered data to contents of buffer.
 * Advance the cursor past the last byte copied.  If matched advance the cursor past the nth byte.
 * @param bdata Buffered data
 * @param buffer Data to compare
 * @param n Number of bytes to compare
 * @return true if match.
 */
static inline bool qd_buffered_data_equal(qd_buffered_data_t *bdata, const unsigned char *buffer, size_t n)
{ return true; }

/**
 * Move the cursor n bytes forward (e.g. skip the first n bytes).
 * @param bdata Buffered data
 * @param n Number of bytes to skip
 * @return Actual number of bytes skipped.  May be < n if buffered data is shorter than n.
 */
static inline bool qd_buffered_data_advance(qd_buffered_data_t *bdata, const unsigned char *buffer, size_t n)
{ return true; }


/**
 * Extract the unsigned byte at the current cursor, and advance to the next byte
 * @param bdata Buffered data
 * @param octet Location to write the unsigned byte
 * @return true if octet set, false if no data available (length == 0)
 */
static inline bool qd_buffered_data_octet(qd_buffered_data_t *bdata, uint8_t *octet)
{ return true; }


/**
 * Extract the unsigned 32 bit integer at the current cursor and advance past it.
 * Converts the result into host byte order.
 * @param bdata Buffered data
 * @param uinteger32 Location to write the integer
 * @return true on success, false if length < 4 octets (cursor is not moved)
 */
static inline bool qd_buffered_data_uint32(qd_buffered_data_t *bdata, uint32_t *uinteger32)
{ return true; }


//////////////////////////////////////
// API for processing buffered data
//////////////////////////////////////


/**
 * Callback provided by the caller for processing data in a buffer chain.
 * @param context Caller provided handle
 * @param data Start of data (contiguous)
 * @param length Number of contiguous bytes starting at data
 * @return The number of bytes processed by the handler:
 * If != to length:
 *    if < 0 a non-recoverable error occurred.  Processing is aborted.
 *    if (> 0 && < length) handler is done and no further callbacks will be made
 */
typedef ssize_t (*qd_buffered_data_handler_t)(void *context,
                                              const unsigned char *data,
                                              size_t length);

/**
 * Pass the data in a buffered list to a processing function.  The handler is
 * called repeatedly passing the contiguous data from each buffer in the buffer
 * chain (up to bdata->length bytes.  bdata is advanced by the number of bytes
 * processed by the handler after each successful call.
 *
 * The handler can signal that it is complete by returning a value that
 * is < length.  This will cause qd_buffered_data_consume to return.
 *
 * If the handler returns an error (value < 0) then bdata is not modified.
 *
 * @param bdata The buffered data
 * @param handler A qd_buffered_data_handler_t function
 * @param context Passed to the handler function
 * @return the total number of bytes processed, or an error code if one is
 * returned by the handler.
 */
static inline ssize_t qd_buffered_data_consume(qd_buffered_data_t *bdata,
                                               qd_buffered_data_handler_t *handler,
                                               void *context)
{ return 0; }


//////////////////////////////////////////
// API for extracting AMQP type encodings
//////////////////////////////////////////


/**
 * Extract the AMQP type descriptor information from a buffered field.
 * Expects bdata to point at the tag octet of the descriptor.
 * On success bdata is advanced past the descriptor
 *
 * @param bdata The buffered data
 * @param tag Set to the AMQP type tag
 * @param size Set to the size of the data in octets (includes length_of_count)
 * @param length_of_size In bytes, set to 1 or 4, depending on the type
 * @param count Set to the number of entries if type is compound, else zero
 * @param length_of_count In bytes (1 or 4) if type is compound
 * @return 0 on success, else a descriptive parse error message
 */
static inline const char *qd_buffered_data_get_type_info(qd_buffered_data_t *bdata,
                                                         uint8_t  *tag,
                                                         uint32_t *size,
                                                         uint32_t *length_of_size,
                                                         uint32_t *count,
                                                         uint32_t *length_of_count)
{ return 0; }


///@}

#endif
