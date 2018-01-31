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
#include <qpid/dispatch/alloc.h>
#include "message_private.h"
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
    qd_iterator_pointer_t   start_pointer;      // Pointer to the raw data
    qd_iterator_pointer_t   view_start_pointer; // Pointer to the start of the view
    qd_iterator_pointer_t   view_pointer;       // Pointer to the remaining view
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


static char *my_area   = "";
static char *my_router = "";

static const char    *SEPARATORS = "./";
static const uint32_t HASH_INIT  = 5381;


static void parse_address_view(qd_iterator_t *iter)
{
    //
    // This function starts with an iterator view that is identical to
    // ITER_VIEW_ADDRESS_NO_HOST.  We will now further refine the view
    // in order to aid the router in looking up addresses.
    //

    qd_iterator_pointer_t save_pointer = iter->view_pointer;
    iter->annotation_length = 1;

    if (iter->prefix_override == '\0' && qd_iterator_prefix(iter, "_")) {
        if (iter->view == ITER_VIEW_ADDRESS_WITH_SPACE) {
            iter->view_pointer      = save_pointer;
            iter->view_space        = false;
            iter->annotation_length = 0;
            return;
        }

        if (qd_iterator_prefix(iter, "local/")) {
            iter->prefix = 'L';
            iter->state  = STATE_AT_PREFIX;
            return;
        }

        if (qd_iterator_prefix(iter, "topo/")) {
            if (qd_iterator_prefix(iter, "all/") || qd_iterator_prefix(iter, my_area)) {
                if (qd_iterator_prefix(iter, "all/")) {
                    iter->prefix = 'T';
                    iter->state  = STATE_AT_PREFIX;
                    return;
                } else if (qd_iterator_prefix(iter, my_router)) {
                    iter->prefix = 'L';
                    iter->state  = STATE_AT_PREFIX;
                    return;
                }

                iter->prefix = 'R';
                iter->state  = STATE_AT_PREFIX;
                iter->mode   = MODE_TO_SLASH;
                return;
            }

            iter->prefix = 'A';
            iter->state  = STATE_AT_PREFIX;
            iter->mode   = MODE_TO_SLASH;
            return;
        }
    }

    iter->prefix            = iter->prefix_override ? iter->prefix_override : 'M';
    iter->state             = STATE_AT_PREFIX;
    iter->view_space        = true;
    iter->annotation_length = iter->space_length + (iter->prefix == 'M' ? 2 : 1);
}


static void adjust_address_with_space(qd_iterator_t *iter)
{
    //
    // Convert an ADDRESS_HASH view to an ADDRESS_WITH_SPACE view
    //
    if (iter->view_space) {
        iter->annotation_length -= iter->prefix == 'M' ? 2 : 1;
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
        iter->prefix = 'R';
        iter->state  = STATE_AT_PREFIX;
        iter->mode   = MODE_TO_END;
        return;
    }

    iter->prefix = 'A';
    iter->state  = STATE_AT_PREFIX;
    iter->mode   = MODE_TO_SLASH;
}


void qd_iterator_remove_trailing_separator(qd_iterator_t *iter)
{
    // Save the iterator's pointer so we can apply it back before returning from this function.
    qd_iterator_pointer_t save_pointer = iter->view_pointer;

    char current_octet = 0;
    while (!qd_iterator_end(iter)) {
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
    // Advance to the node-id.
    //
    state_t               state = STATE_START;
    unsigned int          octet;
    qd_iterator_pointer_t save_pointer = {0,0,0};

    while (!qd_iterator_end(iter) && state != STATE_AT_NODE_ID) {
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

    if (iter->view == ITER_VIEW_NODE_HASH) {
        parse_node_view(iter);
        return;
    }
}


static inline void field_iterator_move_cursor(qd_iterator_t *iter, uint32_t length)
{
    // Only safe to call this help method if the cursor is parsing the data,
    // i.e. if iter is an address iterator, the cursor must be 'past' the
    // prefix
    assert(iter->state == STATE_IN_BODY);
    uint32_t count = (length > iter->view_pointer.remaining) ? iter->view_pointer.remaining : length;

    if (iter->view_pointer.buffer) {
        while (count) {
            uint32_t remaining = qd_buffer_cursor(iter->view_pointer.buffer) - iter->view_pointer.cursor;
            remaining = (remaining > count) ? count : remaining;
            iter->view_pointer.cursor += remaining;
            iter->view_pointer.remaining -= remaining;
            count -= remaining;
            if (iter->view_pointer.cursor == qd_buffer_cursor(iter->view_pointer.buffer)) {
                iter->view_pointer.buffer = iter->view_pointer.buffer->next;
                if (iter->view_pointer.buffer == 0) {
                    iter->view_pointer.remaining = 0;
                    iter->view_pointer.cursor = 0;
                    break;
                } else {
                    iter->view_pointer.cursor = qd_buffer_base(iter->view_pointer.buffer);
                }
            }
        }
    } else {    // string/binary data
        iter->view_pointer.cursor    += count;
        iter->view_pointer.remaining -= count;
    }
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


void qd_iterator_set_address(const char *area, const char *router)
{
    static char buf[2048];     /* Static buffer, should usually be Big Enough */
    static char *ptr = buf;
#define FMT "%s/%c%s/", area, '\0', router /* "area/\0router/\0" */
    size_t size = snprintf(buf, sizeof(buf), FMT);
    if (size < sizeof(buf)) {
        ptr = buf;
    } else {
        if (ptr && ptr != buf) free(ptr);
        ptr = malloc(size + 1);
        snprintf(buf, sizeof(buf), FMT);
    }
    my_area = ptr;
    my_router = ptr + strlen(my_area) + 1;
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
    iter->start_pointer.buffer    = buffer;
    iter->start_pointer.cursor    = qd_buffer_base(buffer) + offset;
    iter->start_pointer.remaining = length;
    iter->phase                   = '0';

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
    int view_length = qd_iterator_length(iter);
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
            iter->annotation_length = (iter->view_space ? space_length : 0) + (iter->prefix == 'M' ? 2 : 1);
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

    if (iter->state == STATE_AT_PREFIX) {
        iter->state = iter->prefix == 'M' ? STATE_AT_PHASE : (iter->view_space && iter->space) ? STATE_IN_SPACE : STATE_IN_BODY;
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

    if (iter->view_pointer.remaining == 0)
        return (unsigned char) 0;

    unsigned char result = *(iter->view_pointer.cursor);

    field_iterator_move_cursor(iter, 1);
    if (iter->view_pointer.remaining && iter->mode == MODE_TO_SLASH && *(iter->view_pointer.cursor) == '/')
        iter->view_pointer.remaining = 0;

    return result;
}


bool qd_iterator_end(const qd_iterator_t *iter)
{
    return iter ? qd_iterator_remaining(iter) == 0 : true;
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

    while (length > 0 && !qd_iterator_end(iter)) {
        if (iter->state == STATE_IN_BODY) {
            field_iterator_move_cursor(iter, length);
            break;
        } else {
            qd_iterator_octet(iter);
            length--;
        }
    }
}


uint32_t qd_iterator_remaining(const qd_iterator_t *iter)
{
    return iter ? iter->annotation_remaining + iter->view_pointer.remaining : 0;
}


bool qd_iterator_equal(qd_iterator_t *iter, const unsigned char *string)
{
    if (!iter)
        return false;

    qd_iterator_reset(iter);

    while (!qd_iterator_end(iter) && *string) {
        if (*string != qd_iterator_octet(iter))
            break;
        string++;
    }

    bool match = (qd_iterator_end(iter) && (*string == 0));
    qd_iterator_reset(iter);
    return match;
}


bool qd_iterator_prefix(qd_iterator_t *iter, const char *prefix)
{
    if (!iter)
        return false;

    qd_iterator_pointer_t save_pointer = iter->view_pointer;
    unsigned char *c                   = (unsigned char*) prefix;

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


// bare bones copy of field_iterator_move_cursor with no field/view baggage
void iterator_pointer_move_cursor(qd_iterator_pointer_t *ptr, uint32_t length)
{
    uint32_t count = length > ptr->remaining ? ptr->remaining : length;

    while (count) {
        uint32_t remaining = qd_buffer_cursor(ptr->buffer) - ptr->cursor;
        remaining = remaining > count ? count : remaining;
        ptr->cursor += remaining;
        ptr->remaining -= remaining;
        count -= remaining;
        if (ptr->cursor == qd_buffer_cursor(ptr->buffer)) {
            ptr->buffer = ptr->buffer->next;
            if (ptr->buffer == 0) {
                ptr->remaining = 0;
                ptr->cursor = 0;
                break;
            } else {
                ptr->cursor = qd_buffer_base(ptr->buffer);
            }
        }
    }
}


// bare bones copy of qd_iterator_prefix with no iterator baggage
bool qd_iterator_prefix_ptr(const qd_iterator_pointer_t *ptr, uint32_t skip, const char *prefix)
{
    if (!ptr)
        return false;

    qd_iterator_pointer_t lptr;
    *&lptr = *ptr;

    iterator_pointer_move_cursor(&lptr, skip);

    unsigned char *c = (unsigned char*) prefix;

    while(*c && lptr.remaining) {
        unsigned char ic = *lptr.cursor;

        if (*c != ic)
            break;
        c++;

        iterator_pointer_move_cursor(&lptr, 1);
        lptr.remaining -= 1;
    }

    return *c == 0;
}


int qd_iterator_length(const qd_iterator_t *iter)
{
    return iter ? iter->annotation_length + iter->view_start_pointer.remaining : 0;
}


int qd_iterator_ncopy(qd_iterator_t *iter, unsigned char* buffer, int n)
{
    if (!iter)
        return 0;

    qd_iterator_reset(iter);
    int i = 0;
    while (!qd_iterator_end(iter) && i < n)
        buffer[i++] = qd_iterator_octet(iter);
    return i;
}


char* qd_iterator_strncpy(qd_iterator_t *iter, char* buffer, int n)
{
    int i = qd_iterator_ncopy(iter, (unsigned char*) buffer, n-1);
    buffer[i] = '\0';
    return buffer;
}


unsigned char *qd_iterator_copy(qd_iterator_t *iter)
{
    if (!iter)
        return 0;

    int length = qd_iterator_length(iter);
    unsigned char *copy = malloc(length+1);
    int i = qd_iterator_ncopy(iter, copy, length+1);
    copy[i] = '\0';
    return copy;
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


qd_iovec_t *qd_iterator_iovec(const qd_iterator_t *iter)
{
    if (!iter)
        return 0;

    //
    // Count the number of buffers this field straddles
    //
    qd_iterator_pointer_t pointer   = iter->view_start_pointer;
    int                   bufcnt    = 1;
    qd_buffer_t          *buf       = pointer.buffer;
    size_t                bufsize   = qd_buffer_size(buf) - (pointer.cursor - qd_buffer_base(pointer.buffer));
    ssize_t               remaining = pointer.remaining - bufsize;

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
    remaining  = pointer.remaining;

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

    qd_iterator_reset(iter);
    while (!qd_iterator_end(iter))
        hash = ((hash << 5) + hash) + (uint32_t) qd_iterator_octet(iter); /* hash * 33 + c */

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

    while (!qd_iterator_end(iter)) {
        // Get the octet at which the iterator is currently pointing to.
        octet = qd_iterator_octet(iter);
        segment_length += 1;

        if (strrchr(SEPARATORS, (int) octet)) {
            qd_insert_hash_segment(iter, &hash, segment_length-1);
        }

        hash = ((hash << 5) + hash) + octet; /* hash * 33 + c */
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


void qd_iterator_get_view_cursor(
    const qd_iterator_t   *iter,
    qd_iterator_pointer_t *ptr)
{
    ptr->buffer    = iter->view_pointer.buffer;
    ptr->cursor    = iter->view_pointer.cursor;
    ptr->remaining = iter->view_pointer.remaining;
}
