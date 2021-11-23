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

#include "qpid/dispatch/buffer_field.h"

#include <stdbool.h>
#include <stdint.h>

/**@file
 * Iterate over message buffer chains and address fields.
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

/** \name typedef
 * Type Definitions
 * @{
 */
typedef struct qd_iterator_t qd_iterator_t;

/**
 * Address Hash Prefix Values
 */
#define QD_ITER_HASH_PREFIX_TOPOLOGICAL           'T'
#define QD_ITER_HASH_PREFIX_LOCAL                 'L'
#define QD_ITER_HASH_PREFIX_AREA                  'A'
#define QD_ITER_HASH_PREFIX_ROUTER                'R'
#define QD_ITER_HASH_PREFIX_MOBILE                'M'
#define QD_ITER_HASH_PREFIX_LINKROUTE_ADDR_IN     'C'
#define QD_ITER_HASH_PREFIX_LINKROUTE_ADDR_OUT    'D'
#define QD_ITER_HASH_PREFIX_LINKROUTE_PATTERN_IN  'E'
#define QD_ITER_HASH_PREFIX_LINKROUTE_PATTERN_OUT 'F'
#define QD_ITER_HASH_PREFIX_GLOBAL_PLACEHOLDER    'G'
#define QD_ITER_HASH_PREFIX_EDGE_SUMMARY          'H'

#define QD_ITER_HASH_PHASE_FALLBACK 'F'


/**
 * qd_iterator_view_t
 *
 * Type of iterator view. Iterator views allow the code traversing an address
 * field to see a transformed view of the raw field.
 *
 * ITER_VIEW_ALL - No transformation of the raw field data
 *
 * ITER_VIEW_ADDRESS_NO_HOST - Remove the scheme and host fields from the view
 *
 *     amqp://host.domain.com:port/other/address/text
 *                                 ^^^^^^^^^^^^^^^^^^
 *     amqp:/other/address/text
 *           ^^^^^^^^^^^^^^^^^^
 *     other/address/text
 *     ^^^^^^^^^^^^^^^^^^
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
 *     amqp:/_edge/<my-router>/<local>
 *                            L^^^^^^^
 *     amqp:/_edge/<router>/<local>
 *                H^^^^^^^^          [ interior mode ]
 *         L_edge                    [ edge mode ]
 *
 * ITER_VIEW_NODE_HASH - Isolate the hashable part of a router-id, used for headers
 *
 *      <area>/<router>
 *     A^^^^^^
 *
 *      <my_area>/<router>
 *               R^^^^^^^^
 *
 * ITER_VIEW_ADDRESS_WITH_SPACE
 *    Same as ADDRESS_HASH but:
 *      - Does not show the prefix/phase
 *      - Does not hash-ize local and topological addresses
 *      - Does not show namespace on local and topological addresses
 *
 */
typedef enum {
    ITER_VIEW_ALL,
    ITER_VIEW_ADDRESS_NO_HOST,
    ITER_VIEW_ADDRESS_HASH,
    ITER_VIEW_NODE_HASH,
    ITER_VIEW_ADDRESS_WITH_SPACE
} qd_iterator_view_t;


/** @} */
/** \name global
 * Global Methods
 * @{
 */

/**
 * Clean up any internal state in the iterator library.  This must be called at
 * shutdown after all iterators have been released.
 */
void qd_iterator_finalize(void);

/**
 * Set the area and router names for the local router.  These are used to match
 * my-area and my-router in address fields.  These settings are global and used
 * for all iterators in the process.
 *
 * @param area The name of the router's area
 * @param router The identifier of the router in the area
 */
void qd_iterator_set_address(bool edge_mode, const char *area, const char *router);

/** @} */
/** \name lifecycle
 * Methods to control iterator lifecycle
 * @{
 */

/**
 * Create an iterator for a null-terminated string.
 *
 * The "text" string must stay intact for the whole life of the iterator.  The iterator
 * does not copy the string, it references it.
 *
 * @param text A null-terminated character string
 * @param view The view for the iterator
 * @return A newly allocated iterator that references the text string.
 */
qd_iterator_t* qd_iterator_string(const char         *text,
                                  qd_iterator_view_t  view);

/**
 * Create an iterator from binary data.
 *
 * The "text" string must stay intact for the whole life of the iterator.  The iterator
 * does not copy the data, it references it.
 *
 * @param text Pointer to the first octet in an octet string
 * @param length Number of octets in the contiguous octet string
 * @param view The view for the iterator
 * @return A newly allocated iterator that references the octet string.
 */
qd_iterator_t* qd_iterator_binary(const char         *text,
                                  int                 length,
                                  qd_iterator_view_t  view);

/**
 * Create an iterator from a field in a buffer chain
 *
 * The buffer chain must stay intact for the whole life of the iterator.  The iterator
 * does not copy the buffer, it references it.
 *
 * @param buffer Pointer to the first buffer in the buffer chain
 * @param offset The offset in the first buffer where the first octet of the field is
 * @param length Number of octets in the field
 * @param view The view for the iterator
 * @return A newly allocated iterator that references the field.
 */
qd_iterator_t *qd_iterator_buffer(qd_buffer_t        *buffer,
                                  int                 offset,
                                  int                 length,
                                  qd_iterator_view_t  view);

/**
 * Free an allocated iterator
 *
 * @param iter An allocated iterator that is no longer to be used
 */
void qd_iterator_free(qd_iterator_t *iter);


/** @} */
/** \name normal
 * Methods to manipulate and traverse an iterator
 * @{
 */

/**
 * Reset the iterator to the first octet.
 *
 * @param iter Pointer to an iterator to be reset
 */
void qd_iterator_reset(qd_iterator_t *iter);

/**
 * Reset the iterator and set the view.  If the iterator was trimmed, clear the trim state.
 *
 * @param iter Pointer to an iterator to be reset
 * @param view The new view for the iterator
 */
void qd_iterator_reset_view(qd_iterator_t      *iter,
                            qd_iterator_view_t  view);

/**
 * Return the view for the iterator
 *
 * @param iter Pointer to an iterator
 * @return The view for the iterator
 */
qd_iterator_view_t qd_iterator_get_view(const qd_iterator_t *iter);

/**
 * Trims octets from both ends of the iterator's view by reducing the length of the view and by
 * resetting the base of the view to the current location.
 *
 * Note that an iterator may be repeatedly trimmed, but the trimming must always reduce
 * the size of the view.  Trimming will never increase the size of the view.  To re-trim
 * with a bigger size, qd_iterator_reset_view must be called to clear the trimmed state.
 *
 * @param iter The iterator whose length should be trimmed
 * @param length The length of the trimmed field.  If greater than or equal to the current length,
 *               then there shall be no effect.
 */
void qd_iterator_trim_view(qd_iterator_t *iter, int length);

/**
 * Return the current octet in the iterator's view and step to the next.
 */
unsigned char qd_iterator_octet(qd_iterator_t *iter);

/**
 * Return true iff the iterator has no more octets in the view.
 */
bool qd_iterator_end(const qd_iterator_t *iter);

/**
 * Return a sub-iterator that equals the supplied iterator except that it
 * starts at the supplied iterator's current position.
 */
qd_iterator_t *qd_iterator_sub(const qd_iterator_t *iter, uint32_t length);

/**
 * Move the iterator's cursor forward up to length bytes
 */
void qd_iterator_advance(qd_iterator_t *iter, uint32_t length);

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
uint32_t qd_iterator_remaining(const qd_iterator_t *iter);

/**
 * Return the exact length of the iterator's view.
 */
int qd_iterator_length(const qd_iterator_t *iter);

/**
 * Compare an input string to the iterator's view.  Return true iff they are equal.
 */
bool qd_iterator_equal(qd_iterator_t *iter, const unsigned char *string);

/**
 * Return true iff the string matches the characters at the current location in the view.
 * This function ignores octets beyond the length of the prefix.
 * This function does not alter the position of the iterator if the prefix does not match,
 * if it matches, the prefix is consumed.
 */
bool qd_iterator_prefix(qd_iterator_t *iter, const char *prefix);

/**
 * Copy the iterator's view into buffer up to a maximum of n bytes.  View is
 * reset to the beginning and cursor is advanced by the number of bytes
 * copied. There is no trailing '\0' added.
 *
 * @return number of bytes copied.
 */
size_t qd_iterator_ncopy(qd_iterator_t *iter, uint8_t *buffer, size_t n);

/**
 * Copy the iterator's view into buffer up to a maximum of n octets.  Unlike
 * qd_iterator_ncopy the view is not reset before the copy: copying begins at
 * the current cursor position. The cursor is advanced by the number of bytes
 * copied. There is no trailing '\0' added.
 *
 * @return number of bytes copied.
 */
size_t qd_iterator_ncopy_octets(qd_iterator_t *iter, uint8_t *buffer, size_t n);

/**
 * Return a new copy of the iterator's view, with a trailing '\0' added.  The
 * cursor is advanced to the end of the view.
 * @return Copy of the view, free with free()
 */
unsigned char *qd_iterator_copy(qd_iterator_t *iter);

/**
 * A version of qd_iterator_copy that does NOT modify the iterator
 */
unsigned char *qd_iterator_copy_const(const qd_iterator_t *iter);

uint8_t qd_iterator_uint8(qd_iterator_t *iter);

/**
 * Return a new iterator that is a duplicate of the original iterator, referring
 * to the same base data.  If the input iterator pointer is NULL, the duplicate
 * will also be NULL (i.e. no new iterator will be created).
 * @param iter Input iterator
 * @return Pointer to a new, identical iterator referring to the same data.
 */
qd_iterator_t *qd_iterator_dup(const qd_iterator_t *iter);

/**
 * Copy the iterator's view into buffer as a null terminated string,
 * up to a maximum of n bytes. Cursor is advanced by the number of bytes
 * copied.  Useful for log messages.
 * @return buffer.
 */
char* qd_iterator_strncpy(qd_iterator_t *iter, char* buffer, int n);

/** @} */
/** \name annotation
 * Methods to modify the view annotations of an iterator
 * @{
 */

/**
 * Set the phase character to annotate a mobile address view.
 *
 * @param iter Pointer to an iterator
 * @param phase A character used to annotate a mobile address view
 */
void qd_iterator_annotate_phase(qd_iterator_t *iter, char phase);

/**
 * Override the prefix character for a mobile address view.
 *
 * @param iter Pointer to an iterator
 * @param prefix A character to use as the prefix of a mobile address view
 */
void qd_iterator_annotate_prefix(qd_iterator_t *iter, char prefix);

/**
 * Annotate a mobile address view with a tenant namespace.
 *
 * Note that the space string is not copied into the iterator.  It must remain in-place
 * and unchanged for the lifetime of the iterator.
 *
 * @param iter Pointer to an iterator
 * @param space Pointer to the first character of a character string representing a namespace
 * @param space_len The number of characters in the space string
 */
void qd_iterator_annotate_space(qd_iterator_t *iter, const char* space, int space_len);


/** @} */
/** \name hash
 * Methods to calculate hash values for iterator views
 *
 * All hashing functions use the djb2 algorithm (http://www.cse.yorku.ca/~oz/hash.html).
 * @{
 */

/**
 * Generate the hash of the view of the iterator.
 *
 * @param iter A field iterator
 * @return The hash value of the iterator's view
 */
uint32_t qd_iterator_hash_view(qd_iterator_t *iter);

/**
 * Generate a series of hash values for a segmented view.  For example, consider the following view:
 *
 *    "M0policy/org.apache.qpid"
 *
 * The hash values computed will be for the following strings:
 *
 *    "M0policy/org.apache.qpid"
 *    "M0policy/org.apache"
 *    "M0policy/org"
 *    "M0policy"
 *
 * These hash values and the sub-views associated with them shall be stored in the iterator.
 *
 * @param iter An address iterator
 */
void qd_iterator_hash_view_segments(qd_iterator_t *iter);

/**
 * Iterate over the segment hash values pre-computed using qd_iterator_hash_view_segments.
 *
 * This function returns the longest remaining segment hash, removes the hash from the set of
 * stored segment values, and adjusts the view so that it includes only the subset of the view
 * associated with the hash value.
 *
 * @param iter A field iterator
 * @param hash (output) Hash value for the next segment
 * @return True iff there is another segment hash to be compared
 */
bool qd_iterator_next_segment(qd_iterator_t *iter, uint32_t *hash);

/**
 * Get an iterator's cursor details.
 * Exposes iter's buffer, cursor, and remaining values.
 *
 * @param iter iter that still has data in its view.
 * @return a copy of the iter's view cursor
 */
qd_buffer_field_t qd_iterator_get_view_cursor(const qd_iterator_t *iter);

/**
 * Construct an iterator from a buffer field
 */
qd_iterator_t *qd_iterator_buffer_field(const qd_buffer_field_t *bfield,
                                        qd_iterator_view_t view);

/** @} */
/** @} */

#endif
