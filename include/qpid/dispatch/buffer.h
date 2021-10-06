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

#include "qpid/dispatch/atomic.h"
#include "qpid/dispatch/ctools.h"

typedef struct qd_buffer_t qd_buffer_t;

DEQ_DECLARE(qd_buffer_t, qd_buffer_list_t);

extern size_t BUFFER_SIZE;

/** A raw byte buffer .*/
struct qd_buffer_t {
    DEQ_LINKS(qd_buffer_t);
    unsigned int size;     ///< Size of data content
    sys_atomic_t bfanout;  ///< The number of receivers for this buffer
};

/**
 * Set the initial buffer capacity to be allocated by future calls to qp_buffer.
 *
 * NOTICE:  This function is provided for testing purposes only.  It should not be invoked
 * in the production code.  If this function is called after the first buffer has been allocated,
 * the software WILL BE unstable and WILL crash.
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
 * Get the fanout value on the buffer.
 * @return the count
 */
static inline uint32_t qd_buffer_get_fanout(const qd_buffer_t *buf)
{
    return buf->bfanout;
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


/**
 * qd_buffer_list_append
 *
 * Append new data to a buffer list using freespace efficiently and adding new buffers when necessary.
 *
 * @param buflist Pointer to a buffer list that will possibly be changed by adding new buffers
 * @param data Pointer to raw binary data to be added to the buffer list
 * @param len The number of bytes of data to append
 */
void qd_buffer_list_append(qd_buffer_list_t *buflist, const uint8_t *data, size_t len);


#include <stdbool.h>

///@}
/* descriptor for a sequence of bytes in a buffer list
 */
typedef struct qd_buffer_field_t qd_buffer_field_t;
struct qd_buffer_field_t {
    qd_buffer_t   *head;
    const uint8_t *cursor;
    size_t         length;
};



typedef struct qd_amqp_field_t qd_amqp_field_t;
struct qd_amqp_field_t {
    uint8_t tag;
    uint32_t size;
    uint32_t count;
    qd_buffer_field_t data;
};



// copy up to n octets to dest, advance bfield by the number of octets copied
static inline size_t qd_buffer_field_memcpy(qd_buffer_field_t *bfield, uint8_t *dest, size_t n)
{
    const uint8_t *start = dest;
    size_t count = MIN(n, bfield->length);
    while (count > 0) {
        assert(bfield->cursor <= qd_buffer_cursor(bfield->head));
        size_t avail = qd_buffer_cursor(bfield->head) - bfield->cursor;

        if (count <= avail) {
            memcpy(dest, bfield->cursor, count);
            dest += count;
            bfield->cursor += count;
            bfield->length -= count;
            break;
        }

        // count is > what is available in the current buffer, move to next
        memcpy(dest, bfield->cursor, avail);
        dest += avail;
        count -= avail;
        assert(DEQ_NEXT(bfield->head));
        bfield->head = DEQ_NEXT(bfield->head);
        bfield->cursor = (const uint8_t *)qd_buffer_base(bfield->head);
        bfield->length -= avail;
    }
    return dest - start;
}

static inline size_t qd_buffer_field_advance(qd_buffer_field_t *bfield, size_t amount)
{
    size_t blen = bfield->length;
    size_t count = MIN(amount, blen);
    while (count > 0) {
        size_t avail = qd_buffer_cursor(bfield->head) - bfield->cursor;

        if (count <= avail) {
            bfield->cursor += count;
            bfield->length -= count;
            break;
        }

        // count is > what is available in the current buffer, move to next
        count -= avail;
        bfield->length -= avail;
        assert(DEQ_NEXT(bfield->head));
        bfield->head = DEQ_NEXT(bfield->head);
        bfield->cursor = qd_buffer_base(bfield->head);
    }

    return blen - bfield->length;
}


static inline bool qd_buffer_field_octet(qd_buffer_field_t *bfield, uint8_t *octet)
{
    if (bfield->length) {
        bfield->length -= 1;
        while (bfield->cursor == qd_buffer_cursor(bfield->head)) {
            bfield->head = DEQ_NEXT(bfield->head);
            assert(bfield->head);
            bfield->cursor = qd_buffer_base(bfield->head);
        }
        *octet = *bfield->cursor++;
        return true;
    }
    return false;
}


/* Parse out the AMQP data type pointed to by bfield, advance bfield past the data type.
 * Return the total number of octets consumed, zero if unable to parse a valid AMQP field
 */
static inline size_t qd_buffer_field_get_amqp_data(qd_buffer_field_t *bfield, qd_amqp_field_t *afield)
{
    size_t size_len = 0;
    int count_len = 0;
    const qd_buffer_field_t save = *bfield;
    uint8_t buf[8];
    uint8_t octet;

    ZERO(afield);

    if (qd_buffer_field_octet(bfield, &afield->tag)) {

        switch (afield->tag & 0xF0) {

            // size encoded in tag
        case 0x40:
            afield->size = 0;
            break;
        case 0x50:
            afield->size = 1;
            break;
        case 0x60:
            afield->size = 2;
            break;
        case 0x70:
            afield->size = 4;
            break;
        case 0x80:
            afield->size = 8;
            break;
        case 0x90:
            afield->size = 16;
            break;

        case 0xA0:
            // one octet size
            size_len = 1;
            if (!qd_buffer_field_octet(bfield, &octet)) {
                *bfield = save;
                return 0;
            }
            afield->size = octet;
            break;

        case 0xB0:
            // 4 byte size (uint32_t)
            size_len = 4;
            if (4 != qd_buffer_field_memcpy(bfield, buf, 4)) {
                *bfield = save;
                return 0;
            }
            afield->size = (((uint32_t) buf[0]) << 24)
                | (((uint32_t) buf[1]) << 16)
                | (((uint32_t) buf[2]) << 8)
                | ((uint32_t) buf[3]);
            break;

        case 0xC0:
        case 0xE0:
            size_len = 1;
            count_len = 1;
            // one octet size and count
            if (2 != qd_buffer_field_memcpy(bfield, buf, 2)) {
                *bfield = save;
                return 0;
            }
            afield->size = buf[0];
            afield->count = buf[1];
            break;

        case 0xD0:
        case 0xF0:
            // 4 octet size and count
            size_len = 4;
            count_len = 4;
            if (8 != qd_buffer_field_memcpy(bfield, buf, 8)) {
                *bfield = save;
                return 0;
            }
            afield->size = (((uint32_t) buf[0]) << 24)
                | (((uint32_t) buf[1]) << 16)
                | (((uint32_t) buf[2]) << 8)
                | ((uint32_t) buf[3]);
            afield->count = (((uint32_t) buf[4]) << 24)
                | (((uint32_t) buf[5]) << 16)
                | (((uint32_t) buf[6]) << 8)
                | ((uint32_t) buf[7]);
            break;

        default:
            *bfield = save;
            return 0;
        }

        // bfield should now be pointing at the first byte of the field data
        afield->data = *bfield;
        afield->data.length = afield->size - count_len;
        if (afield->data.length != qd_buffer_field_advance(bfield, afield->data.length))
            return 0;

        return 1 + size_len + afield->size;  // afield->size includes count_len
    }
    return 0;
}


static inline bool qd_buffer_field_equal(qd_buffer_field_t *bfield, const uint8_t *data, size_t count)
{
    if (bfield->length < count)
        return false;

    const qd_buffer_field_t save = *bfield;

    while (count) {

        size_t avail = qd_buffer_cursor(bfield->head) - bfield->cursor;
        // optimized: early exit when no need to update iterators buffer pointers
        if (count <= avail) {
            if (memcmp(data, bfield->cursor, count) != 0) {
                *bfield = save;
                return false;
            }
            bfield->cursor += count;
            bfield->length -= count;
            return true;
        }

        // count is >= what is available in the current buffer
        if (memcmp(data, bfield->cursor, avail) != 0) {
            *bfield = save;
            return false;
        }

        data += avail;
        count -= avail;
        bfield->length -= avail;

        assert(DEQ_NEXT(bfield->head));
        bfield->head = DEQ_NEXT(bfield->head);
        bfield->cursor = qd_buffer_base(bfield->head);
    } while (count);

    return true;
}


static inline void qd_buffer_list_append_field(qd_buffer_list_t *buflist, qd_buffer_field_t *bfield)
{
    while (bfield->length) {
        size_t avail = qd_buffer_cursor(bfield->head) - bfield->cursor;
        size_t len = MIN(bfield->length, avail);

        qd_buffer_list_append(buflist, bfield->cursor, len);
        bfield->length -= len;
        if (bfield->length) {
            bfield->head = DEQ_NEXT(bfield->head);
            assert(bfield->head);
            bfield->cursor = qd_buffer_base(bfield->head);
        } else {
            bfield->cursor = qd_buffer_cursor(bfield->head);
        }
    }
}

#endif
