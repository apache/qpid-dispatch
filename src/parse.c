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

/**
 * size = the number of bytes following the tag
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

    qd_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_UINT:
        result |= ((uint32_t) qd_iterator_octet(field->raw_iter)) << 24;
        result |= ((uint32_t) qd_iterator_octet(field->raw_iter)) << 16;
        // fallthrough

    case QD_AMQP_USHORT:
        result |= ((uint32_t) qd_iterator_octet(field->raw_iter)) << 8;
        // Fall Through...

    case QD_AMQP_UBYTE:
    case QD_AMQP_SMALLUINT:
    case QD_AMQP_BOOLEAN:
        result |= (uint32_t) qd_iterator_octet(field->raw_iter);
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

    qd_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_ULONG:
    case QD_AMQP_TIMESTAMP:
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 56;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 48;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 40;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 32;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 24;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 16;
        result |= ((uint64_t) qd_iterator_octet(field->raw_iter)) << 8;
        // Fall Through...

    case QD_AMQP_SMALLULONG:
        result |= (uint64_t) qd_iterator_octet(field->raw_iter);
        // Fall Through...

    case QD_AMQP_ULONG0:
        break;
    }

    return result;
}


int32_t qd_parse_as_int(qd_parsed_field_t *field)
{
    int32_t result = 0;

    qd_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_INT:
        result |= ((int32_t) qd_iterator_octet(field->raw_iter)) << 24;
        result |= ((int32_t) qd_iterator_octet(field->raw_iter)) << 16;
        // Fall Through...

    case QD_AMQP_SHORT:
        result |= ((int32_t) qd_iterator_octet(field->raw_iter)) << 8;
        // Fall Through...

    case QD_AMQP_BYTE:
    case QD_AMQP_BOOLEAN:
        result |= (int32_t) qd_iterator_octet(field->raw_iter);
        break;

    case QD_AMQP_SMALLINT:
        result = (int8_t) qd_iterator_octet(field->raw_iter);
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

    qd_iterator_reset(field->raw_iter);

    switch (field->tag) {
    case QD_AMQP_LONG:
        result |= ((int64_t) qd_iterator_octet(field->raw_iter)) << 56;
        result |= ((int64_t) qd_iterator_octet(field->raw_iter)) << 48;
        result |= ((int64_t) qd_iterator_octet(field->raw_iter)) << 40;
        result |= ((int64_t) qd_iterator_octet(field->raw_iter)) << 32;
        result |= ((int64_t) qd_iterator_octet(field->raw_iter)) << 24;
        result |= ((int64_t) qd_iterator_octet(field->raw_iter)) << 16;
        result |= ((int64_t) qd_iterator_octet(field->raw_iter)) << 8;
        result |= (uint64_t) qd_iterator_octet(field->raw_iter);
        break;

    case QD_AMQP_SMALLLONG:
        result = (int8_t) qd_iterator_octet(field->raw_iter);
        break;
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


qd_parsed_field_t *qd_parse_sub_key_rev(qd_parsed_field_t *field, uint32_t idx)
{
    if (field->tag != QD_AMQP_MAP8 && field->tag != QD_AMQP_MAP32)
        return 0;

    idx = (idx << 1) + 1 ;
    qd_parsed_field_t *key = DEQ_TAIL(field->children);
    while (idx && key) {
        idx--;
        key = DEQ_PREV(key);
    }

    return key;
}


qd_parsed_field_t *qd_parse_sub_value_rev(qd_parsed_field_t *field, uint32_t idx)
{
    if (field->tag == QD_AMQP_MAP8 || field->tag == QD_AMQP_MAP32)
        idx = (idx << 1);

    qd_parsed_field_t *key = DEQ_TAIL(field->children);
    while (idx && key) {
        idx--;
        key = DEQ_PREV(key);
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

        qd_iterator_t *iter = qd_parse_raw(sub);
        if (!iter)
            return 0;

        if (qd_iterator_equal(iter, (const unsigned char*) key)) {
            return qd_parse_sub_value(field, idx);
        }
    }

    return 0;
}


bool qd_parse_annotations_v0(
    qd_iterator_t         *ma_iter_in,
    qd_parsed_field_t    **ma_ingress,
    qd_parsed_field_t    **ma_phase,
    qd_parsed_field_t    **ma_to_override,
    qd_parsed_field_t    **ma_trace,
    qd_iterator_pointer_t *blob_pointer,
    uint32_t              *blob_item_count,
    qd_parsed_field_t    **all_annotations,
    uint32_t               iter_skip)
{
    // A message arrived from a non-peer-router source
    // Locate the annotations and declare them to be the user blob
    (*all_annotations)->raw_iter = qd_iterator_sub(ma_iter_in, iter_skip);
    qd_iterator_advance(ma_iter_in, iter_skip);
    qd_parse_get_view_cursor(*all_annotations, blob_pointer);
    qd_iterator_reset((*all_annotations)->raw_iter);
    return true;
}


bool qd_parse_annotations_v1(
    qd_iterator_t         *ma_iter_in,
    qd_parsed_field_t    **ma_ingress,
    qd_parsed_field_t    **ma_phase,
    qd_parsed_field_t    **ma_to_override,
    qd_parsed_field_t    **ma_trace,
    qd_iterator_pointer_t *blob_pointer,
    uint32_t              *blob_item_count,
    qd_parsed_field_t    **all_annotations,
    uint32_t               iter_skip)
{
    // Initial snapshot on size/content of annotation payload
    (*all_annotations)->raw_iter = qd_iterator_sub(ma_iter_in, iter_skip);
    qd_iterator_advance(ma_iter_in, iter_skip);

    // Capture the blob pointer when there are no router annotations
    qd_parse_get_view_cursor(*all_annotations, blob_pointer);

    // Clear out current view
    qd_parse_free(*all_annotations);

    // Do full parse
    *all_annotations = qd_parse(ma_iter_in);
    if (*all_annotations == 0 ||
        !qd_parse_ok(*all_annotations) ||
        !qd_parse_is_map(*all_annotations))
    {
        return false;
    }

    *blob_item_count = qd_parse_sub_count(*all_annotations);

    // Hunt through the map and find the boundary between
    // user annotations and router annotations.

    uint32_t search_n = (*blob_item_count < 4 ? *blob_item_count : 4);
    uint32_t n_router_annos = 0;
    int      router_raw_bytes = 0;

    for (uint32_t idx = 0; idx < search_n; idx++) {
        qd_parsed_field_t *key  = qd_parse_sub_key_rev(*all_annotations, idx);
        if (!key)
            break;
        qd_iterator_t *iter = qd_parse_raw(key);
        if (!iter)
            break;
        if (qd_iterator_prefix(iter, QD_MA_PREFIX))
            break;
        qd_parsed_field_t *val = qd_parse_sub_value_rev(*all_annotations, idx);
        if (!val)
            break;
        qd_iterator_t *iterv = qd_parse_raw(val);
        if (!iterv)
            break;

        // accumulate router annotations
        // number of annotations
        n_router_annos++;
        // size of annotation key and annotations value
        router_raw_bytes += qd_iterator_get_raw_size(iter);
        router_raw_bytes += qd_iterator_get_raw_size(iterv);

        // put the extracted value into common storage
        if        (qd_iterator_equal(iter, (unsigned char*) QD_MA_TRACE)) {
            *ma_trace = val;
        } else if (qd_iterator_equal(iter, (unsigned char*) QD_MA_INGRESS)) {
            *ma_ingress = val;
        } else if (qd_iterator_equal(iter, (unsigned char*) QD_MA_TO)) {
            *ma_to_override = val;
        } else if (qd_iterator_equal(iter, (unsigned char*) QD_MA_PHASE)) {
            *ma_phase = val;
        }
    }

    // Exit if no router annotations
    if (n_router_annos == 0) {
        return false;
    }

    // Adjust size of user annotation blob by the size of the router
    // annotations
    blob_pointer->remaining -= router_raw_bytes;
    assert(blob_pointer->remaining > 0);
    *blob_item_count -= 2 * n_router_annos;
    assert(*blob_item_count >= 0);
    return true;
}


bool qd_parse_annotations_v2(
    qd_iterator_t         *ma_iter_in,
    qd_parsed_field_t    **ma_ingress,
    qd_parsed_field_t    **ma_phase,
    qd_parsed_field_t    **ma_to_override,
    qd_parsed_field_t    **ma_trace,
    qd_iterator_pointer_t *blob_pointer,
    uint32_t              *blob_item_count,
    qd_parsed_field_t    **all_annotations,
    uint32_t               iter_skip)
{
    // This code looks a lot like qd_parse_internal except:
    // * there is no intent of parsing beyond the first map entry.
    // * this code does not recurse or parse the whole map
    // * this code leaves the iter->raw_iter addressing the unparsed
    //   portion of the incoming annotations map

    (*all_annotations)->raw_iter = qd_iterator_sub(ma_iter_in, iter_skip);
    qd_iterator_advance(ma_iter_in, iter_skip);

    // Capture the blob pointer when there are no router annotations
    qd_parse_get_view_cursor(*all_annotations, blob_pointer);

    // Process first key in map.
    qd_parsed_field_t *key_field = qd_parse_internal((*all_annotations)->raw_iter, 0);
    if (!key_field) {
        (*all_annotations)->parse_error = "Failed to parse first map key";
        qd_iterator_reset((*all_annotations)->raw_iter);
        return false;
    }
    if (!qd_parse_ok(key_field)) {
        (*all_annotations)->parse_error = key_field->parse_error;
        qd_iterator_reset((*all_annotations)->raw_iter);
        qd_parse_free(key_field);
        return false;
    }
    qd_iterator_t *key_iter = qd_parse_raw(key_field);
    
    // Check for the v2 annotation key
    bool result = qd_iterator_equal(key_iter, (const unsigned char *)QD_MA_ANNOTATIONS);

    if (result) {
        // Get the v2 value
        qd_parsed_field_t *v2 = qd_parse_internal((*all_annotations)->raw_iter, (*all_annotations));
        if (!qd_parse_ok(v2)) {
            qd_iterator_reset((*all_annotations)->raw_iter);
            (*all_annotations)->parse_error = v2->parse_error;
            qd_parse_free(key_field);
            return false;
        }

        // Housekeeping: associate the v2 object with the parent parsed field
        // to facilitate object cleanup.
        DEQ_INSERT_TAIL((*all_annotations)->children, v2);

        // Just extracted the parsed field holding the v2 annotations from the incoming
        // message. Set the remainder map field count.
        *blob_item_count -= 2;

        // Capture the blob pointer again, thus stripping the v2 annotations.
        qd_parse_get_view_cursor(*all_annotations, blob_pointer);

        assert(qd_parse_is_list(v2));
        assert(qd_parse_sub_count(v2) == MA_POS_LAST);

        *ma_ingress     = qd_parse_sub_value(v2, MA_POS_INGRESS);
        *ma_phase       = qd_parse_sub_value(v2, MA_POS_PHASE);
        *ma_to_override = qd_parse_sub_value(v2, MA_POS_TO);
        *ma_trace       = qd_parse_sub_value(v2, MA_POS_TRACE);

        // nullify empty fields
        if (qd_iterator_remaining(qd_parse_raw(*ma_ingress)) == 0)
            *ma_ingress = 0;
        if (qd_iterator_remaining(qd_parse_raw(*ma_phase)) == 0)
            *ma_phase = 0;
        if (qd_iterator_remaining(qd_parse_raw(*ma_to_override)) == 0)
            *ma_to_override = 0;
        if (qd_parse_sub_count(*ma_trace) == 0)
            *ma_trace = 0;
    }
    qd_parse_free(key_field);
    return result;
}


void qd_parse_annotations(
    int                    hello_version,
    qd_iterator_t         *ma_iter_in,
    qd_parsed_field_t    **ma_ingress,
    qd_parsed_field_t    **ma_phase,
    qd_parsed_field_t    **ma_to_override,
    qd_parsed_field_t    **ma_trace,
    qd_iterator_pointer_t *blob_pointer,
    uint32_t              *blob_item_count,
    qd_parsed_field_t    **all_annotations)
{
    *ma_ingress             = 0;
    *ma_phase               = 0;
    *ma_to_override         = 0;
    *ma_trace               = 0;
    blob_pointer->buffer    = 0;
    blob_pointer->cursor    = 0;
    blob_pointer->remaining = 0;
    *blob_item_count        = 0;

    if (!ma_iter_in)
        return;

    *all_annotations = new_qd_parsed_field_t();
    if (!*all_annotations)
        return;

    DEQ_ITEM_INIT(*all_annotations);
    DEQ_INIT((*all_annotations)->children);
    (*all_annotations)->parent   = 0;
    (*all_annotations)->raw_iter = 0;
    (*all_annotations)->typed_iter = 0;

    uint32_t size            = 0;
    uint32_t length_of_count = 0;
    uint32_t length_of_size  = 0;

    (*all_annotations)->parse_error = get_type_info(ma_iter_in, &(*all_annotations)->tag, 
                                                    &size, blob_item_count, &length_of_size,
                                                    &length_of_count);
    if ((*all_annotations)->parse_error)
        return;

    if (!qd_parse_is_map((*all_annotations))) {
        (*all_annotations)->parse_error = "Message annotations field is not a map";
        return;
    }

    // HACK ALERT
    if (*blob_item_count > 4) {
        //fprintf(stdout, "V2_DEV Must be the user packet. Break here.\n");
    }

    switch (hello_version) {
        case 0:
            qd_parse_annotations_v0(ma_iter_in, ma_ingress, ma_phase,
                                    ma_to_override, ma_trace,
                                    blob_pointer, blob_item_count,
                                    all_annotations, (size - length_of_count));
            break;
        case 1:
            qd_parse_annotations_v1(ma_iter_in, ma_ingress, ma_phase,
                                    ma_to_override, ma_trace,
                                    blob_pointer, blob_item_count,
                                    all_annotations, (size - length_of_count));
            break;
        case 2:
            qd_parse_annotations_v2(ma_iter_in, ma_ingress, ma_phase,
                                    ma_to_override, ma_trace,
                                    blob_pointer, blob_item_count,
                                    all_annotations, (size - length_of_count));
            break;
        default:
            qd_iterator_reset((*all_annotations)->raw_iter);
            assert(false);
            break;
    };

    return;
}


void qd_parse_get_view_cursor(
    const qd_parsed_field_t *field,
    qd_iterator_pointer_t *ptr)
{
    if (field->raw_iter)
        qd_iterator_get_view_cursor(field->raw_iter, ptr);
}
