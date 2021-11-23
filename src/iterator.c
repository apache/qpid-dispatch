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

#include "qpid/dispatch/iterator.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/hash.h"
#include "buffer_field_api.h"

#include <stdio.h>
#include <string.h>


typedef enum {
    MODE_TO_END,
    MODE_TO_SLASH
} parse_mode_t;

typedef enum {
    STATE_AT_PREFIX,
    STATE_AT_PHASE,
    STATE_IN_SPACE,
    STATE_IN_BODY
} view_state_t;

typedef struct qd_hash_segment_t {
    DEQ_LINKS(struct qd_hash_segment_t);
    uint32_t hash;           //The hash of the segment
    uint32_t segment_length; //The length of the segment
} qd_hash_segment_t;

DEQ_DECLARE(qd_hash_segment_t, qd_hash_segment_list_t);
ALLOC_DECLARE(qd_hash_segment_t);
ALLOC_DEFINE(qd_hash_segment_t);

struct qd_iterator_t {
    qd_buffer_field_t       start_pointer;      // Pointer to the raw data
    qd_buffer_field_t       view_start_pointer; // Pointer to the start of the view
    qd_buffer_field_t       view_pointer;       // Pointer to the remaining view
    qd_iterator_view_t      view;
    int                     annotation_length;
    int                     annotation_remaining;
    qd_hash_segment_list_t  hash_segments;
    parse_mode_t            mode;
    view_state_t            state;
    unsigned char           prefix;
    unsigned char           prefix_override;
    unsigned char           phase;
    const char             *space;
    int                     space_length;
    int                     space_cursor;
    bool                    view_space;
};

ALLOC_DECLARE(qd_iterator_t);
ALLOC_DEFINE(qd_iterator_t);

typedef enum {
    STATE_START,
    STATE_SLASH_LEFT,
    STATE_SKIPPING_TO_NEXT_SLASH,
    STATE_SCANNING,
    STATE_COLON,
    STATE_COLON_SLASH,
    STATE_AT_NODE_ID
} state_t;


static bool  edge_mode = false;
static char *my_area   = 0;
static char *my_router = 0;

static const char    *SEPARATORS = "./";


// returns true if the current iterator view has no transformations and is
// dealing with raw field data (e.g. past the address prefix for address
// iterators)
//
static inline int in_field_data(const qd_iterator_t *iter)
{
    return iter->view == ITER_VIEW_ALL || (iter->state == STATE_IN_BODY && iter->mode == MODE_TO_END);
}


static inline uint32_t iterator_remaining(const qd_iterator_t *iter)
{
    return iter->annotation_remaining + iter->view_pointer.remaining;
}


static inline bool iterator_at_end(const qd_iterator_t *iter)
{
    return iterator_remaining(iter) == 0;
}


static inline int iterator_length(const qd_iterator_t *iter)
{
    return iter->annotation_length + iter->view_start_pointer.remaining;
}


static void set_to_edge_connection(qd_iterator_t *iter)
{
    static const char *EDGE_CONNECTION = "_edge";
    iter->prefix = QD_ITER_HASH_PREFIX_LOCAL;
    iter->state  = STATE_AT_PREFIX;
    iter->view_start_pointer.buffer    = 0;
    iter->view_start_pointer.cursor    = (unsigned char*) EDGE_CONNECTION;
    iter->view_start_pointer.remaining = (int) strlen(EDGE_CONNECTION);
    iter->view_pointer = iter->view_start_pointer;
}


static void parse_address_view(qd_iterator_t *iter)
{
    //
    // This function starts with an iterator view that is identical to
    // ITER_VIEW_ADDRESS_NO_HOST.  We will now further refine the view
    // in order to aid the router in looking up addresses.
    //

    qd_buffer_field_t save_pointer = iter->view_pointer;
    iter->annotation_length = 1;

    if (iter->prefix_override == '\0' && qd_iterator_prefix(iter, "_")) {
        if (iter->view == ITER_VIEW_ADDRESS_WITH_SPACE) {
            iter->view_pointer      = save_pointer;
            iter->view_space        = false;
            iter->annotation_length = 0;
            return;
        }

        if (qd_iterator_prefix(iter, "local/")) {
            iter->prefix = QD_ITER_HASH_PREFIX_LOCAL;
            iter->state  = STATE_AT_PREFIX;
            return;
        }

        if (qd_iterator_prefix(iter, "topo/")) {
            assert(my_area && my_router);  // ensure qd_iterator_set_address called!
            if (qd_iterator_prefix(iter, "all/") || qd_iterator_prefix(iter, my_area)) {
                if (qd_iterator_prefix(iter, "all/")) {
                    iter->prefix = QD_ITER_HASH_PREFIX_TOPOLOGICAL;
                    iter->state  = STATE_AT_PREFIX;
                    return;
                } else if (qd_iterator_prefix(iter, my_router)) {
                    iter->prefix = QD_ITER_HASH_PREFIX_LOCAL;
                    iter->state  = STATE_AT_PREFIX;
                    return;
                }

                if (edge_mode)
                    set_to_edge_connection(iter);
                else {
                    iter->prefix = QD_ITER_HASH_PREFIX_ROUTER;
                    iter->state  = STATE_AT_PREFIX;
                    iter->mode   = MODE_TO_SLASH;
                }
                return;
            }

            if (edge_mode)
                set_to_edge_connection(iter);
            else {
                iter->prefix = QD_ITER_HASH_PREFIX_AREA;
                iter->state  = STATE_AT_PREFIX;
                iter->mode   = MODE_TO_SLASH;
            }
            return;
        }

        if (qd_iterator_prefix(iter, "edge/")) {
            if (qd_iterator_prefix(iter, my_router)) {
                iter->prefix = QD_ITER_HASH_PREFIX_LOCAL;
                iter->state  = STATE_AT_PREFIX;
                return;
            }

            if (edge_mode) {
                set_to_edge_connection(iter);
                return;
            } else {
                iter->prefix = QD_ITER_HASH_PREFIX_EDGE_SUMMARY;
                iter->state  = STATE_AT_PREFIX;
                iter->mode   = MODE_TO_SLASH;
                return;
            }
        }

        iter->view_pointer  = save_pointer;
    }

    iter->prefix            = iter->prefix_override ? iter->prefix_override : QD_ITER_HASH_PREFIX_MOBILE;
    iter->state             = STATE_AT_PREFIX;
    iter->view_space        = true;
    iter->annotation_length = iter->space_length + (iter->prefix == QD_ITER_HASH_PREFIX_MOBILE ? 2 : 1);
}


static void adjust_address_with_space(qd_iterator_t *iter)
{
    //
    // Convert an ADDRESS_HASH view to an ADDRESS_WITH_SPACE view
    //
    if (iter->view_space) {
        iter->annotation_length -= iter->prefix == QD_ITER_HASH_PREFIX_MOBILE ? 2 : 1;
        iter->state = iter->space ? STATE_IN_SPACE : STATE_IN_BODY;
    } else {
        iter->annotation_length = 0;
        iter->state = STATE_IN_BODY;
    }
}


static void parse_node_view(qd_iterator_t *iter)
{
    //
    // This function starts with an iterator view that is identical to
    // ITER_VIEW_ADDRESS_NO_HOST.  We will now further refine the view in order
    // to aid the router in looking up nodes.
    //

    iter->annotation_length = 1;

    if (qd_iterator_prefix(iter, my_area)) {
        iter->prefix = QD_ITER_HASH_PREFIX_ROUTER;
        iter->state  = STATE_AT_PREFIX;
        iter->mode   = MODE_TO_END;
        return;
    }

    iter->prefix = QD_ITER_HASH_PREFIX_AREA;
    iter->state  = STATE_AT_PREFIX;
    iter->mode   = MODE_TO_SLASH;
}


void qd_iterator_remove_trailing_separator(qd_iterator_t *iter)
{
    // Save the iterator's pointer so we can apply it back before returning from this function.
    qd_buffer_field_t save_pointer = iter->view_pointer;

    char current_octet = 0;
    while (!iterator_at_end(iter)) {
        current_octet = qd_iterator_octet(iter);
    }

    // We have the last octet in current_octet
    iter->view_pointer = save_pointer;
    if (current_octet && strrchr(SEPARATORS, (int) current_octet))
        iter->view_pointer.remaining--;
}


static void view_initialize(qd_iterator_t *iter)
{
    //
    // The default behavior is for the view to *not* have a prefix.
    // We'll add one if it's needed later.
    //
    iter->state                = STATE_IN_BODY;
    iter->prefix               = '\0';
    iter->mode                 = MODE_TO_END;
    iter->annotation_length    = 0;
    iter->annotation_remaining = 0;
    iter->view_space           = false;

    if (iter->view == ITER_VIEW_ALL)
        return;

    //
    // ITER_VIEW_NODE_HASH has no scheme or leading slash - see iterator.h
    // Do not walk the entire iterator trying to find one.
    //
    if (iter->view == ITER_VIEW_NODE_HASH) {
        parse_node_view(iter);
        return;
    }

    //
    // Advance to the node-id.
    //
    state_t               state = STATE_START;
    unsigned int          octet;
    qd_buffer_field_t save_pointer = {0,0,0};

    while (!iterator_at_end(iter) && state != STATE_AT_NODE_ID) {
        octet = qd_iterator_octet(iter);

        switch (state) {
        case STATE_START :
            if (octet == '/') {
                state = STATE_SLASH_LEFT;
                save_pointer = iter->view_pointer;
            } else
                state = STATE_SCANNING;
            break;

        case STATE_SLASH_LEFT :
            if (octet == '/')
                state = STATE_SKIPPING_TO_NEXT_SLASH;
            else {
                state = STATE_AT_NODE_ID;
                iter->view_pointer = save_pointer;
            }
            break;

        case STATE_SKIPPING_TO_NEXT_SLASH :
            if (octet == '/')
                state = STATE_AT_NODE_ID;
            break;

        case STATE_SCANNING :
            if (octet == ':')
                state = STATE_COLON;
            break;

        case STATE_COLON :
            if (octet == '/') {
                state = STATE_COLON_SLASH;
                save_pointer = iter->view_pointer;
            } else
                state = STATE_SCANNING;
            break;

        case STATE_COLON_SLASH :
            if (octet == '/')
                state = STATE_SKIPPING_TO_NEXT_SLASH;
            else {
                state = STATE_AT_NODE_ID;
                iter->view_pointer = save_pointer;
            }
            break;

        case STATE_AT_NODE_ID :
            break;
        }
    }

    if (state != STATE_AT_NODE_ID){
        //
        // The address string was relative, not absolute.  The node-id
        // is at the beginning of the string.
        //
        iter->view_pointer = iter->start_pointer;
    }

    //
    // Cursor is now on the first octet of the node-id
    //
    if (iter->view == ITER_VIEW_ADDRESS_NO_HOST)
        return;

    if (iter->view == ITER_VIEW_ADDRESS_HASH || iter->view == ITER_VIEW_ADDRESS_WITH_SPACE) {
        qd_iterator_remove_trailing_separator(iter);
        parse_address_view(iter);
        if (iter->view == ITER_VIEW_ADDRESS_WITH_SPACE)
            adjust_address_with_space(iter);
        return;
    }
}


// data copy for optimized for simple (stateless) field data (e.g. past the
// prefix of an address iterator)
//
static inline int iterator_field_ncopy(qd_iterator_t *iter, unsigned char *buffer, int n)
{
    assert(in_field_data(iter));

    if (iter->view_pointer.buffer) {
        return qd_buffer_field_ncopy(&iter->view_pointer, (uint8_t*)buffer, n);

    } else {  // string or binary array
        int count = MIN(n, iter->view_pointer.remaining);
        memcpy(buffer, iter->view_pointer.cursor, count);
        iter->view_pointer.cursor += count;
        iter->view_pointer.remaining -= count;
        return count;
    }
}


// cursor move optimized for simple (stateless) field data
//
static inline void iterator_field_move_cursor(qd_iterator_t *iter, uint32_t length)
{
    // Only safe to call this help method if the cursor is parsing the data,
    // i.e. if iter is an address iterator, the cursor must be 'past' the
    // prefix
    assert(in_field_data(iter));

    if (iter->view_pointer.buffer) {
        qd_buffer_field_advance(&iter->view_pointer, length);

    } else {    // string/binary data
        uint32_t count = MIN(length, iter->view_pointer.remaining);
        iter->view_pointer.cursor    += count;
        iter->view_pointer.remaining -= count;
    }
}


// optimized iterator compare for simple field data. Note: the view is advanced
// when equal.
//
static inline bool iterator_field_equal(qd_iterator_t *iter, const unsigned char *buffer, size_t count)
{
    // Only safe to call this help method if the cursor is parsing the data,
    // i.e. if iter is an address iterator, the cursor must be 'past' the
    // prefix
    assert(in_field_data(iter));

    // ensure at least count octets available
    if (iter->view_pointer.remaining < count)
        return false;

    if (iter->view_pointer.buffer) {
        return qd_buffer_field_equal(&iter->view_pointer, (const uint8_t*) buffer, count);

    } else {  // string or binary array

        if (memcmp(buffer, iter->view_pointer.cursor, count) != 0) {
            return false;
        }

        iter->view_pointer.cursor    += count;
        iter->view_pointer.remaining -= count;
    }

    return true;
}


// fast view-agnostic copy: copy out up to n bytes from the current location in
// the iterator view, advance cursor
//
static inline size_t iterator_view_copy(qd_iterator_t *iter, uint8_t *buffer, size_t n)
{
    int i = 0;

    assert(iter);

    while (i < n && !iterator_at_end(iter)) {
        if (!in_field_data(iter)) {
            buffer[i++] = qd_iterator_octet(iter);
        } else {
            i += iterator_field_ncopy(iter, &buffer[i], n - i);
            break;
        }
    }
    return i;
}

static void qd_iterator_free_hash_segments(qd_iterator_t *iter)
{
    qd_hash_segment_t *seg = DEQ_HEAD(iter->hash_segments);
    while (seg) {
        DEQ_REMOVE_HEAD(iter->hash_segments);
        free_qd_hash_segment_t(seg);
        seg = DEQ_HEAD(iter->hash_segments);
    }
}


void qd_iterator_set_address(bool _edge_mode, const char *area, const char *router)
{
    const size_t area_size   = strlen(area);
    const size_t router_size = strlen(router);

    edge_mode = _edge_mode;

    free(my_area);
    my_area = qd_malloc(area_size + 2);  // include trailing '\'
    sprintf(my_area, "%s/", area);

    free(my_router);
    my_router = qd_malloc(router_size + 2);
    sprintf(my_router, "%s/", router);
}


qd_iterator_t* qd_iterator_string(const char *text, qd_iterator_view_t view)
{
    qd_iterator_t *iter = new_qd_iterator_t();
    if (!iter)
        return 0;

    ZERO(iter);
    iter->start_pointer.cursor    = (unsigned char*) text;
    iter->start_pointer.remaining = strlen(text);
    iter->phase                   = '0';

    qd_iterator_reset_view(iter, view);

    return iter;
}


qd_iterator_t* qd_iterator_binary(const char *text, int length, qd_iterator_view_t view)
{
    qd_iterator_t *iter = new_qd_iterator_t();
    if (!iter)
        return 0;

    ZERO(iter);
    iter->start_pointer.cursor    = (unsigned char*) text;
    iter->start_pointer.remaining = length;
    iter->phase                   = '0';

    qd_iterator_reset_view(iter, view);

    return iter;
}


qd_iterator_t *qd_iterator_buffer(qd_buffer_t *buffer, int offset, int length, qd_iterator_view_t view)
{
    qd_iterator_t *iter = new_qd_iterator_t();
    if (!iter)
        return 0;

    ZERO(iter);
    iter->start_pointer = qd_buffer_field(buffer, qd_buffer_base(buffer) + offset, length);
    iter->phase         = '0';

    qd_iterator_reset_view(iter, view);

    return iter;
}


void qd_iterator_free(qd_iterator_t *iter)
{
    if (!iter)
        return;

    qd_iterator_free_hash_segments(iter);
    free_qd_iterator_t(iter);
}


void qd_iterator_reset(qd_iterator_t *iter)
{
    if (iter) {
        iter->view_pointer         = iter->view_start_pointer;
        iter->annotation_remaining = iter->annotation_length;

        if (iter->view == ITER_VIEW_ADDRESS_WITH_SPACE) {
            if (iter->space && iter->view_space) {
                iter->state = STATE_IN_SPACE;
                iter->space_cursor = 0;
            }
        } else
            iter->state = iter->prefix ? STATE_AT_PREFIX : STATE_IN_BODY;
    }
}


void qd_iterator_reset_view(qd_iterator_t *iter, qd_iterator_view_t view)
{
    if (iter) {
        iter->view_pointer = iter->start_pointer;
        iter->view         = view;
        view_initialize(iter);
        iter->view_start_pointer   = iter->view_pointer;
        iter->annotation_remaining = iter->annotation_length;
    }
}


qd_iterator_view_t qd_iterator_get_view(const qd_iterator_t *iter)
{
    return iter ? iter->view : ITER_VIEW_ALL;
}


void qd_iterator_annotate_phase(qd_iterator_t *iter, char phase)
{
    if (iter)
        iter->phase = phase;
}


void qd_iterator_trim_view(qd_iterator_t *iter, int length)
{
    if (!iter)
        return;

    iter->view_start_pointer = iter->view_pointer;
    int view_length = iterator_length(iter);
    if (view_length > length) {
        if (iter->annotation_length > length) {
            iter->annotation_length            = length;
            iter->annotation_remaining         = length;
            iter->view_start_pointer.remaining = 0;
        } else
            iter->view_start_pointer.remaining -= view_length - length;
        iter->view_pointer = iter->view_start_pointer;
    }
}


void qd_iterator_annotate_prefix(qd_iterator_t *iter, char prefix)
{
    if (iter) {
        iter->prefix_override = prefix;
        qd_iterator_reset_view(iter, iter->view);
    }
}


void qd_iterator_annotate_space(qd_iterator_t *iter, const char* space, int space_length)
{
    if (iter) {
        iter->space        = space;
        iter->space_length = space_length;
        if      (iter->view == ITER_VIEW_ADDRESS_HASH)
            iter->annotation_length = (iter->view_space ? space_length : 0) + (iter->prefix == QD_ITER_HASH_PREFIX_MOBILE ? 2 : 1);
        else if (iter->view == ITER_VIEW_ADDRESS_WITH_SPACE) {
            if (iter->view_space)
                iter->annotation_length = space_length;
        }
    }
}


unsigned char qd_iterator_octet(qd_iterator_t *iter)
{
    if (!iter)
        return 0;

    if (iter->state == STATE_IN_BODY) {
        if (iter->view_pointer.remaining == 0)
            return (unsigned char) 0;

        unsigned char result;
        if (iter->view_pointer.buffer) {
            uint8_t octet;
            (void)qd_buffer_field_octet(&iter->view_pointer, &octet);
            result = (unsigned char) octet;
        } else { // string or binary array
            result = *(iter->view_pointer.cursor)++;
            --iter->view_pointer.remaining;
        }

        if (iter->mode == MODE_TO_SLASH && iter->view_pointer.remaining && *(iter->view_pointer.cursor) == '/') {
            iter->view_pointer.remaining = 0;
        }

        return result;
    }

    if (iter->state == STATE_AT_PREFIX) {
        iter->state = iter->prefix == QD_ITER_HASH_PREFIX_MOBILE ? STATE_AT_PHASE : (iter->view_space && iter->space) ? STATE_IN_SPACE : STATE_IN_BODY;
        iter->space_cursor = 0;
        iter->annotation_remaining--;
        return iter->prefix;
    }

    if (iter->state == STATE_AT_PHASE) {
        iter->state = (iter->view_space && iter->space) ? STATE_IN_SPACE : STATE_IN_BODY;
        iter->space_cursor = 0;
        iter->annotation_remaining--;
        return iter->phase;
    }

    if (iter->state == STATE_IN_SPACE) {
        if (iter->space_cursor == iter->space_length - 1) {
            iter->state = STATE_IN_BODY;
            assert(iter->annotation_remaining == 1);
        }
        iter->annotation_remaining--;
        return iter->space[iter->space_cursor++];
    }

    assert(false);  // all states checked - cannot get here
    return 0;
}


bool qd_iterator_end(const qd_iterator_t *iter)
{
    return iter ? iterator_at_end(iter) : true;
}


qd_iterator_t *qd_iterator_sub(const qd_iterator_t *iter, uint32_t length)
{
    if (!iter)
        return 0;

    qd_iterator_t *sub = new_qd_iterator_t();
    if (!sub)
        return 0;

    ZERO(sub);
    sub->start_pointer           = iter->view_pointer;
    sub->start_pointer.remaining = length;
    sub->view_start_pointer      = sub->start_pointer;
    sub->view_pointer            = sub->start_pointer;
    sub->view                    = iter->view;
    sub->mode                    = iter->mode;
    sub->state                   = STATE_IN_BODY;
    sub->phase                   = '0';

    return sub;
}


void qd_iterator_advance(qd_iterator_t *iter, uint32_t length)
{
    if (!iter)
        return;

    while (length > 0 && !iterator_at_end(iter)) {
        if (!in_field_data(iter)) {
            qd_iterator_octet(iter);
            length--;
        } else {
            iterator_field_move_cursor(iter, length);
            break;
        }
    }
}


uint32_t qd_iterator_remaining(const qd_iterator_t *iter)
{
    return iter ? iterator_remaining(iter) : 0;
}


bool qd_iterator_equal(qd_iterator_t *iter, const unsigned char *string)
{
    if (!iter)
        return false;

    qd_iterator_reset(iter);

    while (!in_field_data(iter) &&
           *string &&
           !iterator_at_end(iter)) {
        if (*string != qd_iterator_octet(iter)) {
            qd_iterator_reset(iter);
            return false;
        }
        ++string;
    }

    if (*string == 0 && iterator_at_end(iter)) {
        qd_iterator_reset(iter);
        return true;
    }

    // otherwise there raw field data.  Check for a match on the field and be sure
    // there is not anything left over in the iterator after the string.
    bool match = (in_field_data(iter)
                  && iterator_field_equal(iter, string, strlen((char *)string))
                  && iterator_at_end(iter));

    qd_iterator_reset(iter);
    return match;
}


bool qd_iterator_prefix(qd_iterator_t *iter, const char *prefix)
{
    if (!iter)
        return false;

    qd_buffer_field_t save_pointer = iter->view_pointer;
    unsigned char *c               = (unsigned char*) prefix;

    while(*c) {
        if (*c != qd_iterator_octet(iter))
            break;
        c++;
    }

    if (*c) {
        iter->view_pointer = save_pointer;
        return false;
    }

    return true;
}


int qd_iterator_length(const qd_iterator_t *iter)
{
    return iter ? iterator_length(iter) : 0;
}


size_t qd_iterator_ncopy(qd_iterator_t *iter, unsigned char* buffer, size_t n)
{
    if (!iter)
        return 0;

    qd_iterator_reset(iter);
    return iterator_view_copy(iter, (uint8_t *) buffer, n);
}


size_t qd_iterator_ncopy_octets(qd_iterator_t *iter, uint8_t *buffer, size_t n)
{
    if (!iter)
        return 0;
    return iterator_view_copy(iter, (uint8_t *) buffer, n);
}


char* qd_iterator_strncpy(qd_iterator_t *iter, char* buffer, int n)
{
    int i = qd_iterator_ncopy(iter, (unsigned char*) buffer, n-1);
    buffer[i] = '\0';
    return buffer;
}


uint8_t qd_iterator_uint8(qd_iterator_t *iter ) {
    qd_iterator_reset(iter);
    if (iterator_at_end(iter))
        return 0;
    return (uint8_t) qd_iterator_octet(iter);
}


unsigned char *qd_iterator_copy(qd_iterator_t *iter)
{
    if (!iter)
        return 0;

    int length = iterator_length(iter);
    unsigned char *copy = malloc(length+1);
    int i = qd_iterator_ncopy(iter, copy, length+1);
    copy[i] = '\0';
    return copy;
}


unsigned char *qd_iterator_copy_const(const qd_iterator_t *iter)
{
    if (!iter)
        return 0;

    qd_iterator_t temp = *iter;
    DEQ_INIT(temp.hash_segments);
    return qd_iterator_copy(&temp);
}


qd_iterator_t *qd_iterator_dup(const qd_iterator_t *iter)
{
    if (!iter)
        return 0;

    qd_iterator_t *dup = new_qd_iterator_t();
    if (dup) {
        *dup = *iter;
        // drop any references to the hash segments to avoid potential double
        // free
        DEQ_INIT(dup->hash_segments);
    }
    return dup;
}


/**
 * Creates and returns a new qd_hash_segment_t and initializes it.
 */
static qd_hash_segment_t *qd_iterator_hash_segment(void)
{
    qd_hash_segment_t *hash_segment = new_qd_hash_segment_t();
    DEQ_ITEM_INIT(hash_segment);
    hash_segment->hash           = 0;
    hash_segment->segment_length = 0;
    return hash_segment;
}


/**
 * Create a new hash segment and insert it at the end of the linked list
 */
static void qd_insert_hash_segment(qd_iterator_t *iter, uint32_t *hash, int segment_length)
{
    qd_hash_segment_t *hash_segment = qd_iterator_hash_segment();

    // While storing the segment, don't include the hash of the separator in the segment but do include it in the overall hash.
    hash_segment->hash = *hash;

    hash_segment->segment_length = segment_length;
    DEQ_INSERT_TAIL(iter->hash_segments, hash_segment);
}


uint32_t qd_iterator_hash_view(qd_iterator_t *iter)
{
    uint32_t hash = HASH_INIT;
    uint8_t buffer[64];

    qd_iterator_reset(iter);
    while (!iterator_at_end(iter)) {
        size_t count = iterator_view_copy(iter, buffer, sizeof(buffer));
        for (int i = 0; i < count; ++i)
            hash = HASH_COMPUTE(hash, buffer[i]);
    }

    return hash;
}


void qd_iterator_hash_view_segments(qd_iterator_t *iter)
{
    if (!iter)
        return;

    // Reset the pointers in the iterator
    qd_iterator_reset(iter);
    uint32_t hash = HASH_INIT;
    char octet;
    int segment_length=0;

    qd_iterator_free_hash_segments(iter);

    while (!iterator_at_end(iter)) {
        // Get the octet at which the iterator is currently pointing to.
        octet = qd_iterator_octet(iter);
        segment_length += 1;

        if (strrchr(SEPARATORS, (int) octet)) {
            qd_insert_hash_segment(iter, &hash, segment_length-1);
        }

        hash = HASH_COMPUTE(hash, octet);
    }

    // Segments should never end with a separator. see view_initialize which in turn calls
    // qd_iterator_remove_trailing_separator
    // Insert the last segment which was not inserted in the previous while loop
    qd_insert_hash_segment(iter, &hash, segment_length);

    // Return the pointers in the iterator back to the original state before returning from this function.
    qd_iterator_reset(iter);
}


bool qd_iterator_next_segment(qd_iterator_t *iter, uint32_t *hash)
{
    qd_hash_segment_t *hash_segment = DEQ_TAIL(iter->hash_segments);
    if (!hash_segment)
        return false;

    *hash = hash_segment->hash;
    qd_iterator_trim_view(iter, hash_segment->segment_length);

    DEQ_REMOVE_TAIL(iter->hash_segments);

    free_qd_hash_segment_t(hash_segment);

    return true;
}


qd_buffer_field_t qd_iterator_get_view_cursor(const qd_iterator_t *iter)
{
    return iter->view_pointer;
}


void qd_iterator_finalize(void)
{
    free(my_area);
    free(my_router);

    // unit tests need these zeroed
    my_area = 0;
    my_router = 0;
}


qd_iterator_t *qd_iterator_buffer_field(const qd_buffer_field_t *bfield,
                                        qd_iterator_view_t view)
{
    assert(bfield);
    qd_buffer_field_t copy = *bfield;
    qd_buffer_field_normalize(&copy);
    return qd_iterator_buffer(copy.buffer,
                              copy.cursor - qd_buffer_base(copy.buffer),
                              copy.remaining,
                              view);
}
