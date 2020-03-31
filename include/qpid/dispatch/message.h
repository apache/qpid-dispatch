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
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/buffer.h>
#include <qpid/dispatch/compose.h>
#include <qpid/dispatch/parse.h>
#include <qpid/dispatch/container.h>
#include <qpid/dispatch/log.h>

/**@file
 * Message representation. 
 *
 * @defgroup message message
 *
 * Message representation.
 * @{
 */

// DISPATCH-807 Queue depth limits
// upper and lower limits for bang bang hysteresis control
//
// Q2 defines the maximum number of buffers allowed in a message's buffer
// chain.  This limits the number of bytes that will be read from an incoming
// link (pn_link_recv) for the current message. Once Q2 is enabled no further
// pn_link_recv calls will be done on the link. Q2 remains in effect until enough
// bytes have been consumed by the outgoing link(s) to drop the number of
// buffered bytes below the lower threshold.
#define QD_QLIMIT_Q2_UPPER 256   // disable pn_link_recv (qd_buffer_t's)
#define QD_QLIMIT_Q2_LOWER 128   // re-enable pn_link_recv
//
// Q3 limits the number of bytes allowed to be buffered in a session's outgoing
// buffer.  Once the Q3 upper limit is hit (read via pn_session_outgoing_bytes),
// pn_link_send will no longer be called for ALL outgoing links sharing the
// session.  When enough outgoing bytes have been drained below the lower limit
// pn_link_sends will resume.
#define QD_QLIMIT_Q3_UPPER  (QD_QLIMIT_Q3_LOWER * 2)  // in pn_buffer_t's
#define QD_QLIMIT_Q3_LOWER  (QD_QLIMIT_Q2_UPPER * 2)  // 2 == a guess

// Callback for status change (confirmed persistent, loaded-in-memory, etc.)

typedef struct qd_message_t qd_message_t;

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
 * Retrieve the message annotations from a message and place them in message storage.
 *
 * IMPORTANT: The pointer returned by this function remains owned by the message.
 *            The caller MUST NOT free the parsed field.
 *
 * @param msg Pointer to a received message.
 */
void qd_message_message_annotations(qd_message_t *msg);

/**
 * Set the value for the QD_MA_TRACE field in the outgoing message annotations
 * for the message.
 *
 * IMPORTANT: This method takes ownership of the trace_field - the calling
 * method must not reference it after this call.
 *
 * @param msg Pointer to an outgoing message.
 * @param trace_field Pointer to a composed field representing the list that
 * will be used as the value for the QD_MA_TRACE map entry.  If null, the
 * message will not have a QA_MA_TRACE message annotation field.  Ownership of
 * this field is transferred to the message.
 *
 */
void qd_message_set_trace_annotation(qd_message_t *msg, qd_composed_field_t *trace_field);

/**
 * Set the value for the QD_MA_TO field in the outgoing message annotations for
 * the message.
 *
 * IMPORTANT: This method takes ownership of the to_field - the calling
 * method must not reference it after this call.
 *
 * @param msg Pointer to an outgoing message.
 * @param to_field Pointer to a composed field representing the to override
 * address that will be used as the value for the QD_MA_TO map entry.  If null,
 * the message will not have a QA_MA_TO message annotation field.  Ownership of
 * this field is transferred to the message.
 *
 */
void qd_message_set_to_override_annotation(qd_message_t *msg, qd_composed_field_t *to_field);

/**
 * Set a phase for the phase annotation in the message.
 *
 * @param msg Pointer to an outgoing message.
 * @param phase The phase of the address for the outgoing message.
 *
 */
void qd_message_set_phase_annotation(qd_message_t *msg, int phase);
int  qd_message_get_phase_annotation(const qd_message_t *msg);

/**
 * Set the value for the QD_MA_INGRESS field in the outgoing message
 * annotations for the message.
 *
 * IMPORTANT: This method takes ownership of the ingress_field - the calling
 * method must not reference it after this call.
 *
 * @param msg Pointer to an outgoing message.
 * @param ingress_field Pointer to a composed field representing ingress router
 * that will be used as the value for the QD_MA_INGRESS map entry.  If null,
 * the message will not have a QA_MA_INGRESS message annotation field.
 * Ownership of this field is transferred to the message.
 *
 */
void qd_message_set_ingress_annotation(qd_message_t *msg, qd_composed_field_t *ingress_field);

/**
 * Receive message data frame by frame via a delivery.  This function may be called more than once on the same
 * delivery if the message spans multiple frames. Always returns a message. The message buffers are filled up to the point with the data that was been received so far.
 * The buffer keeps filling up on successive calls to this function.
 *
 * @param delivery An incoming delivery from a link
 * @return A pointer to the complete message or 0 if the message is not yet complete.
 */
qd_message_t *qd_message_receive(pn_delivery_t *delivery);

/**
 * Returns the PN_DELIVERY_CTX record from the attachments
 *
 * @param delivery An incoming delivery from a link
 * @return - pointer to qd_message_t object
 */
qd_message_t * qd_get_message_context(pn_delivery_t *delivery);

/**
 * Send the message outbound on an outgoing link.
 *
 * @param msg A pointer to a message to be sent.
 * @param link The outgoing link on which to send the message.
 * @param strip_outbound_annotations [in] annotation control flag
 * @param restart_rx [out] indication to wake up receive process
 * @param q3_stalled [out] indicates that the link is stalled due to proton-buffer-full
 */
void qd_message_send(qd_message_t *msg, qd_link_t *link, bool strip_outbound_annotations, bool *restart_rx, bool *q3_stalled);

/**
 * Check that the message is well-formed up to a certain depth.  Any part of the message that is
 * beyond the specified depth is not checked for validity.
 *
 * Note: some message sections are optional - QD_MESSAGE_OK is returned if the
 * optional section is not present, as that is valid.
 */
typedef enum {
    QD_MESSAGE_DEPTH_INVALID,     // corrupt or malformed message detected
    QD_MESSAGE_DEPTH_OK,          // valid up to depth, including 'depth' if not optional
    QD_MESSAGE_DEPTH_INCOMPLETE   // have not received up to 'depth', or partial depth
} qd_message_depth_status_t;

qd_message_depth_status_t qd_message_check_depth(const qd_message_t *msg, qd_message_depth_t depth);

/**
 * Return an iterator for the requested message field.  If the field is not in the message,
 * return NULL.
 *
 * @param msg A pointer to a message.
 * @param field The field to be returned via iterator.
 * @return A field iterator that spans the requested field.
 */
qd_iterator_t *qd_message_field_iterator_typed(qd_message_t *msg, qd_message_field_t field);
qd_iterator_t *qd_message_field_iterator(qd_message_t *msg, qd_message_field_t field);

ssize_t qd_message_field_length(qd_message_t *msg, qd_message_field_t field);
ssize_t qd_message_field_copy(qd_message_t *msg, qd_message_field_t field, char *buffer, size_t *hdr_length);

//
// Functions for composed messages
//

// Convenience Functions
void qd_message_compose_1(qd_message_t *msg, const char *to, qd_buffer_list_t *buffers);
void qd_message_compose_2(qd_message_t *msg, qd_composed_field_t *content);
void qd_message_compose_3(qd_message_t *msg, qd_composed_field_t *content1, qd_composed_field_t *content2);
void qd_message_compose_4(qd_message_t *msg, qd_composed_field_t *content1, qd_composed_field_t *content2, qd_composed_field_t *content3);

/** Put string representation of a message suitable for logging in buffer.
 * @return buffer
 */
char* qd_message_repr(qd_message_t *msg, char* buffer, size_t len, qd_log_bits log_message);
/** Recommended buffer length for qd_message_repr */
int qd_message_repr_len();

qd_log_source_t* qd_message_log_source();

/**
 * Accessor for message field ingress
 * 
 * @param msg A pointer to the message
 * @return the parsed field
 */
qd_parsed_field_t *qd_message_get_ingress    (qd_message_t *msg);

/**
 * Accessor for message field phase
 * 
 * @param msg A pointer to the message
 * @return the parsed field
 */
qd_parsed_field_t *qd_message_get_phase      (qd_message_t *msg);

/**
 * Accessor for message field to_override
 * 
 * @param msg A pointer to the message
 * @return the parsed field
 */
qd_parsed_field_t *qd_message_get_to_override(qd_message_t *msg);

/**
 * Accessor for message field trace
 * 
 * @param msg A pointer to the message
 * @return the parsed field
 */
qd_parsed_field_t *qd_message_get_trace      (qd_message_t *msg);

/**
 * Accessor for message field phase
 * 
 * @param msg A pointer to the message
 * @return the phase as an integer
 */
int                qd_message_get_phase_val  (qd_message_t *msg);

/*
 * Should the message be discarded.
 * A message can be discarded if the disposition is released or rejected.
 *
 * @param msg A pointer to the message.
 **/
bool qd_message_is_discard(qd_message_t *msg);

/**
 *Set the discard field on the message to to the passed in boolean value.
 *
 * @param msg A pointer to the message.
 * @param discard - the boolean value of discard.
 */
void qd_message_set_discard(qd_message_t *msg, bool discard);

/**
 * Has the message been completely received?
 * Return true if the message is fully received
 * Returns false if only the partial message has been received, if there is more of the message to be received.
 *
 * @param msg A pointer to the message.
 */
bool qd_message_receive_complete(qd_message_t *msg);

/**
 * Returns true if the message has been completely received AND the message has been completely sent.
 */
bool qd_message_send_complete(qd_message_t *msg);

/**
 * Returns true if the delivery tag has already been sent.
 */
bool qd_message_tag_sent(qd_message_t *msg);


/**
 * Sets if the delivery tag has already been sent out or not.
 */
void qd_message_set_tag_sent(qd_message_t *msg, bool tag_sent);

/**
 * Increase the fanout of the message by 1.
 *
 * @param in_msg A pointer to the inbound message.
 * @param out_msg A pointer to the outbound message or 0 if forwarding to a
 * local subscriber.
 */
void qd_message_add_fanout(qd_message_t *in_msg,
                           qd_message_t *out_msg);

/**
 * Disable the Q2-holdoff for this message.
 *
 * @param msg A pointer to the message
 */
void qd_message_Q2_holdoff_disable(qd_message_t *msg);

/**
 * Test if attempt to retreive message data through qd_message_recv should block
 * due to Q2 input holdoff limit being exceeded. This message has enough
 * buffers in the internal buffer chain and any calls to to qd_message_receive
 * will not result in a call to pn_link_receive to retrieve more data.
 *
 * @param msg A pointer to the message
 */
bool qd_message_Q2_holdoff_should_block(qd_message_t *msg);

/**
 * Test if a message that is blocked by Q2 input holdoff has enough room
 * to begin receiving again. This message has transmitted and disposed of
 * enough buffers to begin receiving more data from the underlying proton link.
 *
 * @param msg A pointer to the message
 */
bool qd_message_Q2_holdoff_should_unblock(qd_message_t *msg);

/**
 * Return qd_link through which the message is being received.
 * @param msg A pointer to the message
 * @return the qd_link
 */
qd_link_t * qd_message_get_receiving_link(const qd_message_t *msg);

/**
 * Return message aborted state
 * @param msg A pointer to the message
 * @return true if the message has been aborted
 */
bool qd_message_aborted(const qd_message_t *msg);

/**
 * Set the aborted flag on the message.
 * @param msg A pointer to the message
 * @param aborted
 */
void qd_message_set_aborted(const qd_message_t *msg, bool aborted);

/**
 * Return message priority
 * @param msg A pointer to the message
 * @return The message priority value. Default if not present.
 */
uint8_t qd_message_get_priority(qd_message_t *msg);

/**
 * True if message is larger that maxMessageSize
 * @param msg A pointer to the message
 * @return 
 */
bool qd_message_oversize(const qd_message_t *msg);

///@}

#endif
