#ifndef __dispatch_annotation_h__
#define __dispatch_annotation_h__ 1

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

#include <qpid/dispatch/router.h>

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
 * Annotates the message with dispatch router annotations.
 *
 * IMPORTANT: The inbound annotations are stripped if strip_inbound_annotations is true.
 *
 * @param qd_router_t Pointer to the router.
 * @param qd_parsed_field_t Pointer to the message annotation.
 * @param qd_message_t Pointer to the message.
 * @param drop Pointer indicating if the message has to be dropped in case of message looping.
 * @param to_override Override address.
 * @param node_id Pointer to the node id of the router.
 * @param strip_inbound_annotations boolean indicating if the in bound annotations must be stripped.
 * @return - the iterator to the ingress field annotation if it was present
 *
 */
qd_field_iterator_t *router_annotate_message(qd_router_t       *router,
                                             qd_parsed_field_t *in_ma,
                                             qd_message_t      *msg,
                                             int               *drop,
                                             const char        *to_override,
                                             char *node_id,
                                             bool strip_inbound_annotations);

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
 * @param to_field Pointer to a composed field representing the to overrid
 * address that will be used as the value for the QD_MA_TO map entry.  If null,
 * the message will not have a QA_MA_TO message annotation field.  Ownership of
 * this field is transferred to the message.
 *
 */
void qd_message_set_to_override_annotation(qd_message_t *msg, qd_composed_field_t *to_field);

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

#endif
