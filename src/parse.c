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

#include "alloc.h"
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/parse.h>
#include <qpid/dispatch/amqp.h>

DEQ_DECLARE(qd_parsed_field_t, qd_parsed_field_list_t);

struct qd_parsed_field_t {
    DEQ_LINKS(qd_parsed_field_t);
    const qd_parsed_field_t *parent;
    qd_parsed_field_list_t   children;
    uint8_t                  tag;
    qd_field_iterator_t     *raw_iter;
    qd_field_iterator_t     *typed_iter;
    const char              *parse_error;
};

ALLOC_DECLARE(qd_parsed_field_t);
ALLOC_DEFINE(qd_parsed_field_t);

/**
 * size = the number of bytes following the tag
 * count = the number of elements. Applies only to compound structures
 */
static char *get_type_info(qd_field_iterator_t *iter, uint8_t *tag, uint32_t *size, uint32_t *count, uint32_t *length_of_size, uint32_t *length_of_count)
{
    if (qd_field_iterator_end(iter))
        return "Insufficient Data to Determine Tag";

    *tag             = qd_field_iterator_octet(iter);
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
        *size += ((unsigned int) qd_field_iterator_octet(iter)) << 24;
        *size += ((unsigned int) qd_field_iterator_octet(iter)) << 16;
        *size += ((unsigned int) qd_field_iterator_octet(iter)) << 8;
        *length_of_size = 3;
        // fall through to the next case

    case 0xA0:
    case 0xC0:
    case 0xE0:
        if (qd_field_iterator_end(iter))
            return "Insufficient Data to Determine Length";
        *size += (unsigned int) qd_field_iterator_octet(iter);
        *length_of_size += 1;
        break;

    default:
        return "Invalid Tag - No Length Information";
    }

    switch (*tag & 0xF0) {
    case 0xD0:
    case 0xF0:
        *count += ((unsigned int) qd_field_iterator_octet(iter)) << 24;
        *count += ((unsigned int) qd_field_iterator_octet(iter)) << 16;
        *count += ((unsigned int) qd_field_iterator_octet(iter)) << 8;
        *length_of_count = 3;
        // fall through to the next case

    case 0xC0:
    case 0xE0:
        if (qd_field_iterator_end(iter))
            return "Insufficient Data to Determine Count";
        *count += (unsigned int) qd_field_iterator_octet(iter);
        *length_of_count += 1;
        break;
    }

    if ((*tag == QD_AMQP_MAP8 || *tag == QD_AMQP_MAP32) && (*count & 1))
        return "Odd Number of Elements in a Map";

    if (*length_of_count > *size)
        return "Insufficient Length to Determine Count";

    return 0;
}

static qd_parsed_field_t *qd_parse_internal(qd_field_iterator_t *iter, qd_parsed_field_t *p)
{
    qd_parsed_field_t *field = new_qd_parsed_field_t();
    if (!field)
        return 0;

    DEQ_ITEM_INIT(field);
    DEQ_INIT(field->children);
    field->parent   = p;
    field->raw_iter = 0;
    field->typed_iter = qd_field_iterator_dup(iter);

    uint32_t size            = 0;
    uint32_t count           = 0;
    uint32_t length_of_count = 0;
    uint32_t length_of_size  = 0;

    field->parse_error = get_type_info(iter, &field->tag, &size, &count, &length_of_size, &length_of_count);

    if (!field->parse_error) {
        qd_field_iterator_trim(field->typed_iter, size + length_of_size + 1); // + 1 accounts for the tag length

        field->raw_iter = qd_field_iterator_sub(iter, size - length_of_count);

        qd_field_iterator_advance(iter, size - length_of_count);

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


qd_parsed_field_t *qd_parse(qd_field_iterator_t *iter)
{
    if (!iter)
        return 0;
    return qd_parse_internal(iter, 0);
}


void qd_parse_free(qd_parsed_field_t *field)
{
    if (!field)
        return;

    assert(field->parent == 0);
    if (field->raw_iter)
        qd_field_iterator_free(field->raw_iter);

    if (field->typed_iter)
        qd_field_iterator_free(field->typed_iter);

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
    dup->raw_iter    = qd_field_iterator_dup(field->raw_iter);
    dup->typed_iter  = qd_field_iterator_dup(field->typed_iter);
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


qd_field_iterator_t *qd_parse_raw(qd_parsed_field_t *field)
{
    return field->raw_iter;
}


qd_field_iterator_t *qd_parse_typed(qd_parsed_field_t *field)
{
    return field->typed_iter;
}


uint32_t qd_parse_as_uint(qd_parsed_field_t *field)
{
    uint32_t result = 0;

    qd_field_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_UINT:
        result |= ((uint32_t) qd_field_iterator_octet(field->raw_iter)) << 24;
        result |= ((uint32_t) qd_field_iterator_octet(field->raw_iter)) << 16;
        // fallthrough

    case QD_AMQP_USHORT:
        result |= ((uint32_t) qd_field_iterator_octet(field->raw_iter)) << 8;
        // Fall Through...

    case QD_AMQP_UBYTE:
    case QD_AMQP_SMALLUINT:
    case QD_AMQP_BOOLEAN:
        result |= (uint32_t) qd_field_iterator_octet(field->raw_iter);
        break;

    case QD_AMQP_TRUE:
        result = 1;
        break;
    }

    return result;
}


uint64_t qd_parse_as_ulong(qd_parsed_field_t *field)
{
    uint64_t result = 0;

    qd_field_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_ULONG:
    case QD_AMQP_TIMESTAMP:
        result |= ((uint64_t) qd_field_iterator_octet(field->raw_iter)) << 56;
        result |= ((uint64_t) qd_field_iterator_octet(field->raw_iter)) << 48;
        result |= ((uint64_t) qd_field_iterator_octet(field->raw_iter)) << 40;
        result |= ((uint64_t) qd_field_iterator_octet(field->raw_iter)) << 32;
        result |= ((uint64_t) qd_field_iterator_octet(field->raw_iter)) << 24;
        result |= ((uint64_t) qd_field_iterator_octet(field->raw_iter)) << 16;
        result |= ((uint64_t) qd_field_iterator_octet(field->raw_iter)) << 8;
        // Fall Through...

    case QD_AMQP_SMALLULONG:
        result |= (uint64_t) qd_field_iterator_octet(field->raw_iter);
        // Fall Through...

    case QD_AMQP_ULONG0:
        break;
    }

    return result;
}


int32_t qd_parse_as_int(qd_parsed_field_t *field)
{
    int32_t result = 0;

    qd_field_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_INT:
        result |= ((int32_t) qd_field_iterator_octet(field->raw_iter)) << 24;
        result |= ((int32_t) qd_field_iterator_octet(field->raw_iter)) << 16;
        // Fall Through...

    case QD_AMQP_SHORT:
        result |= ((int32_t) qd_field_iterator_octet(field->raw_iter)) << 8;
        // Fall Through...

    case QD_AMQP_BYTE:
    case QD_AMQP_BOOLEAN:
        result |= (int32_t) qd_field_iterator_octet(field->raw_iter);
        break;

    case QD_AMQP_SMALLINT:
        result = (int8_t) qd_field_iterator_octet(field->raw_iter);
        break;

    case QD_AMQP_TRUE:
        result = 1;
        break;
    }

    return result;
}


int64_t qd_parse_as_long(qd_parsed_field_t *field)
{
    int64_t result = 0;

    qd_field_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_LONG:
        result |= ((int64_t) qd_field_iterator_octet(field->raw_iter)) << 56;
        result |= ((int64_t) qd_field_iterator_octet(field->raw_iter)) << 48;
        result |= ((int64_t) qd_field_iterator_octet(field->raw_iter)) << 40;
        result |= ((int64_t) qd_field_iterator_octet(field->raw_iter)) << 32;
        result |= ((int64_t) qd_field_iterator_octet(field->raw_iter)) << 24;
        result |= ((int64_t) qd_field_iterator_octet(field->raw_iter)) << 16;
        result |= ((int64_t) qd_field_iterator_octet(field->raw_iter)) << 8;
        result |= (uint64_t) qd_field_iterator_octet(field->raw_iter);
        break;

    case QD_AMQP_SMALLLONG:
        result = (int8_t) qd_field_iterator_octet(field->raw_iter);
        break;
    }

    return result;
}


bool qd_parse_as_bool(qd_parsed_field_t *field)
{
    bool result = false;

    qd_field_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_BYTE:
    case QD_AMQP_BOOLEAN:
        result = !!qd_field_iterator_octet(field->raw_iter);
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


int qd_parse_is_map(qd_parsed_field_t *field)
{
    return field->tag == QD_AMQP_MAP8 || field->tag == QD_AMQP_MAP32;
}


int qd_parse_is_list(qd_parsed_field_t *field)
{
    return field->tag == QD_AMQP_LIST8 || field->tag == QD_AMQP_LIST32;
}


int qd_parse_is_scalar(qd_parsed_field_t *field)
{
    return DEQ_SIZE(field->children) == 0;
}


qd_parsed_field_t *qd_parse_value_by_key(qd_parsed_field_t *field, const char *key)
{
    uint32_t count = qd_parse_sub_count(field);

    for (uint32_t idx = 0; idx < count; idx++) {
        qd_parsed_field_t *sub  = qd_parse_sub_key(field, idx);
        if (!sub)
            return 0;

        qd_field_iterator_t *iter = qd_parse_raw(sub);
        if (!iter)
            return 0;

        if (qd_field_iterator_equal(iter, (const unsigned char*) key)) {
            return qd_parse_sub_value(field, idx);
        }
    }

    return 0;
}
