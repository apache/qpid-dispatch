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

#include "qpid/dispatch/compose.h"

#include "compose_private.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/buffer.h"
#include "qpid/dispatch/ctools.h"

#include <string.h>

ALLOC_DEFINE(qd_composite_t);
ALLOC_DEFINE(qd_composed_field_t);


static inline void bump_count(qd_composed_field_t *field)
{
    qd_composite_t *comp = DEQ_HEAD(field->fieldStack);
    if (comp)
        comp->count++;
}


static inline void bump_count_by_n(qd_composed_field_t * field, uint32_t n)
{
    qd_composite_t *comp = DEQ_HEAD(field->fieldStack);
    if (comp)
        comp->count += n;
}


static inline void bump_length(qd_composed_field_t *field,
                        uint32_t length)
{
    qd_composite_t *comp = DEQ_HEAD(field->fieldStack);
    if (comp)
        comp->length += length;
}


static inline void qd_insert(qd_composed_field_t *field, const uint8_t *seq, size_t len)
{
    qd_buffer_t    *buf  = DEQ_TAIL(field->buffers);
    qd_composite_t *comp = DEQ_HEAD(field->fieldStack);

    while (len > 0) {
        if (buf == 0 || qd_buffer_capacity(buf) == 0) {
            buf = qd_buffer();
            if (buf == 0)
                return;
            DEQ_INSERT_TAIL(field->buffers, buf);
        }

        size_t to_copy = qd_buffer_capacity(buf);
        if (to_copy > len)
            to_copy = len;
        memcpy(qd_buffer_cursor(buf), seq, to_copy);
        qd_buffer_insert(buf, to_copy);
        len -= to_copy;
        seq += to_copy;
        if (comp)
            comp->length += to_copy;
    }
}


static inline void qd_insert_8(qd_composed_field_t *field, uint8_t value)
{
    qd_insert(field, &value, 1);
}


static inline void qd_insert_32(qd_composed_field_t *field, uint32_t value)
{
    uint8_t buf[4];
    buf[0] = (uint8_t) ((value & 0xFF000000) >> 24);
    buf[1] = (uint8_t) ((value & 0x00FF0000) >> 16);
    buf[2] = (uint8_t) ((value & 0x0000FF00) >> 8);
    buf[3] = (uint8_t)  (value & 0x000000FF);
    qd_insert(field, buf, 4);
}


static inline void qd_insert_64(qd_composed_field_t *field, uint64_t value)
{
    uint8_t buf[8];
    buf[0] = (uint8_t) ((value & 0xFF00000000000000L) >> 56);
    buf[1] = (uint8_t) ((value & 0x00FF000000000000L) >> 48);
    buf[2] = (uint8_t) ((value & 0x0000FF0000000000L) >> 40);
    buf[3] = (uint8_t) ((value & 0x000000FF00000000L) >> 32);
    buf[4] = (uint8_t) ((value & 0x00000000FF000000L) >> 24);
    buf[5] = (uint8_t) ((value & 0x0000000000FF0000L) >> 16);
    buf[6] = (uint8_t) ((value & 0x000000000000FF00L) >> 8);
    buf[7] = (uint8_t)  (value & 0x00000000000000FFL);
    qd_insert(field, buf, 8);
}


static inline void qd_overwrite(qd_buffer_t **buf, size_t *cursor, uint8_t value)
{
    while (*buf) {
        if (*cursor >= qd_buffer_size(*buf)) {
            *buf = (*buf)->next;
            *cursor = 0;
        } else {
            qd_buffer_base(*buf)[*cursor] = value;
            (*cursor)++;
            return;
        }
    }
}


static inline void qd_overwrite_32(qd_field_location_t *field, uint32_t value)
{
    qd_buffer_t *buf    = field->buffer;
    size_t       cursor = field->offset;

    qd_overwrite(&buf, &cursor, (uint8_t) ((value & 0xFF000000) >> 24));
    qd_overwrite(&buf, &cursor, (uint8_t) ((value & 0x00FF0000) >> 16));
    qd_overwrite(&buf, &cursor, (uint8_t) ((value & 0x0000FF00) >> 8));
    qd_overwrite(&buf, &cursor, (uint8_t)  (value & 0x000000FF));
}


static inline void qd_compose_start_composite(qd_composed_field_t *field, int isMap)
{
    if (isMap)
        qd_insert_8(field, QD_AMQP_MAP32);
    else
        qd_insert_8(field, QD_AMQP_LIST32);

    //
    // Push a composite descriptor on the field stack
    //
    qd_composite_t *comp = new_qd_composite_t();
    DEQ_ITEM_INIT(comp);
    comp->isMap = isMap;

    //
    // Mark the current location to later overwrite the length
    //
    comp->length_location.buffer = DEQ_TAIL(field->buffers);
    comp->length_location.offset = qd_buffer_size(comp->length_location.buffer);
    comp->length_location.length = 4;
    comp->length_location.parsed = 1;

    qd_insert(field, (const uint8_t*) "\x00\x00\x00\x00", 4);

    //
    // Mark the current location to later overwrite the count
    //
    comp->count_location.buffer = DEQ_TAIL(field->buffers);
    comp->count_location.offset = qd_buffer_size(comp->count_location.buffer);
    comp->count_location.length = 4;
    comp->count_location.parsed = 1;

    qd_insert(field, (const uint8_t*) "\x00\x00\x00\x00", 4);

    comp->length = 4; // Include the length of the count field
    comp->count = 0;

    DEQ_INSERT_HEAD(field->fieldStack, comp);
}


static inline void qd_compose_end_composite(qd_composed_field_t *field)
{
    qd_composite_t *comp = DEQ_HEAD(field->fieldStack);
    assert(comp);

    qd_overwrite_32(&comp->length_location, comp->length);
    qd_overwrite_32(&comp->count_location,  comp->count);

    DEQ_REMOVE_HEAD(field->fieldStack);

    //
    // If there is an enclosing composite, update its length and count
    //
    qd_composite_t *enclosing = DEQ_HEAD(field->fieldStack);
    if (enclosing) {
        enclosing->length += (comp->length - 4); // the length and count were already accounted for
        enclosing->count++;
    }

    free_qd_composite_t(comp);
}


qd_composed_field_t *qd_compose_subfield(qd_composed_field_t *extend)
{
    qd_composed_field_t *field = extend;

    if (field) {
        assert(DEQ_SIZE(field->fieldStack) == 0);
    } else {
        field = new_qd_composed_field_t();
        if (!field)
            return 0;

        DEQ_INIT(field->buffers);
        DEQ_INIT(field->fieldStack);
    }

    return field;
}


qd_composed_field_t *qd_compose(uint64_t performative, qd_composed_field_t *extend)
{
    qd_composed_field_t *field = qd_compose_subfield(extend);

    if (field) {
        qd_insert_8(field, 0x00);
        qd_compose_insert_ulong(field, performative);
    }

    return field;
}


void qd_compose_free(qd_composed_field_t *field)
{
    if (!field) return;
    qd_buffer_t *buf = DEQ_HEAD(field->buffers);
    while (buf) {
        DEQ_REMOVE_HEAD(field->buffers);
        qd_buffer_free(buf);
        buf = DEQ_HEAD(field->buffers);
    }

    qd_composite_t *comp = DEQ_HEAD(field->fieldStack);
    while (comp) {
        DEQ_REMOVE_HEAD(field->fieldStack);
        free_qd_composite_t(comp);
        comp = DEQ_HEAD(field->fieldStack);
    }

    free_qd_composed_field_t(field);
}


void qd_compose_start_list(qd_composed_field_t *field)
{
    qd_compose_start_composite(field, 0);
}


void qd_compose_end_list(qd_composed_field_t *field)
{
    qd_compose_end_composite(field);
}


void qd_compose_empty_list(qd_composed_field_t *field)
{
    qd_insert_8(field, QD_AMQP_LIST0);
    bump_count(field);
}


void qd_compose_start_map(qd_composed_field_t *field)
{
    qd_compose_start_composite(field, 1);
}


void qd_compose_end_map(qd_composed_field_t *field)
{
    qd_compose_end_composite(field);
}


void qd_compose_insert_null(qd_composed_field_t *field)
{
    qd_insert_8(field, QD_AMQP_NULL);
    bump_count(field);
}


void qd_compose_insert_bool(qd_composed_field_t *field, int value)
{
    qd_insert_8(field, value ? QD_AMQP_TRUE : QD_AMQP_FALSE);
    bump_count(field);
}


void qd_compose_insert_uint(qd_composed_field_t *field, uint32_t value)
{
    if (value == 0) {
        qd_insert_8(field, QD_AMQP_UINT0);
    } else if (value < 256) {
        qd_insert_8(field, QD_AMQP_SMALLUINT);
        qd_insert_8(field, (uint8_t) value);
    } else {
        qd_insert_8(field, QD_AMQP_UINT);
        qd_insert_32(field, value);
    }
    bump_count(field);
}


void qd_compose_insert_ulong(qd_composed_field_t *field, uint64_t value)
{
    if (value == 0) {
        qd_insert_8(field, QD_AMQP_ULONG0);
    } else if (value < 256) {
        qd_insert_8(field, QD_AMQP_SMALLULONG);
        qd_insert_8(field, (uint8_t) value);
    } else {
        qd_insert_8(field, QD_AMQP_ULONG);
        qd_insert_64(field, value);
    }
    bump_count(field);
}


void qd_compose_insert_int(qd_composed_field_t *field, int32_t value)
{
    if (value >= -128 && value <= 127) {
        qd_insert_8(field, QD_AMQP_SMALLINT);
        qd_insert_8(field, (uint8_t) value);
    } else {
        qd_insert_8(field, QD_AMQP_INT);
        qd_insert_32(field, (uint32_t) value);
    }
    bump_count(field);
}


void qd_compose_insert_long(qd_composed_field_t *field, int64_t value)
{
    if (value >= -128 && value <= 127) {
        qd_insert_8(field, QD_AMQP_SMALLLONG);
        qd_insert_8(field, (uint8_t) value);
    } else {
        qd_insert_8(field, QD_AMQP_LONG);
        qd_insert_64(field, (uint64_t) value);
    }
    bump_count(field);
}


void qd_compose_insert_timestamp(qd_composed_field_t *field, uint64_t value)
{
    qd_insert_8(field, QD_AMQP_TIMESTAMP);
    qd_insert_64(field, value);
    bump_count(field);
}


void qd_compose_insert_uuid(qd_composed_field_t *field, const uint8_t *value)
{
    qd_insert_8(field, QD_AMQP_UUID);
    qd_insert(field, value, 16);
    bump_count(field);
}


void qd_compose_insert_binary(qd_composed_field_t *field, const uint8_t *value, uint32_t len)
{
    if (len < 256) {
        qd_insert_8(field, QD_AMQP_VBIN8);
        qd_insert_8(field, (uint8_t) len);
    } else {
        qd_insert_8(field, QD_AMQP_VBIN32);
        qd_insert_32(field, len);
    }
    qd_insert(field, value, len);
    bump_count(field);
}


void qd_compose_insert_binary_buffers(qd_composed_field_t *field, qd_buffer_list_t *buffers)
{
    qd_buffer_t *buf = DEQ_HEAD(*buffers);
    uint32_t     len = 0;

    //
    // Calculate the size of the binary field to be appended.
    //
    while (buf) {
        len += qd_buffer_size(buf);
        buf = DEQ_NEXT(buf);
    }

    //
    // Supply the appropriate binary tag for the length.
    //
    if (len < 256) {
        qd_insert_8(field, QD_AMQP_VBIN8);
        qd_insert_8(field, (uint8_t) len);
    } else {
        qd_insert_8(field, QD_AMQP_VBIN32);
        qd_insert_32(field, len);
    }

    //
    // Move the supplied buffers to the tail of the field's buffer list.
    //
    buf = DEQ_HEAD(*buffers);
    while (buf) {
        DEQ_REMOVE_HEAD(*buffers);
        DEQ_INSERT_TAIL(field->buffers, buf);
        buf = DEQ_HEAD(*buffers);
    }
    bump_length(field, len);
    bump_count(field);
}


void qd_compose_insert_string_n(qd_composed_field_t *field, const char *value, size_t len)
{
    if (len < 256) {
        qd_insert_8(field, QD_AMQP_STR8_UTF8);
        qd_insert_8(field, (uint8_t) len);
    } else {
        qd_insert_8(field, QD_AMQP_STR32_UTF8);
        qd_insert_32(field, len);
    }
    qd_insert(field, (const uint8_t*) value, len);
    bump_count(field);
}


void qd_compose_insert_string(qd_composed_field_t *field, const char *value)
{
    if (value)
        qd_compose_insert_string_n(field, value, strlen(value));
    else
        qd_compose_insert_string_n(field, value, 0);
}


void qd_compose_insert_string2(qd_composed_field_t *field, const char *value1, const char *value2)
{
    uint32_t len1 = strlen(value1);
    uint32_t len2 = strlen(value2);
    uint32_t len  = len1 + len2;

    if (len < 256) {
        qd_insert_8(field, QD_AMQP_STR8_UTF8);
        qd_insert_8(field, (uint8_t) len);
    } else {
        qd_insert_8(field, QD_AMQP_STR32_UTF8);
        qd_insert_32(field, len);
    }
    qd_insert(field, (const uint8_t*) value1, len1);
    qd_insert(field, (const uint8_t*) value2, len2);
    bump_count(field);
}


void qd_compose_insert_string_iterator(qd_composed_field_t *field, qd_iterator_t *iter)
{
    qd_iterator_reset(iter);

    char *as_str = (char *)qd_iterator_copy(iter);
    const uint32_t len = as_str ? strlen(as_str) : 0;
    qd_compose_insert_string_n(field, as_str, len);
    free(as_str);
}


void qd_compose_insert_symbol(qd_composed_field_t *field, const char *value)
{
    uint32_t len = 0;
    if (value)
        len = strlen(value);

    if (len < 256) {
        qd_insert_8(field, QD_AMQP_SYM8);
        qd_insert_8(field, (uint8_t) len);
    } else {
        qd_insert_8(field, QD_AMQP_SYM32);
        qd_insert_32(field, len);
    }
    qd_insert(field, (const uint8_t*) value, len);
    bump_count(field);
}


void qd_compose_insert_typed_iterator(qd_composed_field_t *field, qd_iterator_t *iter)
{
    while (!qd_iterator_end(iter)) {
        uint8_t octet = qd_iterator_octet(iter);
        qd_insert_8(field, octet);
    }

    bump_count(field);
}


qd_buffer_list_t *qd_compose_buffers(qd_composed_field_t *field)
{
    return &field->buffers;
}


void qd_compose_take_buffers(qd_composed_field_t *field,
                             qd_buffer_list_t *list)
{
    // assumption: extracting partially built containers is wrong:
    assert(DEQ_SIZE(field->fieldStack) == 0);
    *list = *qd_compose_buffers(field);
    DEQ_INIT(field->buffers); // Zero out the linkage to the now moved buffers.
}


void qd_compose_insert_buffers(qd_composed_field_t *field,
                               qd_buffer_list_t *list)
{
    uint32_t len = qd_buffer_list_length(list);
    if (len) {
        DEQ_APPEND(field->buffers, *list);
        bump_length(field, len);
        bump_count(field);
    }
}


void qd_compose_insert_opaque_elements(qd_composed_field_t *field,
                                       uint32_t             count,
                                       uint32_t             size)
{
    bump_count_by_n(field, count);
    bump_length(field, size);
}


void qd_compose_insert_double(qd_composed_field_t *field, double value)
{
    union {
        uint64_t l;
        double d;
    } converter;
    converter.d = value;

    qd_insert_8(field, QD_AMQP_DOUBLE);
    qd_insert_64(field, converter.l);
    bump_count(field);
}
