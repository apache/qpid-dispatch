#ifndef qd_router_core_client_api_h
#define qd_router_core_client_api_h 1
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

//
// A simple request/response client messaging API using core terminated links
//

#include "router_core_private.h"


typedef struct qdrc_client_t qdrc_client_t;


//
// client application callbacks
//


/**
 * Client links have changed state
 *
 * @param core
 * @param client - core client returned by qdrc_client_CT()
 * @param user_context - as passed to qdrc_client_CT()
 * @param active - true if both links are opened, else false
 */
typedef void (*qdrc_client_on_state_CT_t)(qdr_core_t    *core,
                                          qdrc_client_t *client,
                                          void          *user_context,
                                          bool           active);


/**
 * Credit has become available for sending messages
 *
 * @param core
 * @param client - core client returned by qdrc_client_CT()
 * @param user_context - as passed to qdrc_client_CT()
 * @param available_credit - current credit allocation
 * @param drain - if true client must consume all credit
 */
typedef void (*qdrc_client_on_flow_CT_t)(qdr_core_t    *core,
                                         qdrc_client_t *client,
                                         void          *user_context,
                                         int            available_credit,
                                         bool           drain);

/**
 * Final disposition received for sent message
 *
 * @param core
 * @param client - core client returned by qdrc_client_CT()
 * @param user_context - as passed to qdrc_client_CT()
 * @param request_context - as passed to qdrc_client_request_CT()
 * @param disposition - for the associated sent message
 */
typedef void (*qdrc_client_on_ack_CT_t)(qdr_core_t    *core,
                                        qdrc_client_t *client,
                                        void          *user_context,
                                        void          *request_context,
                                        uint64_t       disposition);

/**
 * A reply message has arrived for a given request
 *
 * @param core
 * @param client - core client returned by qdrc_client_CT()
 * @param user_context - as passed to qdrc_client_CT()
 * @param app_properties - application properties header from reply.
 *        Ownership is handed off to this callback - user must free the
 *        iterator when done.
 * @param body - message body.  Ownership is handed off to this callback
 *        - user must free the iterator when done.
 * @return final disposition for the received reply message
 */
typedef uint64_t (*qdrc_client_on_reply_CT_t)(qdr_core_t    *core,
                                              qdrc_client_t *client,
                                              void          *user_context,
                                              void          *request_context,
                                              qd_iterator_t *app_properties,
                                              qd_iterator_t *body);


typedef void (*qdrc_client_request_done_CT_t)(qdr_core_t    *core,
                                              qdrc_client_t *client,
                                              void          *user_context,
                                              void          *request_context,
                                              const char    *error);


/**
 * Create a request/response client
 *
 * @param core
 * @param conn - connection over which links will be created.
 * @param target - for messages sent by this client.
 * @param credit_window - for receiver link (credit loop)
 * @param user_context - context that will be passed to callbacks
 * @param on_state_cb - callback when link state changes
 * @param on_flow_cb - callback when sender credit is updated.
 * @return a new core client
 */
qdrc_client_t *qdrc_client_CT(qdr_core_t                *core,
                              qdr_connection_t          *conn,
                              qdr_terminus_t            *target,
                              uint32_t                   credit_window,
                              void                      *user_context,
                              qdrc_client_on_state_CT_t  on_state_cb,
                              qdrc_client_on_flow_CT_t   on_flow_cb);


/**
 * Free a request/response client
 *
 * @param client - as returned by qdrc_client_CT()
 */
void qdrc_client_free_CT(qdrc_client_t *client);


/**
 * Send a request message
 *
 * @param client - as returned by qdrc_client_CT()
 * @param request_context - context for this request that will be passed to
 *        callbacks
 * @param app_properties - the application properties for the sent message.
 *        Ownership is transferred to this call - the caller must not reference the
 *        composed field on return.
 * @param body - the message body for the sent message. Ownership is transferred
 *        to this call - the caller must not reference the composed field on return.
 * @param timeout - abort the request if it does not complete in timeout
 *                  seconds. On timeout done_cb will be called with error
 *                  set to "Timed out" (timeout=0 means never timeout).
 * @param on_reply_cb - (optional) invoked when reply message arrives
 * @param on_ack_cb - (optional) invoked when sent message disposition is set
 * @param done_cb - (optional) called once request is done (for cleanup)
 * @return zero on success.
 */
int qdrc_client_request_CT(qdrc_client_t                 *client,
                           void                          *request_context,
                           qd_composed_field_t           *app_properties,
                           qd_composed_field_t           *body,
                           uint32_t                       timeout,
                           qdrc_client_on_reply_CT_t      on_reply_cb,
                           qdrc_client_on_ack_CT_t        on_ack_cb,
                           qdrc_client_request_done_CT_t  done_cb);

#endif // #define qd_router_core_client_api_h 1
