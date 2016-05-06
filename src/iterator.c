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

#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/ctools.h>
#include "alloc.h"
#include <qpid/dispatch/log.h>
#include "message_private.h"
#include <stdio.h>
#include <string.h>

//static const char *log_module = "FIELD";

typedef enum {
    MODE_TO_END,
    MODE_TO_SLASH
} parse_mode_t;

typedef enum {
    STATE_AT_PREFIX,
    STATE_AT_PHASE,
    STATE_IN_ADDRESS
} addr_state_t;

typedef struct {
    qd_buffer_t   *buffer;
    unsigned char *cursor;
    int            length;
} pointer_t;

typedef struct qd_hash_segment_t qd_hash_segment_t;

struct qd_hash_segment_t {
    DEQ_LINKS(qd_hash_segment_t); //Adds the *prev and *next links
    uint32_t   hash; //The hash of the segment
    uint32_t segment_length; //The length of each hash segment
};

DEQ_DECLARE(qd_hash_segment_t, qd_hash_segment_list_t);
ALLOC_DECLARE(qd_hash_segment_t);
ALLOC_DEFINE(qd_hash_segment_t);

struct qd_field_iterator_t {
    pointer_t               start_pointer;
    pointer_t               view_start_pointer;
    pointer_t               pointer;
    qd_iterator_view_t      view;
    qd_hash_segment_list_t  hash_segments;
    parse_mode_t            mode;
    addr_state_t            state;
    bool                    view_prefix;
    unsigned char           prefix;
    unsigned char           prefix_override;
    unsigned char           phase;
};

ALLOC_DECLARE(qd_field_iterator_t);
ALLOC_DEFINE(qd_field_iterator_t);

typedef enum {
    STATE_START,
    STATE_SLASH_LEFT,
    STATE_SKIPPING_TO_NEXT_SLASH,
    STATE_SCANNING,
    STATE_COLON,
    STATE_COLON_SLASH,
    STATE_AT_NODE_ID
} state_t;


static char *my_area    = "";
static char *my_router  = "";

const char *SEPARATORS  = "./";

const uint32_t HASH_INIT = 5381;


static void parse_address_view(qd_field_iterator_t *iter)
{
    //
    // This function starts with an iterator view that is identical to
    // ITER_VIEW_NO_HOST.  We will now further refine the view in order
    // to aid the router in looking up addresses.
    //

    if (iter->prefix_override == '\0' && qd_field_iterator_prefix(iter, "_")) {
        if (qd_field_iterator_prefix(iter, "local/")) {
            iter->prefix      = 'L';
            iter->state       = STATE_AT_PREFIX;
            iter->view_prefix = true;
            return;
        }

        if (qd_field_iterator_prefix(iter, "topo/")) {
            if (qd_field_iterator_prefix(iter, "all/") || qd_field_iterator_prefix(iter, my_area)) {
                if (qd_field_iterator_prefix(iter, "all/")) {
                    iter->prefix      = 'T';
                    iter->state       = STATE_AT_PREFIX;
                    iter->view_prefix = true;
                    return;
                } else if (qd_field_iterator_prefix(iter, my_router)) {
                    iter->prefix      = 'L';
                    iter->state       = STATE_AT_PREFIX;
                    iter->view_prefix = true;
                    return;
                }

                iter->prefix      = 'R';
                iter->state       = STATE_AT_PREFIX;
                iter->view_prefix = true;
                iter->mode        = MODE_TO_SLASH;
                return;
            }

            iter->prefix      = 'A';
            iter->state       = STATE_AT_PREFIX;
            iter->view_prefix = true;
            iter->mode        = MODE_TO_SLASH;
            return;
        }
    }

    iter->prefix      = iter->prefix_override ? iter->prefix_override : 'M';
    iter->state       = STATE_AT_PREFIX;
    iter->view_prefix = true;
}


static void parse_node_view(qd_field_iterator_t *iter)
{
    //
    // This function starts with an iterator view that is identical to
    // ITER_VIEW_NO_HOST.  We will now further refine the view in order
    // to aid the router in looking up nodes.
    //

    if (qd_field_iterator_prefix(iter, my_area)) {
        iter->prefix      = 'R';
        iter->state       = STATE_AT_PREFIX;
        iter->view_prefix = true;
        iter->mode        = MODE_TO_END;
        return;
    }

    iter->prefix      = 'A';
    iter->state       = STATE_AT_PREFIX;
    iter->view_prefix = true;
    iter->mode        = MODE_TO_SLASH;
}


static void qd_address_iterator_remove_trailing_separator(qd_field_iterator_t *iter)
{
    // Save the iterator's pointer so we can apply it back before returning from this function.
    pointer_t save_pointer = iter->pointer;

    char current_octet = 0;
    while (!qd_field_iterator_end(iter)) {
        current_octet = qd_field_iterator_octet(iter);
    }

    // We have the last octet in current_octet
    iter->pointer = save_pointer;
    if (current_octet && strrchr(SEPARATORS, (int) current_octet))
        iter->pointer.length--;
}


static void view_initialize(qd_field_iterator_t *iter)
{
    //
    // The default behavior is for the view to *not* have a prefix.
    // We'll add one if it's needed later.
    //
    iter->state       = STATE_IN_ADDRESS;
    iter->view_prefix = false;
    iter->mode        = MODE_TO_END;

    if (iter->view == ITER_VIEW_ALL)
        return;

    //
    // Advance to the node-id.
    //
    state_t        state = STATE_START;
    unsigned int   octet;
    pointer_t      save_pointer = {0,0,0};

    while (!qd_field_iterator_end(iter) && state != STATE_AT_NODE_ID) {
        octet = qd_field_iterator_octet(iter);

        switch (state) {
        case STATE_START :
            if (octet == '/') {
                state = STATE_SLASH_LEFT;
                save_pointer = iter->pointer;
            } else
                state = STATE_SCANNING;
            break;

        case STATE_SLASH_LEFT :
            if (octet == '/')
                state = STATE_SKIPPING_TO_NEXT_SLASH;
            else {
                state = STATE_AT_NODE_ID;
                iter->pointer = save_pointer;
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
                save_pointer = iter->pointer;
            } else
                state = STATE_SCANNING;
            break;

        case STATE_COLON_SLASH :
            if (octet == '/')
                state = STATE_SKIPPING_TO_NEXT_SLASH;
            else {
                state = STATE_AT_NODE_ID;
                iter->pointer = save_pointer;
            }
            break;

        case STATE_AT_NODE_ID :
            break;
        }
    }

    if (state != STATE_AT_NODE_ID) {
        //
        // The address string was relative, not absolute.  The node-id
        // is at the beginning of the string.
        //
        iter->pointer = iter->start_pointer;
    }

    //
    // Cursor is now on the first octet of the node-id
    //
    if (iter->view == ITER_VIEW_NODE_ID) {
        iter->mode = MODE_TO_SLASH;
        return;
    }

    if (iter->view == ITER_VIEW_NO_HOST) {
        iter->mode = MODE_TO_END;
        return;
    }

    if (iter->view == ITER_VIEW_ADDRESS_HASH) {
        iter->mode = MODE_TO_END;
        qd_address_iterator_remove_trailing_separator(iter);
        parse_address_view(iter);
        return;
    }

    if (iter->view == ITER_VIEW_NODE_HASH) {
        iter->mode = MODE_TO_END;
        parse_node_view(iter);
        return;
    }

    if (iter->view == ITER_VIEW_NODE_SPECIFIC) {
        iter->mode = MODE_TO_END;
        while (!qd_field_iterator_end(iter)) {
            octet = qd_field_iterator_octet(iter);
            if (octet == '/')
                break;
        }
        return;
    }
}


static inline void field_iterator_move_cursor(qd_field_iterator_t *iter, uint32_t length)
{
    // Only safe to call this help method if the cursor is parsing the data,
    // i.e. if iter is an address iterator, the cursor must be 'past' the
    // prefix
    assert(iter->state == STATE_IN_ADDRESS);
    uint32_t count = ((length > iter->pointer.length)
                      ? iter->pointer.length
                      : length);

    if (iter->pointer.buffer) {
        while (count) {
            uint32_t remaining = qd_buffer_cursor(iter->pointer.buffer) - iter->pointer.cursor;
            remaining = (remaining > count) ? count : remaining;
            iter->pointer.cursor += remaining;
            iter->pointer.length -= remaining;
            count -= remaining;
            if (iter->pointer.cursor == qd_buffer_cursor(iter->pointer.buffer)) {
                iter->pointer.buffer = iter->pointer.buffer->next;
                if (iter->pointer.buffer == 0) {
                    iter->pointer.length = 0;
                    iter->pointer.cursor = 0;
                    break;
                } else {
                    iter->pointer.cursor = qd_buffer_base(iter->pointer.buffer);
                }
            }
        }
    } else {    // string/binary data
        iter->pointer.cursor += count;
        iter->pointer.length -= count;
    }
}

void qd_field_iterator_set_address(const char *area, const char *router)
{
    my_area = (char*) malloc(strlen(area) + 2);
    strcpy(my_area, area);
    strcat(my_area, "/");

    my_router = (char*) malloc(strlen(router) + 2);
    strcpy(my_router, router);
    strcat(my_router, "/");
}


qd_field_iterator_t* qd_address_iterator_string(const char *text, qd_iterator_view_t view)
{
    qd_field_iterator_t *iter = new_qd_field_iterator_t();
    if (!iter)
        return 0;

    iter->start_pointer.buffer     = 0;
    iter->start_pointer.cursor     = (unsigned char*) text;
    iter->start_pointer.length     = strlen(text);
    iter->phase                    = '0';
    iter->prefix_override          = '\0';

    DEQ_INIT(iter->hash_segments);

    qd_address_iterator_reset_view(iter, view);

    return iter;
}


qd_field_iterator_t* qd_address_iterator_binary(const char *text, int length, qd_iterator_view_t view)
{
    qd_field_iterator_t *iter = new_qd_field_iterator_t();
    if (!iter)
        return 0;

    iter->start_pointer.buffer = 0;
    iter->start_pointer.cursor = (unsigned char*) text;
    iter->start_pointer.length = length;
    iter->phase                = '0';
    iter->prefix_override      = '\0';

    DEQ_INIT(iter->hash_segments);

    qd_address_iterator_reset_view(iter, view);

    return iter;
}


qd_field_iterator_t *qd_address_iterator_buffer(qd_buffer_t *buffer, int offset, int length, qd_iterator_view_t view)
{
    qd_field_iterator_t *iter = new_qd_field_iterator_t();
    if (!iter)
        return 0;

    iter->start_pointer.buffer = buffer;
    iter->start_pointer.cursor = qd_buffer_base(buffer) + offset;
    iter->start_pointer.length = length;
    iter->phase                = '0';
    iter->prefix_override      = '\0';

    DEQ_INIT(iter->hash_segments);

    qd_address_iterator_reset_view(iter, view);

    return iter;
}


void qd_field_iterator_free(qd_field_iterator_t *iter)
{
    if (!iter) return;
    free_qd_field_iterator_t(iter);
}


void qd_field_iterator_reset(qd_field_iterator_t *iter)
{
    iter->pointer = iter->view_start_pointer;
    iter->state   = iter->view_prefix ? STATE_AT_PREFIX : STATE_IN_ADDRESS;
}


void qd_address_iterator_reset_view(qd_field_iterator_t *iter, qd_iterator_view_t  view)
{
    iter->pointer = iter->start_pointer;
    iter->view    = view;

    view_initialize(iter);

    iter->view_start_pointer = iter->pointer;
}


qd_iterator_view_t qd_address_iterator_get_view(const qd_field_iterator_t *iter)
{
    return iter->view;
}


void qd_address_iterator_set_phase(qd_field_iterator_t *iter, char phase)
{
    iter->phase = phase;
}


void qd_address_iterator_override_prefix(qd_field_iterator_t *iter, char prefix)
{
    iter->prefix_override = prefix;
    qd_address_iterator_reset_view(iter, iter->view);
}


unsigned char qd_field_iterator_octet(qd_field_iterator_t *iter)
{
    if (iter->state == STATE_AT_PREFIX) {
        iter->state =  iter->prefix == 'M' ? STATE_AT_PHASE : STATE_IN_ADDRESS;
        return iter->prefix;
    }

    if (iter->state == STATE_AT_PHASE) {
        iter->state = STATE_IN_ADDRESS;
        return iter->phase;
    }

    if (iter->pointer.length == 0)
        return (unsigned char) 0;

    unsigned char result = *(iter->pointer.cursor);

    field_iterator_move_cursor(iter, 1);
    if (iter->pointer.length && iter->mode == MODE_TO_SLASH && *(iter->pointer.cursor) == '/')
        iter->pointer.length = 0;

    return result;
}


int qd_field_iterator_end(const qd_field_iterator_t *iter)
{
    return iter->pointer.length == 0;
}


qd_field_iterator_t *qd_field_iterator_sub(const qd_field_iterator_t *iter, uint32_t length)
{
    qd_field_iterator_t *sub = new_qd_field_iterator_t();
    if (!sub)
        return 0;

    sub->start_pointer        = iter->pointer;
    sub->start_pointer.length = length;
    sub->view_start_pointer   = sub->start_pointer;
    sub->pointer              = sub->start_pointer;
    sub->view                 = iter->view;
    sub->mode                 = iter->mode;
    sub->state                = STATE_IN_ADDRESS;
    sub->view_prefix          = false;
    sub->prefix_override      = '\0';
    sub->phase                = '0';

    DEQ_INIT(sub->hash_segments);

    return sub;
}


void qd_field_iterator_advance(qd_field_iterator_t *iter, uint32_t length)
{
    while (length > 0 && !qd_field_iterator_end(iter)) {
        if (iter->state == STATE_IN_ADDRESS) {
            field_iterator_move_cursor(iter, length);
            break;
        } else {
            qd_field_iterator_octet(iter);
            length--;
        }
    }
}


uint32_t qd_field_iterator_remaining(const qd_field_iterator_t *iter)
{
    return iter->pointer.length;
}


int qd_field_iterator_equal(qd_field_iterator_t *iter, const unsigned char *string)
{
    qd_field_iterator_reset(iter);

    while (!qd_field_iterator_end(iter) && *string) {
        if (*string != qd_field_iterator_octet(iter))
            break;
        string++;
    }

    int match = (qd_field_iterator_end(iter) && (*string == 0));
    qd_field_iterator_reset(iter);
    return match;
}


int qd_field_iterator_prefix(qd_field_iterator_t *iter, const char *prefix)
{
    pointer_t      save_pointer = iter->pointer;
    unsigned char *c            = (unsigned char*) prefix;

    while(*c) {
        if (*c != qd_field_iterator_octet(iter))
            break;
        c++;
    }

    if (*c) {
        iter->pointer = save_pointer;
        return 0;
    }

    return 1;
}


int qd_field_iterator_length(const qd_field_iterator_t *iter)
{
    qd_field_iterator_t copy = *iter;
    int length = 0;
    qd_field_iterator_reset(&copy);
    while (!qd_field_iterator_end(&copy)) {
        qd_field_iterator_octet(&copy);
        length++;
    }
    return length;
}


int qd_field_iterator_ncopy(qd_field_iterator_t *iter, unsigned char* buffer, int n) {
    qd_field_iterator_reset(iter);
    int i = 0;
    while (!qd_field_iterator_end(iter) && i < n)
        buffer[i++] = qd_field_iterator_octet(iter);
    return i;
}


char* qd_field_iterator_strncpy(qd_field_iterator_t *iter, char* buffer, int n) {
    int i = qd_field_iterator_ncopy(iter, (unsigned char*)buffer, n-1);
    buffer[i] = '\0';
    return buffer;
}


unsigned char *qd_field_iterator_copy(qd_field_iterator_t *iter)
{
    int length = qd_field_iterator_length(iter);
    unsigned char *copy = malloc(length+1);
    int i = qd_field_iterator_ncopy(iter, copy, length+1);
    copy[i] = '\0';
    return copy;
}


qd_field_iterator_t *qd_field_iterator_dup(const qd_field_iterator_t *iter)
{
    if (iter == 0)
        return 0;

    qd_field_iterator_t *dup = new_qd_field_iterator_t();
    if (dup)
        *dup = *iter;
    return dup;
}


qd_iovec_t *qd_field_iterator_iovec(const qd_field_iterator_t *iter)
{
    assert(!iter->view_prefix); // Not supported for views with a prefix

    //
    // Count the number of buffers this field straddles
    //
    pointer_t    pointer   = iter->view_start_pointer;
    int          bufcnt    = 1;
    qd_buffer_t *buf       = pointer.buffer;
    size_t       bufsize   = qd_buffer_size(buf) - (pointer.cursor - qd_buffer_base(pointer.buffer));
    ssize_t      remaining = pointer.length - bufsize;

    while (remaining > 0) {
        bufcnt++;
        buf = buf->next;
        if (!buf)
            return 0;
        remaining -= qd_buffer_size(buf);
    }

    //
    // Allocate an iovec object big enough to hold the number of buffers
    //
    qd_iovec_t *iov = qd_iovec(bufcnt);
    if (!iov)
        return 0;

    //
    // Build out the io vectors with pointers to the segments of the field in buffers
    //
    bufcnt     = 0;
    buf        = pointer.buffer;
    bufsize    = qd_buffer_size(buf) - (pointer.cursor - qd_buffer_base(pointer.buffer));
    void *base = pointer.cursor;
    remaining  = pointer.length;

    while (remaining > 0) {
        if (bufsize > remaining)
            bufsize = remaining;
        qd_iovec_array(iov)[bufcnt].iov_base = base;
        qd_iovec_array(iov)[bufcnt].iov_len  = bufsize;
        bufcnt++;
        remaining -= bufsize;
        if (remaining > 0) {
            buf     = buf->next;
            base    = qd_buffer_base(buf);
            bufsize = qd_buffer_size(buf);
        }
    }

    return iov;
}


uint32_t qd_iterator_hash_function(qd_field_iterator_t *iter)
{
    uint32_t hash = HASH_INIT;

    qd_field_iterator_reset(iter);
    while (!qd_field_iterator_end(iter))
        hash = ((hash << 5) + hash) + (int) qd_field_iterator_octet(iter); /* hash * 33 + c */

    return hash;
}


/**
 * Creates and returns a new qd_hash_segment_t and initializes it.
 */
static qd_hash_segment_t *qd_iterator_hash_segment(void)
{
    qd_hash_segment_t *hash_segment = new_qd_hash_segment_t();
    DEQ_ITEM_INIT(hash_segment);
    hash_segment->hash       = 0;
    hash_segment->segment_length = 0;
    return hash_segment;
}


/**
 * Create a new hash segment and insert it at the end of the linked list
 */
static void qd_insert_hash_segment(qd_field_iterator_t *iter, uint32_t *hash, int segment_length)
{
    qd_hash_segment_t *hash_segment = qd_iterator_hash_segment();

    // While storing the segment, don't include the hash of the separator in the segment but do include it in the overall hash.
    hash_segment->hash = *hash;

    hash_segment->segment_length = segment_length;
    DEQ_INSERT_TAIL(iter->hash_segments, hash_segment);
}


void qd_iterator_hash_segments(qd_field_iterator_t *iter)
{
    // Reset the pointers in the iterator
    qd_field_iterator_reset(iter);
    uint32_t hash = HASH_INIT;
    char octet;
    int segment_length=0;

    while (!qd_field_iterator_end(iter)) {
        // Get the octet at which the iterator is currently pointing to.
        octet = qd_field_iterator_octet(iter);
        segment_length += 1;

        if (strrchr(SEPARATORS, (int) octet)) {
            qd_insert_hash_segment(iter, &hash, segment_length-1);
        }

        hash = ((hash << 5) + hash) + octet; /* hash * 33 + c */
    }

    // Segments should never end with a separator. see view_initialize which in turn calls
    // qd_address_iterator_remove_trailing_separator
    // Insert the last segment which was not inserted in the previous while loop
    qd_insert_hash_segment(iter, &hash, segment_length);

    // Return the pointers in the iterator back to the original state before returning from this function.
    qd_field_iterator_reset(iter);
}


bool qd_iterator_hash_and_reset(qd_field_iterator_t *iter, uint32_t *hash)
{
    qd_hash_segment_t *hash_segment = DEQ_TAIL(iter->hash_segments);
    if (!hash_segment)
        return false;

    *hash = hash_segment->hash;

    // Get the length of the hashed segment and set it on the iterator so that the iterator can only advance till that length
    // Check for a non empty iter->prefix and reduce the segment length by 1
    if (iter->view_prefix) {
        if (iter->prefix == 'M')
            iter->view_start_pointer.length = hash_segment->segment_length - 2;
        else
            iter->view_start_pointer.length = hash_segment->segment_length - 1;
    } else
        iter->view_start_pointer.length = hash_segment->segment_length;

    // Remove the tail from the hash segments since we have already compared it.
    DEQ_REMOVE_TAIL(iter->hash_segments);

    free_qd_hash_segment_t(hash_segment);

    return true;
}
