#ifndef __dispatch_parse_h__
#define __dispatch_parse_h__ 1
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

#include "qpid/dispatch/buffer_field.h"
#include "qpid/dispatch/iterator.h"

/**@file
 * Parse raw data fields into AMQP data trees.
 *
 * @defgroup parse parse
 *
 * Parse data from raw octets into a tree structure representing
 * an AMQP data type tree.
 *@{
 */

typedef struct qd_parsed_field_t qd_parsed_field_t;

/**
 * Parse a field delimited by a buffer field.
 *
 * @param bfield holds the data to be parsed
 * @return A pointer to the newly created field.
 */
qd_parsed_field_t *qd_parse(const qd_iterator_t *iter);

/**
 * Free the resources associated with a parsed field.
 *
 * @param field A field pointer returned by qd_parse.
 */
void qd_parse_free(qd_parsed_field_t *field);

/**
 * Create a duplicate parsed field, referring to the same base data.
 *
 * @param field A field pointer returned by qd_parse.
 * @return A separate field that is a duplicate of the supplied field.
 */
qd_parsed_field_t *qd_parse_dup(const qd_parsed_field_t *field);

/**
 * Check to see if the field parse was successful (i.e. the field was
 * well-formed).
 *
 * @param field The field pointer returned by qd_parse.
 * @return true iff the field was well-formed and successfully parsed.
 */
int qd_parse_ok(qd_parsed_field_t *field);

/**
 * Return the text of the error describing the parse error if the field
 * is not well-formed.
 *
 * @param field The field pointer returned by qd_parse.
 * @return a null-terminated string describing the parse failure.
 */
const char *qd_parse_error(qd_parsed_field_t *field);

/**
 * Return the AMQP tag for the parsed (and well-formed) field.
 *
 * @param field The field pointer returned by qd_parse.
 * @return The tag (see amqp.h) that indicates the type of the field.
 */
uint8_t qd_parse_tag(qd_parsed_field_t *field);

/**
 * Return an iterator for the raw content of the field.  This is useful
 * only for scalar fields.  It is not appropriate for compound fields.
 * For compound fields, use the sub-field functions instead.
 *
 * The returned iterator describes the raw content of the field, and can be
 * used for comparison, indexing, or copying.
 *
 * IMPORTANT: The returned iterator is owned by the field and *must not* be
 * freed by the caller of this function.
 *
 * @param field The field pointer returned by qd_parse.
 * @return A field iterator that describes the field's raw content.
 */
qd_iterator_t *qd_parse_raw(qd_parsed_field_t *field);

/**
 * Return an iterator for the typed content of the field. Contains the type followed by the raw content.
 *
 * IMPORTANT: The returned iterator is owned by the field and *must not* be
 * freed by the caller of this function.
 *
 * @param field The field pointer returned by qd_parse.
 * @return A field iterator that describes the field's typed content.
 */
qd_iterator_t *qd_parse_typed(qd_parsed_field_t *field);

/**
 * Return the location and length of the raw value of the parsed field.
 *
 * The returned value does not include the parsed fields header. If the field
 * is a container (map/list) then the returned value is the content of the
 * container, including each entries encoded header.
 *
 * IMPORTANT: The returned location remains valid for the lifetime of the
 * parsed field.
 *
 * @param field The field pointer returned by qd_parse.
 * @return The location in the buffer chain containing the field's raw content.
 */
qd_buffer_field_t qd_parse_value(const qd_parsed_field_t *field);

/**
 * Return the raw content as an unsigned integer up to 32-bits.  This is
 * valid only for scalar fields of a fixed size of 4-octets or fewer.
 *
 * @param field The field pointer returned by qd_parse.
 * @return The raw content of the field cast as a uint32_t.
 */
uint32_t qd_parse_as_uint(qd_parsed_field_t *field);

/**
 * Return the raw content as an unsigned integer up to 64-bits.  This is
 * valid only for scalar fields of a fixed size of 8-octets or fewer.
 *
 * @param field The field pointer returned by qd_parse.
 * @return The raw content of the field cast as a uint64_t.
 */
uint64_t qd_parse_as_ulong(qd_parsed_field_t *field);

/**
 * Return the raw content as a signed integer up to 32-bits.  This is
 * valid only for scalar fields of a fixed size of 4-octets or fewer.
 *
 * @param field The field pointer returned by qd_parse.
 * @return The raw content of the field cast as an int32_t.
 */
int32_t qd_parse_as_int(qd_parsed_field_t *field);

/**
 * Return the raw content as a signed integer up to 64-bits.  This is
 * valid only for scalar fields of a fixed size of 8-octets or fewer.
 *
 * @param field The field pointer returned by qd_parse.
 * @return The raw content of the field cast as an int64_t.
 */
int64_t qd_parse_as_long(qd_parsed_field_t *field);

/**
 * Return the raw content as a boolean value.
 *
 * @param field The field pointer returned by qd_parse.
 * @return The raw content of the field cast as a bool.
 */
bool qd_parse_as_bool(qd_parsed_field_t *field);

/**
 * Return the raw content as a c-string.  This is valid only for SYM* and STR*
 * parsed types. The caller is responsible for freeing the returned string.
 *
 * @param field The field pointer returned by qd_parse.
 * @return a C string containing the value or 0 if conversion failed.
 */
char *qd_parse_as_string(const qd_parsed_field_t *parsed_field);

/**
 * Return the number of sub-fields in a compound field.  If the field is
 * a list or array, this is the number of items in the list/array.  If
 * the field is a map, this is the number of key/value pairs in the map
 * (i.e. half the number of actual sub-field in the map).
 *
 * For scalar fields, this function will return zero.
 *
 * @param field The field pointer returned by qd_parse.
 * @return The number of sub-fields in the field.
 */
uint32_t qd_parse_sub_count(qd_parsed_field_t *field);

/**
 * Return a qd_parsed_field_t for the idx'th key in a map field.
 * If 'field' is not a map, or idx is equal-to or greater-than the number
 * of sub-fields in field, this function will return NULL.
 *
 * IMPORTANT: The pointer returned by this function remains owned by the
 * parent field.  It *must not* be freed by the caller.
 *
 * @param field The field pointer returned by qd_parse.
 * @param idx The index of the desired sub-field (in range 0..sub_count)
 * @return A pointer to the parsed sub-field
 */
qd_parsed_field_t *qd_parse_sub_key(qd_parsed_field_t *field, uint32_t idx);

/**
 * Return a qd_parsed_field_t for the idx'th value in a compound field.
 * If idx is equal-to or greater-than the number of sub-fields in field,
 * this function will return NULL.
 *
 * IMPORTANT: The pointer returned by this function remains owned by the
 * parent field.  It *must not* be freed by the caller.
 *
 * @param field The field pointer returned by qd_parse.
 * @param idx The index of the desired sub-field (in range 0..sub_count)
 * @return A pointer to the parsed sub-field
 */
qd_parsed_field_t *qd_parse_sub_value(qd_parsed_field_t *field, uint32_t idx);

qd_parsed_field_t *qd_field_first_child(qd_parsed_field_t *field);
qd_parsed_field_t *qd_field_next_child(qd_parsed_field_t *field);

/**
 * Convenience Function - Return true iff the field is a map.
 *
 * @param field The field pointer returned by qd_parse[_sub_{value,key}]
 * @return non-zero if the condition is mat.
 */
int qd_parse_is_map(qd_parsed_field_t *field);

/**
 * Convenience Function - Return true iff the field is a list.
 *
 * @param field The field pointer returned by qd_parse[_sub_{value,key}]
 * @return non-zero if the condition is mat.
 */
int qd_parse_is_list(qd_parsed_field_t *field);

/**
 * Convenience Function - Return true iff the field is a scalar type.
 *
 * @param field The field pointer returned by qd_parse[_sub_{value,key}]
 * @return non-zero if the condition is mat.
 */
int qd_parse_is_scalar(qd_parsed_field_t *field);

/**
 * Convenience Function - Return the value for a key in a map.
 *
 * @param field The field pointer returned by qd_parse[_sub_{value,key}]
 * @param key The key to search for in the map.
 * @return The value field corresponding to the key or NULL.
 */
qd_parsed_field_t *qd_parse_value_by_key(qd_parsed_field_t *field, const char *key);

/**
 * Parse a message annotation map field.
 * Return parsed fields for the four router entries or null if any is absent
 * and a blob pointer and count for the user entries in the map which are passed
 * through as part of the message payload.
 *
 * @param strip_annotations_in strip inbound annotations
 * @param ma_iter_in Field iterator for the annotation map field being parsed.
 * @param ma_ingress returned parsed field: ingress
 * @param ma_phase returned parsed field: phase
 * @param ma_to_override returned parsed field: override
 * @param ma_trace returned parsed field: trace
 * @param  returned buffer pointer to user's annotation blob
 * @param blob_item_count number of map entries referenced by blob_iterator
 */
const char *qd_parse_annotations(
    bool                   strip_annotations_in,
    qd_iterator_t         *ma_iter_in,
    qd_parsed_field_t    **ma_ingress,
    qd_parsed_field_t    **ma_phase,
    qd_parsed_field_t    **ma_to_override,
    qd_parsed_field_t    **ma_trace,
    qd_parsed_field_t    **ma_stream,
    qd_buffer_field_t     *user_annotations,
    uint32_t              *user_count);

/**
 * Identify which annotation is being parsed
 */
typedef enum {
    QD_MAE_INGRESS,
    QD_MAE_TRACE,
    QD_MAE_TO,
    QD_MAE_PHASE,
    QD_MAE_STREAM,
    QD_MAE_NONE
} qd_ma_enum_t;


/**
 * Parse a 32 bit unsigned integer in network order to a native uint32 value.
 *
 * This is used throughout the code for decoding the size and count components
 * of variable-sized AMQP types.
 *
 * The caller must ensure buf references four contiguous octets in memory.
 */
static inline uint32_t qd_parse_uint32_decode(const uint8_t buf[])
{
    return (((uint32_t) buf[0]) << 24)
        |  (((uint32_t) buf[1]) << 16)
        |  (((uint32_t) buf[2]) << 8)
        |  ((uint32_t) buf[3]);
}

///@}

#endif

