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

#include "qpid/dispatch/parse.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/ctools.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

DEQ_DECLARE(qd_parsed_field_t, qd_parsed_field_list_t);

struct qd_parsed_field_t {
    DEQ_LINKS(qd_parsed_field_t);
    const qd_parsed_field_t *parent;
    qd_parsed_field_list_t   children;
    uint8_t                  tag;
    qd_iterator_t           *raw_iter;
    qd_iterator_t           *typed_iter;
    const char              *parse_error;
};

ALLOC_DECLARE(qd_parsed_field_t);
ALLOC_DEFINE(qd_parsed_field_t);

ALLOC_DECLARE(qd_parsed_turbo_t);
ALLOC_DEFINE(qd_parsed_turbo_t);

qd_parsed_field_t* qd_field_first_child(qd_parsed_field_t *field)
{
    return DEQ_HEAD(field->children);
}

qd_parsed_field_t* qd_field_next_child(qd_parsed_field_t *field)
{
    return DEQ_NEXT(field);
}

/**
 * size = the number of bytes following tag:size (payload, including the count)
 * count = the number of elements. Applies only to compound structures
 */
static char *get_type_info(qd_iterator_t *iter, uint8_t *tag, uint32_t *size, uint32_t *count, uint32_t *length_of_size, uint32_t *length_of_count)
{
    if (qd_iterator_end(iter))
        return "Insufficient Data to Determine Tag";

    *tag             = qd_iterator_octet(iter);
    *count           = 0;
    *size            = 0;
    *length_of_count = 0;
    *length_of_size  = 0;


    switch (*tag & 0xF0) {
    case 0x40:
        *size = 0;
        break;
    case 0x50:
        *size = 1;
        break;
    case 0x60:
        *size = 2;
        break;
    case 0x70:
        *size = 4;
        break;
    case 0x80:
        *size = 8;
        break;
    case 0x90:
        *size = 16;
        break;
    case 0xB0:
    case 0xD0:
    case 0xF0:
        *size += ((unsigned int) qd_iterator_octet(iter)) << 24;
        *size += ((unsigned int) qd_iterator_octet(iter)) << 16;
        *size += ((unsigned int) qd_iterator_octet(iter)) << 8;
        *length_of_size = 3;
        // fall through to the next case

    case 0xA0:
    case 0xC0:
    case 0xE0:
        if (qd_iterator_end(iter))
            return "Insufficient Data to Determine Length";
        *size += (unsigned int) qd_iterator_octet(iter);
        *length_of_size += 1;
        break;

    default:
        return "Invalid Tag - No Length Information";
    }

    switch (*tag & 0xF0) {
    case 0xD0:
    case 0xF0:
        *count += ((unsigned int) qd_iterator_octet(iter)) << 24;
        *count += ((unsigned int) qd_iterator_octet(iter)) << 16;
        *count += ((unsigned int) qd_iterator_octet(iter)) << 8;
        *length_of_count = 3;
        // fall through to the next case

    case 0xC0:
    case 0xE0:
        if (qd_iterator_end(iter))
            return "Insufficient Data to Determine Count";
        *count += (unsigned int) qd_iterator_octet(iter);
        *length_of_count += 1;
        break;
    }

    if ((*tag == QD_AMQP_MAP8 || *tag == QD_AMQP_MAP32) && (*count & 1))
        return "Odd Number of Elements in a Map";

    if (*length_of_count > *size)
        return "Insufficient Length to Determine Count";

    return 0;
}

static qd_parsed_field_t *qd_parse_internal(qd_iterator_t *iter, qd_parsed_field_t *p)
{
    qd_parsed_field_t *field = new_qd_parsed_field_t();
    if (!field)
        return 0;

    DEQ_ITEM_INIT(field);
    DEQ_INIT(field->children);
    field->parent   = p;
    field->raw_iter = 0;
    field->typed_iter = qd_iterator_dup(iter);

    uint32_t size            = 0;
    uint32_t count           = 0;
    uint32_t length_of_count = 0;
    uint32_t length_of_size  = 0;

    field->parse_error = get_type_info(iter, &field->tag, &size, &count, &length_of_size, &length_of_count);

    if (!field->parse_error) {
        qd_iterator_trim_view(field->typed_iter, size + length_of_size + 1); // + 1 accounts for the tag length

        field->raw_iter = qd_iterator_sub(iter, size - length_of_count);

        qd_iterator_advance(iter, size - length_of_count);

        for (uint32_t idx = 0; idx < count; idx++) {
            qd_parsed_field_t *child = qd_parse_internal(field->raw_iter, field);
            DEQ_INSERT_TAIL(field->children, child);
            if (!qd_parse_ok(child)) {
                field->parse_error = child->parse_error;
                break;
            }
        }
    }

    return field;
}


qd_parsed_field_t *qd_parse(qd_iterator_t *iter)
{
    if (!iter)
        return 0;
    return qd_parse_internal(iter, 0);
}


const char *qd_parse_turbo(qd_iterator_t          *iter,
                           qd_parsed_turbo_list_t *annos,
                           uint32_t               *user_entries,
                           uint32_t               *user_bytes)
{
    if (!iter || !annos || !user_entries || !user_bytes)
        return  "missing argument";

    DEQ_INIT(*annos);
    *user_entries = 0;
    *user_bytes = 0;

    // The iter is addressing the message-annotations map.
    // Open the field describing the map's items
    uint8_t  tag             = 0;
    uint32_t size            = 0;
    uint32_t count           = 0;
    uint32_t length_of_count = 0;
    uint32_t length_of_size  = 0;
    const char * parse_error = get_type_info(iter, &tag, &size, &count, &length_of_size, &length_of_count);

    if (parse_error)
        return parse_error;

    if (count == 0)
        return 0;

    int n_allocs = 0;

    // Do skeletal parse of each map element
    for (uint32_t idx = 0; idx < count; idx++) {
        qd_parsed_turbo_t *turbo;
        if (n_allocs < QD_MA_FILTER_LEN * 2) {
            turbo = new_qd_parsed_turbo_t();
            n_allocs++;

        } else {
            // Retire an existing element.
            // If there are this many in the list then this one cannot be a
            // router annotation and must be a user annotation.
            turbo = DEQ_HEAD(*annos);
            *user_entries += 1;
            *user_bytes += sizeof(turbo->tag) + turbo->size + turbo->length_of_size;
            DEQ_REMOVE_HEAD(*annos);
        }
        if (!turbo)
            return "failed to allocate qd_parsed_turbo_t";
        ZERO(turbo);

        // Get the buffer pointers for the map element
        qd_iterator_get_view_cursor(iter, &turbo->bufptr);

        // Get description of the map element
        parse_error = get_type_info(iter, &turbo->tag, &turbo->size, &turbo->count,
                                    &turbo->length_of_size, &turbo->length_of_count);
        if (parse_error) {
            free_qd_parsed_turbo_t(turbo);
            return parse_error;
        }

        // Save parsed element
        DEQ_INSERT_TAIL(*annos, turbo);

        // Advance map iterator to next map element
        qd_iterator_advance(iter, turbo->size - turbo->length_of_count);
    }

    // remove leading annos in the queue if their prefix is not a match and
    // return them as part of the user annotations
    for (int idx=0; idx < n_allocs; idx += 2) {
        qd_parsed_turbo_t *turbo = DEQ_HEAD(*annos);
        assert(turbo);
        if (qd_iterator_prefix_ptr(&turbo->bufptr, turbo->length_of_size + 1, QD_MA_PREFIX))
            break;

        // leading anno is a user annotation map key
        // remove the key and value from the list and accumulate them as user items
        *user_bytes += sizeof(turbo->tag) + turbo->size + turbo->length_of_size;
        DEQ_REMOVE_HEAD(*annos);
        free_qd_parsed_turbo_t(turbo);

        turbo = DEQ_HEAD(*annos);
        assert(turbo);
        *user_bytes += sizeof(turbo->tag) + turbo->size + turbo->length_of_size;
        DEQ_REMOVE_HEAD(*annos);
        free_qd_parsed_turbo_t(turbo);

        *user_entries += 2;
    }
    return parse_error;
}


void qd_parse_free(qd_parsed_field_t *field)
{
    if (!field)
        return;

    assert(field->parent == 0);
    if (field->raw_iter)
        qd_iterator_free(field->raw_iter);

    if (field->typed_iter)
        qd_iterator_free(field->typed_iter);

    qd_parsed_field_t *sub_field = DEQ_HEAD(field->children);
    while (sub_field) {
        qd_parsed_field_t *next = DEQ_NEXT(sub_field);
        DEQ_REMOVE_HEAD(field->children);
        sub_field->parent = 0;
        qd_parse_free(sub_field);
        sub_field = next;
    }

    free_qd_parsed_field_t(field);
}


static qd_parsed_field_t *qd_parse_dup_internal(const qd_parsed_field_t *field, const qd_parsed_field_t *parent)
{
    qd_parsed_field_t *dup = new_qd_parsed_field_t();

    if (dup == 0)
        return 0;

    ZERO(dup);
    dup->parent      = parent;
    dup->tag         = field->tag;
    dup->raw_iter    = qd_iterator_dup(field->raw_iter);
    dup->typed_iter  = qd_iterator_dup(field->typed_iter);
    dup->parse_error = field->parse_error;

    qd_parsed_field_t *child = DEQ_HEAD(field->children);
    while (child) {
        qd_parsed_field_t *dup_child = qd_parse_dup_internal(child, field);
        DEQ_INSERT_TAIL(dup->children, dup_child);
        child = DEQ_NEXT(child);
    }

    return dup;
}


qd_parsed_field_t *qd_parse_dup(const qd_parsed_field_t *field)
{
    return field ? qd_parse_dup_internal(field, 0) : 0;
}


int qd_parse_ok(qd_parsed_field_t *field)
{
    return field->parse_error == 0;
}


const char *qd_parse_error(qd_parsed_field_t *field)
{
    return field->parse_error;
}


uint8_t qd_parse_tag(qd_parsed_field_t *field)
{
    return field->tag;
}


qd_iterator_t *qd_parse_raw(qd_parsed_field_t *field)
{
    if (!field)
        return 0;

    return field->raw_iter;
}


qd_iterator_t *qd_parse_typed(qd_parsed_field_t *field)
{
    return field->typed_iter;
}


uint32_t qd_parse_as_uint(qd_parsed_field_t *field)
{
    uint32_t result = 0;
    uint64_t tmp = qd_parse_as_ulong(field);
    if (qd_parse_ok(field)) {
        if (tmp <= UINT32_MAX) {
            result = tmp;
        } else {
            field->parse_error = "Integer value too large to parse as uint";
        }
    }

    return result;
}


uint64_t qd_parse_as_ulong(qd_parsed_field_t *field)
{
    uint64_t result = 0;

    qd_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_ULONG:
    case QD_AMQP_TIMESTAMP:
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 56;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 48;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 40;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 32;
        // Fall Through...

    case QD_AMQP_UINT:
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 24;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 16;
        // Fall Through...

    case QD_AMQP_USHORT:
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 8;
        // Fall Through...

    case QD_AMQP_BOOLEAN:
    case QD_AMQP_UBYTE:
    case QD_AMQP_SMALLUINT:
    case QD_AMQP_SMALLULONG:
        result |= (uint64_t) qd_iterator_octet(field->raw_iter);
        break;

    case QD_AMQP_TRUE:
        result = 1;
        break;

    case QD_AMQP_FALSE:
    case QD_AMQP_UINT0:
    case QD_AMQP_ULONG0:
        // already zeroed
        break;

    case QD_AMQP_STR8_UTF8:
    case QD_AMQP_STR32_UTF8:
    case QD_AMQP_SYM8:
    case QD_AMQP_SYM32:
        {
            // conversion from string to 64 bit unsigned integer:
            // the maximum unsigned 64 bit value would need 20 characters.
            char buf[64];
            qd_iterator_strncpy(field->raw_iter, buf, sizeof(buf));
            if (sscanf(buf, "%"SCNu64, &result) != 1)
                field->parse_error = "Cannot convert string to unsigned long";
        }
        break;

    case QD_AMQP_BYTE:
    case QD_AMQP_SHORT:
    case QD_AMQP_INT:
    case QD_AMQP_SMALLINT:
    case QD_AMQP_LONG:
    case QD_AMQP_SMALLLONG:
    {
        // if a signed integer is positive, accept it
        int64_t ltmp = qd_parse_as_long(field);
        if (qd_parse_ok(field)) {
            if (ltmp >= 0) {
                result = (uint64_t)ltmp;
            } else {
                field->parse_error = "Unable to parse negative integer as unsigned";
            }
        }
    }
    break;


    default:
        field->parse_error = "Unable to parse as an unsigned integer";
        // catch any missing types during development
        assert(false);
    }

    return result;
}


int32_t qd_parse_as_int(qd_parsed_field_t *field)
{
    int32_t result = 0;
    int64_t tmp = qd_parse_as_long(field);
    if (qd_parse_ok(field)) {
        if (INT32_MIN <= tmp && tmp <= INT32_MAX) {
            result = tmp;
        } else {
            field->parse_error = "Integer value too large to parse as int";
        }
    }

    return result;
}


int64_t qd_parse_as_long(qd_parsed_field_t *field)
{
    int64_t result = 0;

    qd_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_LONG: {
        uint64_t tmp = ((uint64_t) qd_iterator_octet(field->raw_iter)) << 56;
        tmp |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 48;
        tmp |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 40;
        tmp |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 32;
        tmp |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 24;
        tmp |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 16;
        tmp |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 8;
        tmp |= (uint64_t) qd_iterator_octet(field->raw_iter);
        result = (int64_t) tmp;
        break;
    }

    case QD_AMQP_INT: {
        uint32_t tmp = ((uint32_t) qd_iterator_octet(field->raw_iter)) << 24;
        tmp |= ((uint32_t) qd_iterator_octet(field->raw_iter)) << 16;
        tmp |= ((uint32_t) qd_iterator_octet(field->raw_iter)) << 8;
        tmp |= ((uint32_t) qd_iterator_octet(field->raw_iter));
        result = (int32_t) tmp;
        break;
    }

    case QD_AMQP_SHORT: {
        uint16_t tmp = ((uint16_t) qd_iterator_octet(field->raw_iter)) << 8;
        tmp |= ((uint16_t) qd_iterator_octet(field->raw_iter));
        result = (int16_t) tmp;
        break;
    }

    case QD_AMQP_BYTE:
    case QD_AMQP_BOOLEAN:
    case QD_AMQP_SMALLLONG:
    case QD_AMQP_SMALLINT:
        result = (int8_t) qd_iterator_octet(field->raw_iter);
        break;

    case QD_AMQP_TRUE:
        result = 1;
        break;

    case QD_AMQP_FALSE:
    case QD_AMQP_UINT0:
    case QD_AMQP_ULONG0:
        // already zeroed
        break;

    case QD_AMQP_STR8_UTF8:
    case QD_AMQP_STR32_UTF8:
    case QD_AMQP_SYM8:
    case QD_AMQP_SYM32:
        {
            // conversion from string to 64 bit integer:
            // the maximum 64 bit value would need 20 characters.
            char buf[64];
            qd_iterator_strncpy(field->raw_iter, buf, sizeof(buf));
            if (sscanf(buf, "%"SCNi64, &result) != 1)
                field->parse_error = "Cannot convert string to long";
        }
        break;

    case QD_AMQP_UBYTE:
    case QD_AMQP_SMALLUINT:
    case QD_AMQP_SMALLULONG:
    case QD_AMQP_USHORT:
    case QD_AMQP_UINT:
    case QD_AMQP_ULONG:
    {
        // if an unsigned integer "fits" accept it
        uint64_t utmp = qd_parse_as_ulong(field);
        if (qd_parse_ok(field)) {
            uint64_t max = INT8_MAX;
            switch (field->tag) {
            case QD_AMQP_USHORT:
                max = INT16_MAX;
                break;
            case QD_AMQP_UINT:
                max = INT32_MAX;
                break;
            case QD_AMQP_ULONG:
                max = INT64_MAX;
                break;
            }
            if (utmp <= max) {
                result = (int64_t)utmp;
            } else {
                field->parse_error = "Unable to parse unsigned integer as a signed integer";
            }
        }
    }
    break;

    default:
        field->parse_error = "Unable to parse as a signed integer";
        // catch any missing types during development
        assert(false);
    }

    return result;
}


bool qd_parse_as_bool(qd_parsed_field_t *field)
{
    bool result = false;

    qd_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_BYTE:
    case QD_AMQP_BOOLEAN:
        result = !!qd_iterator_octet(field->raw_iter);
        break;

    case QD_AMQP_TRUE:
        result = true;
        break;
    }

    return result;
}


uint32_t qd_parse_sub_count(qd_parsed_field_t *field)
{
    uint32_t count = DEQ_SIZE(field->children);

    if (field->tag == QD_AMQP_MAP8 || field->tag == QD_AMQP_MAP32)
        count = count >> 1;

    return count;
}


qd_parsed_field_t *qd_parse_sub_key(qd_parsed_field_t *field, uint32_t idx)
{
    if (field->tag != QD_AMQP_MAP8 && field->tag != QD_AMQP_MAP32)
        return 0;

    idx = idx << 1;
    qd_parsed_field_t *key = DEQ_HEAD(field->children);
    while (idx && key) {
        idx--;
        key = DEQ_NEXT(key);
    }

    return key;
}


qd_parsed_field_t *qd_parse_sub_value(qd_parsed_field_t *field, uint32_t idx)
{
    if (field->tag == QD_AMQP_MAP8 || field->tag == QD_AMQP_MAP32)
        idx = (idx << 1) + 1;

    qd_parsed_field_t *key = DEQ_HEAD(field->children);
    while (idx && key) {
        idx--;
        key = DEQ_NEXT(key);
    }

    return key;
}


int is_tag_a_map(uint8_t tag)
{
    return tag == QD_AMQP_MAP8 || tag == QD_AMQP_MAP32;
}


int qd_parse_is_map(qd_parsed_field_t *field)
{
    if (!field)
        return 0;

    return is_tag_a_map(field->tag);
}


int qd_parse_is_list(qd_parsed_field_t *field)
{
    if (!field)
        return 0;

    return field->tag == QD_AMQP_LIST8
        || field->tag == QD_AMQP_LIST32
        || field->tag == QD_AMQP_LIST0;
}


int qd_parse_is_scalar(qd_parsed_field_t *field)
{
    return DEQ_SIZE(field->children) == 0;
}


qd_parsed_field_t *qd_parse_value_by_key(qd_parsed_field_t *field, const char *key)
{
    if (!key)
        return 0;

    uint32_t count = qd_parse_sub_count(field);

    for (uint32_t idx = 0; idx < count; idx++) {
        qd_parsed_field_t *sub  = qd_parse_sub_key(field, idx);
        if (!sub)
            return 0;

        qd_iterator_t *iter = qd_parse_raw(sub);
        if (!iter)
            return 0;

        if (qd_iterator_equal(iter, (const unsigned char*) key)) {
            return qd_parse_sub_value(field, idx);
        }
    }

    return 0;
}


// TODO(kgiusti) - de-duplicate all the buffer chain walking code!
// See DISPATCH-1403
//
static inline int _turbo_advance(qd_iterator_pointer_t *ptr, int length)
{
    const int start = ptr->remaining;
    int move = MIN(length, ptr->remaining);
    while (move > 0) {
        int avail = qd_buffer_cursor(ptr->buffer) - ptr->cursor;
        if (move < avail) {
            ptr->cursor += move;
            ptr->remaining -= move;
            break;
        }
        move -= avail;
        ptr->remaining -= avail;
        if (ptr->remaining == 0) {
            ptr->cursor += avail;   // move to end
            break;
        }

        // More remaining in buffer chain: advance to next buffer in chain
        assert(DEQ_NEXT(ptr->buffer));
        if (!DEQ_NEXT(ptr->buffer)) {
            // this is an error!  ptr->remainer is not accurate.  This should not happen
            // since the MA field must be completely received at this point
            // (see DISPATCH-1394).
            int copied = start - ptr->remaining;
            ptr->remaining = 0;
            ptr->cursor += avail;  // force to end of chain
            return copied;
        }
        ptr->buffer = DEQ_NEXT(ptr->buffer);
        ptr->cursor = qd_buffer_base(ptr->buffer);
    }
    return start - ptr->remaining;
}


// TODO(kgiusti): deduplicate!
// See DISPATCH-1403
//
static inline int _turbo_copy(qd_iterator_pointer_t *ptr, char *buffer, int length)
{
    int move = MIN(length, ptr->remaining);
    char * const start = buffer;
    while (ptr->remaining && move > 0) {
        int avail = MIN(move, qd_buffer_cursor(ptr->buffer) - ptr->cursor);
        memcpy(buffer, ptr->cursor, avail);
        buffer += avail;
        move -= avail;
        _turbo_advance(ptr, avail);
    }
    return (buffer - start);
}


const char *qd_parse_annotations_v1(
    bool                   strip_anno_in,
    qd_iterator_t         *ma_iter_in,
    qd_parsed_field_t    **ma_ingress,
    qd_parsed_field_t    **ma_phase,
    qd_parsed_field_t    **ma_to_override,
    qd_parsed_field_t    **ma_trace,
    qd_parsed_field_t    **ma_stream,
    qd_iterator_pointer_t *blob_pointer,
    uint32_t              *blob_item_count)
{
    // Do full parse
    qd_iterator_reset(ma_iter_in);

    qd_parsed_turbo_list_t annos;
    uint32_t               user_entries;
    uint32_t               user_bytes;
    const char * parse_error = qd_parse_turbo(ma_iter_in, &annos, &user_entries, &user_bytes);
    if (parse_error) {
        return parse_error;
    }

    // define a shorthand name for the qd message annotation key prefix length
#define QMPL QD_MA_PREFIX_LEN

    // trace, phase, and class keys are all the same length
    assert(QD_MA_TRACE_LEN == QD_MA_PHASE_LEN);
    assert(QD_MA_TRACE_LEN == QD_MA_CLASS_LEN);
    
    qd_parsed_turbo_t *anno;
    if (!strip_anno_in) {
        anno = DEQ_HEAD(annos);
        while (anno) {
            uint8_t * dp;                     // pointer to key name in raw buf or extract buf
            char key_name[QD_MA_MAX_KEY_LEN]; // key name extracted across buf boundary
            int key_len = anno->size;

            const int avail = qd_buffer_cursor(anno->bufptr.buffer) - anno->bufptr.cursor;
            if (avail >= anno->size + anno->length_of_size + 1) {
                // The best case: key name is completely in current raw buffer
                dp = anno->bufptr.cursor + anno->length_of_size + 1;
            } else {
                // Pull the key name from multiple buffers
                qd_iterator_pointer_t wbuf = anno->bufptr;    // scratch buf pointers for getting key
                _turbo_advance(&wbuf, anno->length_of_size + 1);
                int t_size = MIN(anno->size, QD_MA_MAX_KEY_LEN); // get this many total
                key_len = _turbo_copy(&wbuf, key_name, t_size);

                dp = (uint8_t *)key_name;
            }

            // Verify that the key starts with the prefix.
            // Once a key with the routing prefix is observed in the annotation
            // stream then the remainder of the keys must be routing keys.
            // Padding keys are not real routing annotations but they have
            // the routing prefix.
            assert(key_len >= QMPL && memcmp(QD_MA_PREFIX, dp, QMPL) == 0);

            // Advance pointer to data beyond the common prefix
            dp += QMPL;

            qd_ma_enum_t ma_type = QD_MAE_NONE;
            switch (key_len) {
                case QD_MA_TO_LEN:
                    if (memcmp(QD_MA_TO + QMPL,      dp, QD_MA_TO_LEN - QMPL) == 0) {
                        ma_type = QD_MAE_TO;
                    }
                    break;
                case QD_MA_TRACE_LEN:
                    if (memcmp(QD_MA_TRACE + QMPL,  dp, QD_MA_TRACE_LEN - QMPL) == 0) {
                        ma_type = QD_MAE_TRACE;
                    } else
                    if (memcmp(QD_MA_PHASE + QMPL,  dp, QD_MA_PHASE_LEN - QMPL) == 0) {
                        ma_type = QD_MAE_PHASE;
                    }
                    break;
                case QD_MA_INGRESS_LEN:
                    if (memcmp(QD_MA_INGRESS + QMPL, dp, QD_MA_INGRESS_LEN - QMPL) == 0) {
                        ma_type = QD_MAE_INGRESS;
                    }
                    break;
                case QD_MA_STREAM_LEN:
                    if (memcmp(QD_MA_STREAM + QMPL, dp, QD_MA_STREAM_LEN - QMPL) == 0) {
                        ma_type = QD_MAE_STREAM;
                    }
                    break;
                default:
                    // padding annotations are ignored here
                    break;
            }

            // Process the data field
            anno = DEQ_NEXT(anno);
            assert(anno);

            if (ma_type != QD_MAE_NONE) {
                // produce a parsed_field for the data
                qd_iterator_t *val_iter =
                    qd_iterator_buffer(anno->bufptr.buffer,
                                    anno->bufptr.cursor - qd_buffer_base(anno->bufptr.buffer),
                                    anno->size + anno->length_of_size,
                                    ITER_VIEW_ALL);
                assert(val_iter);

                qd_parsed_field_t *val_field = qd_parse(val_iter);
                assert(val_field);

                // transfer ownership of the extracted value to the message
                switch (ma_type) {
                    case QD_MAE_INGRESS:
                        *ma_ingress = val_field;
                        break;
                    case QD_MAE_TRACE:
                        *ma_trace = val_field;
                        break;
                    case QD_MAE_TO:
                        *ma_to_override = val_field;
                        break;
                    case QD_MAE_PHASE:
                        *ma_phase = val_field;
                        break;
                    case QD_MAE_STREAM:
                        *ma_stream = val_field;
                        break;
                    case QD_MAE_NONE:
                        assert(false);
                        break;
                }

                qd_iterator_free(val_iter);
            }
            anno = DEQ_NEXT(anno);
        }
    }

    anno = DEQ_HEAD(annos);
    while (anno) {
        DEQ_REMOVE_HEAD(annos);
        free_qd_parsed_turbo_t(anno);
        anno = DEQ_HEAD(annos);
    }

    // Adjust size of user annotation blob by the size of the router
    // annotations
    blob_pointer->remaining = user_bytes;
    assert(blob_pointer->remaining >= 0);

    *blob_item_count = user_entries;
    return 0;
}


void qd_parse_annotations(
    bool                   strip_annotations_in,
    qd_iterator_t         *ma_iter_in,
    qd_parsed_field_t    **ma_ingress,
    qd_parsed_field_t    **ma_phase,
    qd_parsed_field_t    **ma_to_override,
    qd_parsed_field_t    **ma_trace,
    qd_parsed_field_t    **ma_stream,
    qd_iterator_pointer_t *blob_pointer,
    uint32_t              *blob_item_count)
{
    *ma_ingress             = 0;
    *ma_phase               = 0;
    *ma_to_override         = 0;
    *ma_trace               = 0;
    ZERO(blob_pointer);
    *blob_item_count        = 0;

    if (!ma_iter_in)
        return;

    uint8_t  tag             = 0;
    uint32_t size            = 0;
    uint32_t length_of_count = 0;
    uint32_t length_of_size  = 0;

    const char *parse_error = get_type_info(ma_iter_in, &tag,
                                            &size, blob_item_count, &length_of_size,
                                            &length_of_count);
    if (parse_error)
        return;

    if (!is_tag_a_map(tag)) {
        return;
    }

    // Initial snapshot on size/content of annotation payload
    qd_iterator_t *raw_iter = qd_iterator_sub(ma_iter_in, (size - length_of_count));

    // If there are no router annotations then all annotations
    // are the user's opaque blob.
    qd_iterator_get_view_cursor(raw_iter, blob_pointer);

    qd_iterator_free(raw_iter);

    (void) qd_parse_annotations_v1(strip_annotations_in, ma_iter_in, ma_ingress, ma_phase,
                                    ma_to_override, ma_trace, ma_stream,
                                    blob_pointer, blob_item_count);

    return;
}
