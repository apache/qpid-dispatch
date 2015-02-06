#ifndef __dispatch_message_h__
#define __dispatch_message_h__ 1
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

#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/buffer.h>
#include <qpid/dispatch/compose.h>
#include <qpid/dispatch/parse.h>
#include <qpid/dispatch/container.h>

/**@file
 * Message representation. 
 *
 * @defgroup message message
 *
 * Message representation.
 * @{
 */

// Callback for status change (confirmed persistent, loaded-in-memory, etc.)

typedef struct qd_message_t qd_message_t;

DEQ_DECLARE(qd_message_t, qd_message_list_t);

/** Message representation.
 *@internal
 */
struct qd_message_t {
    DEQ_LINKS(qd_message_t);
    // Private members not listed here.
};

/** Amount of message to be parsed.  */
typedef enum {
    QD_DEPTH_NONE,
    QD_DEPTH_HEADER,
    QD_DEPTH_DELIVERY_ANNOTATIONS,
    QD_DEPTH_MESSAGE_ANNOTATIONS,
    QD_DEPTH_PROPERTIES,
    QD_DEPTH_APPLICATION_PROPERTIES,
    QD_DEPTH_BODY,
    QD_DEPTH_ALL
} qd_message_depth_t;


/** Message fields */
typedef enum {
    QD_FIELD_NONE,   // reserved

    //
    // Message Sections
    //
    QD_FIELD_HEADER,
    QD_FIELD_DELIVERY_ANNOTATION,
    QD_FIELD_MESSAGE_ANNOTATION,
    QD_FIELD_PROPERTIES,
    QD_FIELD_APPLICATION_PROPERTIES,
    QD_FIELD_BODY,
    QD_FIELD_FOOTER,

    //
    // Fields of the Header Section
    // Ordered by list position
    //
    QD_FIELD_DURABLE,
    QD_FIELD_PRIORITY,
    QD_FIELD_TTL,
    QD_FIELD_FIRST_ACQUIRER,
    QD_FIELD_DELIVERY_COUNT,

    //
    // Fields of the Properties Section
    // Ordered by list position
    //
    QD_FIELD_MESSAGE_ID,
    QD_FIELD_USER_ID,
    QD_FIELD_TO,
    QD_FIELD_SUBJECT,
    QD_FIELD_REPLY_TO,
    QD_FIELD_CORRELATION_ID,
    QD_FIELD_CONTENT_TYPE,
    QD_FIELD_CONTENT_ENCODING,
    QD_FIELD_ABSOLUTE_EXPIRY_TIME,
    QD_FIELD_CREATION_TIME,
    QD_FIELD_GROUP_ID,
    QD_FIELD_GROUP_SEQUENCE,
    QD_FIELD_REPLY_TO_GROUP_ID
} qd_message_field_t;


/**
 * Allocate a new message.
 *
 * @return A pointer to a qd_message_t that is the sole reference to a newly allocated
 *         message.
 */
qd_message_t *qd_message(void);

/**
 * Free a message reference.  If this is the last reference to the message, free the
 * message as well.
 *
 * @param msg A pointer to a qd_message_t that is no longer needed.
 */
void qd_message_free(qd_message_t *msg);

/**
 * Make a new reference to an existing message.
 *
 * @param msg A pointer to a qd_message_t referencing a message.
 * @return A new pointer to the same referenced message.
 */
qd_message_t *qd_message_copy(qd_message_t *msg);

/**
 * Retrieve the message annotations from a message.
 *
 * IMPORTANT: The pointer returned by this function remains owned by the message.
 *            The caller MUST NOT free the parsed field.
 *
 * @param msg Pointer to a received message.
 * @return Pointer to the parsed field for the message annotations.  If the message doesn't
 *         have message annotations, the return value shall be NULL.
 */
qd_parsed_field_t *qd_message_message_annotations(qd_message_t *msg);

/**
 * Set the message annotations for the message.  If the message already has message annotations,
 * they will be overwritten/replaced by the new field.
 *
 * @param msg Pointer to a receiver message.
 * @param da Pointer to a composed field representing the new message annotations of the message.
 *           If null, the message will not have a message annotations field.
 *           IMPORTANT: The message will not take ownership of the composed field.  The
 *                      caller is responsible for freeing it after this call.  Since the contents
 *                      are copied into the message, it is safe to free the composed field
 *                      any time after the call to this function.
 */
void qd_message_set_message_annotations(qd_message_t *msg, qd_composed_field_t *da);

/**
 * Receive message data via a delivery.  This function may be called more than once on the same
 * delivery if the message spans multiple frames.  Once a complete message has been received, this
 * function shall return the message.
 *
 * @param delivery An incoming delivery from a link
 * @return A pointer to the complete message or 0 if the message is not yet complete.
 */
qd_message_t *qd_message_receive(qd_delivery_t *delivery);

/**
 * Send the message outbound on an outgoing link.
 *
 * @param msg A pointer to a message to be sent.
 * @param link The outgoing link on which to send the message.
 */
void qd_message_send(qd_message_t *msg, qd_link_t *link);

/**
 * Check that the message is well-formed up to a certain depth.  Any part of the message that is
 * beyond the specified depth is not checked for validity.
 */
int qd_message_check(qd_message_t *msg, qd_message_depth_t depth);

/**
 * Return an iterator for the requested message field.  If the field is not in the message,
 * return NULL.
 *
 * @param msg A pointer to a message.
 * @param field The field to be returned via iterator.
 * @return A field iterator that spans the requested field.
 */
qd_field_iterator_t *qd_message_field_iterator_typed(qd_message_t *msg, qd_message_field_t field);
qd_field_iterator_t *qd_message_field_iterator(qd_message_t *msg, qd_message_field_t field);

ssize_t qd_message_field_length(qd_message_t *msg, qd_message_field_t field);
ssize_t qd_message_field_copy(qd_message_t *msg, qd_message_field_t field, void *buffer, size_t *hdr_length);

//
// Functions for composed messages
//

// Convenience Functions
void qd_message_compose_1(qd_message_t *msg, const char *to, qd_buffer_list_t *buffers);
void qd_message_compose_2(qd_message_t *msg, qd_composed_field_t *content);
void qd_message_compose_3(qd_message_t *msg, qd_composed_field_t *content1, qd_composed_field_t *content2);

/** Put string representation of a message suitable for logging in buffer.
 * @return buffer
 */
char* qd_message_repr(qd_message_t *msg, char *buffer, size_t len);
/** Recommended buffer length for qd_message_repr */
int qd_message_repr_len();

///@}

#endif
