#ifndef __dispatch_iterator_h__
#define __dispatch_iterator_h__ 1
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

#include <stdint.h>
#include <qpid/dispatch/buffer.h>
#include <qpid/dispatch/iovec.h>

/**@file
 * Iterate over message buffer chains and addresse fields.
 *
 *
 * @defgroup iterator iterator
 *
 * Used to access fields within a message buffer chain, in particular address
 * fields.
 *
 * Iterator shields the user from the fact that the field may be split across
 * one or more physical buffers.
 *
 * @{
 */
typedef struct qd_field_iterator_t qd_field_iterator_t;

/**
 * Type of iterator view. Iterator views allow the code traversing an address
 * field to see a transformed view of the raw field.
 *
 * ITER_VIEW_ALL - No transformation of the raw field data
 *
 * ITER_VIEW_NO_HOST - Remove the scheme and host fields from the view
 *
 *     amqp://host.domain.com:port/node-id/node/specific
 *                                 ^^^^^^^^^^^^^^^^^^^^^
 *     node-id/node/specific
 *     ^^^^^^^^^^^^^^^^^^^^^
 *
 * ITER_VIEW_NODE_ID - Isolate the node identifier from an address
 *
 *     amqp://host.domain.com:port/node-id/node/specific
 *                                ^^^^^^^
 *     node-id/node/specific
 *     ^^^^^^^
 *
 * ITER_VIEW_NODE_SPECIFIC - Isolate node-specific text from an address
 *
 *     amqp://host.domain.com:port/node-id/node/specific
 *                                         ^^^^^^^^^^^^^
 *     node-id/node/specific
 *             ^^^^^^^^^^^^^
 *
 * ITER_VIEW_ADDRESS_HASH - Isolate the hashable part of the address depending on address syntax
 *
 *     amqp:/_local/<local>
 *                 L^^^^^^^
 *     amqp:/_topo/<area>/<router>/<local>
 *                A^^^^^^
 *     amqp:/_topo/<my-area>/<router>/<local>
 *                          R^^^^^^^^
 *     amqp:/_topo/<my_area>/<my-router>/<local>
 *                                      L^^^^^^^
 *     amqp:/_topo/<area>/all/<local>
 *                A^^^^^^
 *     amqp:/_topo/<my-area>/all/<local>
 *                              L^^^^^^^
 *     amqp:/_topo/all/all/<local>
 *                        L^^^^^^^
 *     amqp:/<mobile>
 *         M0^^^^^^^^
 *
 * ITER_VIEW_NODE_HASH - Isolate the hashable part of a router-id, used for headers
 *
 *      <area>/<router>
 *     A^^^^^^
 *
 *      <my_area>/<router>
 *               R^^^^^^^^
 *
 */
typedef enum {
    ITER_VIEW_ALL,
    ITER_VIEW_NO_HOST,
    ITER_VIEW_NODE_ID,
    ITER_VIEW_NODE_SPECIFIC,
    ITER_VIEW_ADDRESS_HASH,
    ITER_VIEW_NODE_HASH
} qd_iterator_view_t;


/**
 * Create an iterator from a null-terminated string.
 *
 * The "text" string must stay intact for the whole life of the iterator.  The iterator
 * does not copy the string, it references it.
 */
qd_field_iterator_t* qd_field_iterator_string(const char         *text,
                                              qd_iterator_view_t  view);


/**
 * Create an iterator from binar data.
 *
 * The "text" string must stay intact for the whole life of the iterator.  The iterator
 * does not copy the data, it references it.
 */
qd_field_iterator_t* qd_field_iterator_binary(const char         *text,
                                              int                 length,
                                              qd_iterator_view_t  view);


/**
 * Create an iterator from a field in a buffer chain

 * The buffer chain must stay intact for the whole life of the iterator.  The iterator
 * does not copy the buffer, it references it.
 */
qd_field_iterator_t *qd_field_iterator_buffer(qd_buffer_t        *buffer,
                                              int                 offset,
                                              int                 length,
                                              qd_iterator_view_t  view);

/**
 * Free an iterator
 */
void qd_field_iterator_free(qd_field_iterator_t *iter);

/**
 * Set the area and router names for the local router.  These are used to match
 * my-area and my-router in address fields.
 */
void qd_field_iterator_set_address(const char *area, const char *router);

/**
 * Reset the iterator to the first octet and set a new view
 */
void qd_field_iterator_reset(qd_field_iterator_t *iter);

void qd_field_iterator_reset_view(qd_field_iterator_t *iter,
                                  qd_iterator_view_t   view);

void qd_field_iterator_set_phase(qd_field_iterator_t *iter, char phase);

/**
 * Override the hash-prefix with a custom character.
 */
void qd_field_iterator_override_prefix(qd_field_iterator_t *iter, char prefix);

/**
 * Return the current octet in the iterator's view and step to the next.
 */
unsigned char qd_field_iterator_octet(qd_field_iterator_t *iter);

/**
 * Return true iff the iterator has no more octets in the view.
 */
int qd_field_iterator_end(qd_field_iterator_t *iter);

/**
 * Return a sub-iterator that equals the supplied iterator except that it
 * starts at the supplied iterator's current position.
 */
qd_field_iterator_t *qd_field_iterator_sub(qd_field_iterator_t *iter, uint32_t length);

void qd_field_iterator_advance(qd_field_iterator_t *iter, uint32_t length);

/**
 * Return the remaining length (in octets) for the iterator.
 *
 * IMPORTANT:  This function returns the limit of the remaining length.
 *             The actual length *may* be less than indicated, but will
 *             never be more.  This function is safe for allocating memory.
 *
 * @param iter A field iterator
 * @return The number of octets remaining in the view (or more)
 */
uint32_t qd_field_iterator_remaining(qd_field_iterator_t *iter);

/**
 * Compare an input string to the iterator's view.  Return true iff they are equal.
 */
int qd_field_iterator_equal(qd_field_iterator_t *iter, const unsigned char *string);

/**
 * Return true iff the string matches the characters at the current location in the view.
 * This function ignores octets beyond the length of the prefix.
 * This function does not alter the position of the iterator if the prefix does not match,
 * if it matches, the prefix is consumed.
 */
int qd_field_iterator_prefix(qd_field_iterator_t *iter, const char *prefix);

/**
 * Return the exact length of the iterator's view.
 */
int qd_field_iterator_length(qd_field_iterator_t *iter);

/**
 * Copy the iterator's view into buffer up to a maximum of n bytes.
 * There is no trailing '\0' added.
 * @return number of bytes copied.
 */
int qd_field_iterator_ncopy(qd_field_iterator_t *iter, unsigned char* buffer, int n);

/**
 * Return a new copy of the iterator's view, with a trailing '\0' added.
 * @return Copy of the view, free with free()
 */
unsigned char *qd_field_iterator_copy(qd_field_iterator_t *iter);

/**
 * Copy the iterator's view into buffer as a null terminated string,
 * up to a maximum of n bytes. Useful for log messages.
 * @return buffer.
 */
char* qd_field_iterator_strncpy(qd_field_iterator_t *iter, char* buffer, int n);

/**
 * Return the contents of this iter into an iovec structure.  This is used in a
 * scatter/gather IO mechanism.  If the iterator spans multiple physical buffers,
 * the iovec structure will contain one pointer per buffer.
 *
 * @param iter A field iterator
 * @return An iovec structure that references the data in the iterator's buffers.
 */
qd_iovec_t *qd_field_iterator_iovec(const qd_field_iterator_t *iter);

/** @} */

#endif
