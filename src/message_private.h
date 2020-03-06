#ifndef __message_private_h__
#define __message_private_h__ 1
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

#include <qpid/dispatch/message.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/atomic.h>

/** @file
 * Message representation.
 * 
 * Architecture of the message module:
 *
 *     +--------------+            +----------------------+
 *     |              |            |                      |
 *     | qd_message_t |----------->| qd_message_content_t |
 *     |              |     +----->|                      |
 *     +--------------+     |      +----------------------+
 *                          |                |
 *     +--------------+     |                |    +-------------+   +-------------+   +-------------+
 *     |              |     |                +--->| qd_buffer_t |-->| qd_buffer_t |-->| qd_buffer_t |--/
 *     | qd_message_t |-----+                     +-------------+   +-------------+   +-------------+
 *     |              |
 *     +--------------+
 *
 * The message module provides chained-fixed-sized-buffer storage of message content with multiple
 * references.  If a message is received and is to be queued for multiple destinations, there is only
 * one copy of the message content in memory but multiple lightweight references to the content.
 *
 * @internal
 * @{ 
 */

typedef struct {
    qd_buffer_t *buffer;     // Buffer that contains the first octet of the field, null if the field is not present
    size_t       offset;     // Offset in the buffer to the first octet of the header
    size_t       length;     // Length of the field or zero if unneeded
    size_t       hdr_length; // Length of the field's header (not included in the length of the field)
    bool         parsed;     // True iff the buffer chain has been parsed to find this field
    uint8_t      tag;        // Type tag of the field
} qd_field_location_t;


// TODO - consider using pointers to qd_field_location_t below to save memory
// TODO - provide a way to allocate a message without a lock for the link-routing case.
//        It's likely that link-routing will cause no contention for the message content.
//

typedef struct {
    sys_mutex_t         *lock;
    sys_atomic_t         ref_count;                       // The number of messages referencing this
    qd_buffer_list_t     buffers;                         // The buffer chain containing the message
    qd_buffer_t         *pending;                         // Buffer owned by and filled by qd_message_receive
    uint64_t             buffers_freed;                   // count of large msg buffers freed on send

    qd_field_location_t  section_message_header;          // The message header list
    qd_field_location_t  section_delivery_annotation;     // The delivery annotation map
    qd_field_location_t  section_message_annotation;      // The message annotation map
    qd_field_location_t  section_message_properties;      // The message properties list
    qd_field_location_t  section_application_properties;  // The application properties list
    qd_field_location_t  section_body;                    // The message body: Data
    qd_field_location_t  section_footer;                  // The footer
    qd_field_location_t  field_user_annotations;          // Opaque user message annotations, not a real field.

    qd_field_location_t  field_message_id;                // The string value of the message-id
    qd_field_location_t  field_user_id;                   // The string value of the user-id
    qd_field_location_t  field_to;                        // The string value of the to field
    qd_field_location_t  field_subject;                   // The string value of the subject field
    qd_field_location_t  field_reply_to;                  // The string value of the reply_to field
    qd_field_location_t  field_correlation_id;            // The string value of the correlation_id field
    qd_field_location_t  field_content_type;
    qd_field_location_t  field_content_encoding;
    qd_field_location_t  field_absolute_expiry_time;
    qd_field_location_t  field_creation_time;
    qd_field_location_t  field_group_id;
    qd_field_location_t  field_group_sequence;
    qd_field_location_t  field_reply_to_group_id;

    qd_field_location_t  body;                            // The body of the message
    qd_buffer_t         *parse_buffer;
    unsigned char       *parse_cursor;
    qd_message_depth_t   parse_depth;
    qd_iterator_t       *ma_field_iter_in;                // 'message field iterator' for msg.FIELD_MESSAGE_ANNOTATION

    qd_iterator_pointer_t ma_user_annotation_blob;        // Original user annotations
                                                          // with router annotations stripped
    uint32_t             ma_count;                        // Number of map elements in blob
                                                          // after the router fields stripped
    qd_parsed_field_t   *ma_pf_ingress;
    qd_parsed_field_t   *ma_pf_phase;
    qd_parsed_field_t   *ma_pf_to_override;
    qd_parsed_field_t   *ma_pf_trace;
    int                  ma_int_phase;
    int                  max_message_size;               // configured max; 0 if no max to enforce
    int                  bytes_received;                 // returned by pn_link_recv()
    uint32_t             fanout;                         // The number of receivers for this message, including in-process subscribers.
    qd_link_t_sp         input_link_sp;                  // message received on this link

    bool                 ma_parsed;                      // have parsed annotations in incoming message
    bool                 discard;                        // Should this message be discarded?
    bool                 receive_complete;               // true if the message has been completely received, false otherwise
    bool                 q2_input_holdoff;               // hold off calling pn_link_recv
    bool                 aborted;                        // receive completed with abort flag set
    bool                 disable_q2_holdoff;             // Disable the Q2 flow control
    bool                 priority_parsed;
    bool                 priority_present;
    bool                 oversize;                       // policy oversize handling in effect
    uint8_t              priority;                       // The priority of this message
} qd_message_content_t;

typedef struct {
    qd_iterator_pointer_t cursor;          // A pointer to the current location of the outgoing byte stream.
    qd_message_depth_t    message_depth;   // What is the depth of the message that has been received so far
    qd_message_depth_t    sent_depth;      // How much of the message has been sent?  QD_DEPTH_NONE means nothing has been sent so far, QD_DEPTH_HEADER means the header has already been sent, dont send it again and so on.
    qd_message_content_t *content;         // The actual content of the message. The content is never copied
    qd_buffer_list_t      ma_to_override;  // to field in outgoing message annotations.
    qd_buffer_list_t      ma_trace;        // trace list in outgoing message annotations
    qd_buffer_list_t      ma_ingress;      // ingress field in outgoing message annotations
    int                   ma_phase;        // phase for the override address
    bool                  strip_annotations_in;
    bool                  send_complete;   // Has the message been completely received and completely sent?
    bool                  tag_sent;        // Tags are sent
    bool                  is_fanout;       // If msg is an outgoing fanout
} qd_message_pvt_t;

ALLOC_DECLARE(qd_message_t);
ALLOC_DECLARE(qd_message_content_t);

#define MSG_CONTENT(m) (((qd_message_pvt_t*) m)->content)

/** Initialize logging */
void qd_message_initialize();

#define QDR_N_PRIORITIES     10
#define QDR_MAX_PRIORITY     (QDR_N_PRIORITIES - 1)
#define QDR_DEFAULT_PRIORITY  4

///@}

#endif
