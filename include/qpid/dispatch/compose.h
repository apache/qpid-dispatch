#ifndef __dispatch_compose_h__
#define __dispatch_compose_h__ 1
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

#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/buffer.h"
#include "qpid/dispatch/iterator.h"

#include <inttypes.h>

/** A linked list of buffers composing a sequence of AMQP data objects. */
typedef struct qd_composed_field_t qd_composed_field_t;


/**@file
 * Composing AMQP data trees.
 * 
 * @defgroup compose compose
 *
 * Compose a tree-structure representing an AMQP datatype that can
 * be serialized into a message.
 * @{
 */

/**
 * Begin composing a new field for a message.
 *
 * The new field can be standalone or appended onto an existing field.
 *
 * @param performative The performative for the message section being composed.
 * @param extend An existing field onto which to append the new field or NULL to
 *        create a standalone field.
 * @return A pointer to the newly created field.
 */
qd_composed_field_t *qd_compose(uint64_t performative, qd_composed_field_t *extend);

/**
 * Free the resources associated with a composed field.
 *
 * @param field A field pointer returned by qd_compose.
 */
void qd_compose_free(qd_composed_field_t *field);

/**
 * Begin to compose the elements of a list in the field.  This is called before inserting
 * the first list element.
 *
 * @param field A field created by qd_compose.
 */
void qd_compose_start_list(qd_composed_field_t *field);

/**
 * Complete the composition of a list in the field.  This is called after the last
 * list element has been inserted.
 *
 * @param field A field created by qd_compose.
 */
void qd_compose_end_list(qd_composed_field_t *field);

/**
 * Insert an empty list into the field.
 *
 * @param field A field created by qd_compose.
 */
void qd_compose_empty_list(qd_composed_field_t *field);

/**
 * Begin to compose the elements os a map in the field.  This is called before
 * inserting the first element-pair into the map.
 *
 * @param field A field created by qd_compose.
 */
void qd_compose_start_map(qd_composed_field_t *field);

/**
 * Complete the composition of a map in the field.  This is called after the last
 * element-pair has been inserted.
 *
 * @param field A field created by qd_compose.
 */
void qd_compose_end_map(qd_composed_field_t *field);

/**
 * Insert a null element into the field.
 *
 * @param field A field created by qd_compose.
 */
void qd_compose_insert_null(qd_composed_field_t *field);

/**
 * Insert a boolean value into the field.
 *
 * @param field A field created by qd_compose.
 * @param value The boolean (zero or non-zero) value to insert.
 */
void qd_compose_insert_bool(qd_composed_field_t *field, int value);

/**
 * Insert an unsigned integer (up to 32 bits) into the field.
 *
 * @param field A field created by qd_compose.
 * @param value The unsigned integer value to be inserted.
 */
void qd_compose_insert_uint(qd_composed_field_t *field, uint32_t value);

/**
 * Insert a long (64-bit) unsigned value into the field.
 *
 * @param field A field created by qd_compose.
 * @param value The unsigned integer value to be inserted.
 */
void qd_compose_insert_ulong(qd_composed_field_t *field, uint64_t value);

/**
 * Insert a signed integer (up to 32 bits) into the field.
 *
 * @param field A field created by qd_compose.
 * @param value The integer value to be inserted.
 */
void qd_compose_insert_int(qd_composed_field_t *field, int32_t value);

/**
 * Insert a long signed integer (64 bits) into the field.
 *
 * @param field A field created by qd_compose.
 * @param value The integer value to be inserted.
 */
void qd_compose_insert_long(qd_composed_field_t *field, int64_t value);

/**
 * Insert a timestamp into the field.
 *
 * @param field A field created by qd_compose.
 * @param value The timestamp value to be inserted.
 */
void qd_compose_insert_timestamp(qd_composed_field_t *field, uint64_t value);

/**
 * Insert a UUID into the field.
 *
 * @param field A field created by qd_compose.
 * @param value The pointer to the first octet in the UUID to be inserted.
 */
void qd_compose_insert_uuid(qd_composed_field_t *field, const uint8_t *value);

/**
 * Insert a binary blob into the field.
 *
 * @param field A field created by qd_compose.
 * @param value The pointer to the first octet to be inserted.
 * @param len The length, in octets, of the binary blob.
 */
void qd_compose_insert_binary(qd_composed_field_t *field, const uint8_t *value, uint32_t len);

/**
 * Insert a binary blob from a list of buffers.
 *
 * @param field A field created by qd_compose.
 * @param buffers A pointer to a list of buffers to be inserted as binary data.  Note that
 *        the buffer list will be left empty by this function.
 */
void qd_compose_insert_binary_buffers(qd_composed_field_t *field, qd_buffer_list_t *buffers);

/**
 * Insert a null-terminated utf8-encoded string into the field.
 *
 * @param field A field created by qd_compose.
 * @param value A pointer to a null-terminated string.
 */
void qd_compose_insert_string(qd_composed_field_t *field, const char *value);
void qd_compose_insert_string2(qd_composed_field_t *field, const char *value1, const char *value2);
void qd_compose_insert_string_n(qd_composed_field_t *field, const char *value, size_t len);

/**
 * Insert a utf8-encoded string into the field from an iterator
 *
 * @param field A field created by qd_compose.
 * @param iter An iterator for a string value.  The caller is responsible for freeing
 *        this iterator after the call is complete.
 */
void qd_compose_insert_string_iterator(qd_composed_field_t *field, qd_iterator_t *iter);

/**
 * Insert a symbol into the field.
 *
 * @param field A field created by qd_compose.
 * @param value A pointer to a null-terminated ASCII string.
 */
void qd_compose_insert_symbol(qd_composed_field_t *field, const char *value);

/**
 * Insert a type-tagged value into the field from an iterator
 *
 * @param field A field created by qd_compose.
 * @param iter An iterator for a typed value.  The caller is responsible for freeing
 *        this iterator after the call is complete.
 */
void qd_compose_insert_typed_iterator(qd_composed_field_t *field, qd_iterator_t *iter);

/**
 * Begin composing a new sub field that can be appended to a composed field.
 *
 * @param extend An existing field onto which to append the new field or NULL to
 *        create a standalone field.
 * @return A pointer to the newly created field.
 */
qd_composed_field_t *qd_compose_subfield(qd_composed_field_t *extend);

/**
 * Steal the underlying buffers away from a composed field.
 *
 * @param field A composed field whose buffers will be taken
 * @param list To hold the extracted buffers
 */
void qd_compose_take_buffers(qd_composed_field_t *field,
                             qd_buffer_list_t *list);
/**
 * Append a buffer list into a composed field.  If field is a container type
 * (list, map), the container's count will be incremented by one.
 *
 * @param field A field created by ::qd_compose().
 * @param list A list of buffers containing a single completely encoded data
 * object.  Ownership of these buffers is given to field.
 */
void qd_compose_insert_buffers(qd_composed_field_t *field, qd_buffer_list_t *list);

/**
 * Bump map field's count and size to reflect opaque bytes that
 * the caller will insert later. The caller knows how many map items
 * the bytes represent and these are not accounted for using normal
 * compose construction functions.
 *
 * This function does not insert bytes into the field.
 *
 * @param field A field created by qd_compose().
 * @param count The number of map elements to be added to the buffer chain.
 * @param size The number of bytes to be added the buffer chain
 */
void qd_compose_insert_opaque_elements(qd_composed_field_t *field,
                                       uint32_t             count,
                                       uint32_t             size);

/**
 * Insert a double floating point (64 bits) into the field.
 *
 * @param field A field created by qd_compose.
 * @param value The double value to be inserted.
 */
void qd_compose_insert_double(qd_composed_field_t *field, double value);

/**
 * Write a uint32 value in network order to buf.
 *
 * This is used throughout the code for writing the size and count components
 * of variable-sized AMQP types.
 *
 * The caller must ensure buf references four contiguous octets in memory.
 */
static inline void qd_compose_uint32_encode(uint32_t value, uint8_t buf[])
{
  buf[0] = (uint8_t) ((value & 0xFF000000) >> 24);
  buf[1] = (uint8_t) ((value & 0x00FF0000) >> 16);
  buf[2] = (uint8_t) ((value & 0x0000FF00) >> 8);
  buf[3] = (uint8_t) (value & 0x000000FF);
}

/**
 * Compose the proper header for a map given entry count and data size.
 *
 * The caller must ensure hdr references nine contiguous octets in memory.
 *
 * @param size length of encoded map body (not including sizeof(count))
 * @param count total number of elements in the map
 * @return length of data in hdr in octets
 */
static inline int qd_compose_map_header(uint8_t hdr[], uint32_t size, uint32_t count)
{
    if (size + 1 <= UINT8_MAX) {  // + sizeof(count) field
        hdr[0] = QD_AMQP_MAP8;
        hdr[1] = size + 1;
        hdr[2] = count;
        return 3;
    } else {
        hdr[0] = QD_AMQP_MAP32;
        qd_compose_uint32_encode(size + 4, &hdr[1]);
        qd_compose_uint32_encode(count, &hdr[5]);
        return 9;
    }
}


/**
 * Compose the proper header for a list given entry count and data size.
 *
 * The caller must ensure hdr references nine contiguous octets in memory.
 *
 * @param size length of encoded list body (not including sizeof(count))
 * @param count total number of elements in the list
 * @return length of data in hdr in octets
 */
static inline int qd_compose_list_header(uint8_t hdr[], uint32_t size, uint32_t count)
{
    if (size + 1 <= UINT8_MAX) {  // + sizeof(count) field
        hdr[0] = QD_AMQP_LIST8;
        hdr[1] = size + 1;
        hdr[2] = count;
        return 3;
    } else {
        hdr[0] = QD_AMQP_LIST32;
        qd_compose_uint32_encode(size + 4, &hdr[1]);
        qd_compose_uint32_encode(count, &hdr[5]);
        return 9;
    }
}


/**
 * Compose the proper header for a string given its length in octets
 *
 * The caller must ensure hdr references five contiguous octets in memory.
 *
 * @param length of string in octets
 * @return length of data in hdr in octets
 */
static inline int qd_compose_str_header(uint8_t hdr[], uint32_t length)
{
    if (length <= UINT8_MAX) {
        hdr[0] = QD_AMQP_STR8_UTF8;
        hdr[1] = length;
        return 2;
    } else {
        hdr[0] = QD_AMQP_STR32_UTF8;
        qd_compose_uint32_encode(length, &hdr[1]);
        return 5;
    }
}

///@}

#endif

