#ifndef __dispatch_buffer_field_api_h__
#define __dispatch_buffer_field_api_h__ 1
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
 * Inline API for common operations on buffer_fields
 * @internal
 * @{
 */

#include "qpid/dispatch/buffer_field.h"
#include "qpid/dispatch/iterator.h"

/* qd_buffer_field_ncopy
 *
 * Copy up to n octets from bfield to dest, advance bfield by the number of
 * octets copied
 *
 * @return total of octets copied - may be < n if len(bfield) < n
 */
static inline size_t qd_buffer_field_ncopy(qd_buffer_field_t *bfield, uint8_t *dest, size_t n)
{
    assert(bfield);

    const uint8_t *start = dest;
    size_t count = MIN(n, bfield->remaining);

    while (count) {
        size_t avail = qd_buffer_cursor(bfield->buffer) - bfield->cursor;
        if (count < avail) {
            // fastpath: no need to adjust buffer pointers
            memcpy(dest, bfield->cursor, count);
            dest += count;
            bfield->cursor += count;
            bfield->remaining -= count;
            return dest - start;
        }

        memcpy(dest, bfield->cursor, avail);
        dest += avail;
        count -= avail;

        // count is >= what is available in the current buffer, move to next

        bfield->remaining -= avail;
        if (bfield->remaining) {
            do {
                bfield->buffer = DEQ_NEXT(bfield->buffer);
                assert(bfield->buffer);
                bfield->cursor = qd_buffer_base(bfield->buffer);
                // allow for the possibility of an empty next buffer
            } while (bfield->cursor >= qd_buffer_cursor(bfield->buffer));
        }
    }

    return dest - start;
}


/* qd_buffer_field_advance
 *
 * Move the cursor of bfield forward by amount octets.
 *
 * @return total of octets skipped - may be < amount if len(bfield) < amount
 */
static inline size_t qd_buffer_field_advance(qd_buffer_field_t *bfield, size_t amount)
{
    assert(bfield);

    size_t blen = bfield->remaining;
    size_t count = MIN(amount, blen);

    while (count > 0) {
        size_t avail = qd_buffer_cursor(bfield->buffer) - bfield->cursor;

        if (count < avail) {
            // fastpath: no need to adjust buffer pointers
            bfield->cursor += count;
            bfield->remaining -= count;
            break;
        }

        count -= avail;
        bfield->remaining -= avail;

        // count is >= what is available in the current buffer, move to next

        if (bfield->remaining) {
            do {
                bfield->buffer = DEQ_NEXT(bfield->buffer);
                assert(bfield->buffer);
                bfield->cursor = qd_buffer_base(bfield->buffer);
            } while (bfield->cursor >= qd_buffer_cursor(bfield->buffer));
        }
    }

    return blen - bfield->remaining;
}


/* qd_buffer_field_octet
 *
 * Get the first octet of the field and move the cursor to the next octet (if
 * present).  bfield length is decremented by 1
 *
 * @return true of octet read, false if no octet available (end of field).
 */
static inline bool qd_buffer_field_octet(qd_buffer_field_t *bfield, uint8_t *octet)
{
    assert(bfield);

    if (bfield->remaining) {

        // adjust the cursor if it is at the end of the current buffer
        while (bfield->cursor >= qd_buffer_cursor(bfield->buffer)) {
            bfield->buffer = DEQ_NEXT(bfield->buffer);
            assert(bfield->buffer);
            bfield->cursor = qd_buffer_base(bfield->buffer);
        }

        *octet = *bfield->cursor++;
        bfield->remaining -= 1;

        return true;
    }
    return false;
}


/* qd_buffer_field_uint32
 *
 * Get the next 4 octets of the field and convert them to a uint32 value.  Move
 * the cursor past the 4 octets and decrement the length by 4. uint32 values
 * are used extensively in the AMQP type encodings for meta data (size and
 * count).
 *
 * @return true of uint32 read, false if not enough octets available (end of field).
 */
static inline bool qd_buffer_field_uint32(qd_buffer_field_t *bfield, uint32_t *value)
{
    assert(bfield);

    if (bfield->remaining >= 4) {
        uint8_t buf[4];
        qd_buffer_field_ncopy(bfield, buf, 4);
        *value = (((uint32_t) buf[0]) << 24)
            | (((uint32_t) buf[1]) << 16)
            | (((uint32_t) buf[2]) << 8)
            | ((uint32_t) buf[3]);
        return true;
    }
    return false;
}


/* qd_buffer_field_strdup
 *
 * Return a null terminated string containing the bfield data.  Caller assumes
 * responsibility for calling free() on the returned value when finished with
 * it.  Caller also should ensure the data is actually a value that can be
 * rendered as a C string (e.g. no internal zero values).
 *
 * @return null terminated C string, must be free()ed by caller.
 */
static inline char *qd_buffer_field_strdup(qd_buffer_field_t *bfield)
{
    assert(bfield);

    const size_t len = bfield->remaining + 1;
    char *str = qd_malloc(len);
    qd_buffer_field_ncopy(bfield, (uint8_t*) str, bfield->remaining);
    str[len - 1] = 0;
    return str;
}


/* qd_buffer_field_equal
 *
 * Check if the field is exactly equal to count octets of data. If equal
 * advance the bfield count octets.
 *
 * @return true if equal
 */
static inline bool qd_buffer_field_equal(qd_buffer_field_t *bfield, const uint8_t *data, size_t count)
{
    assert(bfield);

    if (bfield->remaining < count)
        return false;

    const qd_buffer_field_t save = *bfield;

    while (count) {

        size_t avail = qd_buffer_cursor(bfield->buffer) - bfield->cursor;

        if (count < avail) {
            // fastpath: no need to adjust buffer pointers
            if (memcmp(data, bfield->cursor, count) != 0) {
                *bfield = save;
                return false;
            }
            bfield->cursor += count;
            bfield->remaining -= count;
            return true;
        }

        if (memcmp(data, bfield->cursor, avail) != 0) {
            *bfield = save;
            return false;
        }

        data += avail;
        count -= avail;
        bfield->remaining -= avail;

        // count is >= what is available in the current buffer, move to next

        if (bfield->remaining) {
            do {
                bfield->buffer = DEQ_NEXT(bfield->buffer);
                assert(bfield->buffer);
                bfield->cursor = qd_buffer_base(bfield->buffer);
                // allow for the possibility of an empty next buffer
            } while (bfield->cursor >= qd_buffer_cursor(bfield->buffer));
        }
    }

    return true;
}


/* qd_buffer_list_append_field
 *
 * Copy the contents of bfield to the end of the buflist buffer chain. This
 * copies all data - no bfield buffers are moved to buflist. bfield is advanced
 * to the end of data.
 *
 * @return void
 */

static inline void qd_buffer_list_append_field(qd_buffer_list_t *buflist, qd_buffer_field_t *bfield)
{
    assert(buflist);
    assert(bfield);

    while (bfield->remaining) {
        size_t avail = qd_buffer_cursor(bfield->buffer) - bfield->cursor;
        size_t len = MIN(bfield->remaining, avail);

        qd_buffer_list_append(buflist, bfield->cursor, len);
        bfield->remaining -= len;
        if (!bfield->remaining) {
            bfield->cursor += len;
        } else {
            bfield->buffer = DEQ_NEXT(bfield->buffer);
            assert(bfield->buffer);
            bfield->cursor = qd_buffer_base(bfield->buffer);
        }
    }
}


/* qd_buffer_field_iterator
 *
 * Create an iterator over the contents of the bfield.
 *
 * @return an iterator with view
 */
static inline qd_iterator_t *qd_buffer_field_iterator(const qd_buffer_field_t *bfield,
                                                      qd_iterator_view_t view)
{
    assert(bfield);
    qd_buffer_t *buffer = bfield->buffer;
    const uint8_t *cursor = bfield->cursor;

    // qd_iterator_buffer() expects the cursor to point to the next available
    // octet if there is data. IOW: passing an offset past the current buffer
    // is incorrect behavior.
    while (bfield->remaining && cursor >= qd_buffer_cursor(buffer)) {
        buffer = DEQ_NEXT(buffer);
        cursor = qd_buffer_base(buffer);
    }

    return qd_iterator_buffer(buffer,
                              cursor - qd_buffer_base(buffer),
                              bfield->remaining,
                              view);
}
///@}

#endif
