#ifndef router_core_address_lookup_utils_h
#define router_core_address_lookup_utils_h 1
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

#include "qpid/dispatch/compose.h"
#include "qpid/dispatch/container.h"
#include "qpid/dispatch/iterator.h"
//
// API for building address lookup request messages.  The message properties
// and body fields are handled separately so they can be passed directly to the
// core client API.
//

#define PROTOCOL_VERSION 1

typedef enum {
    // note: keep unit test in sync
    OPCODE_INVALID,
    OPCODE_LINK_ROUTE_LOOKUP,
} address_lookup_opcode_t;

typedef enum {
    // note: keep unit test in sync
    QCM_ADDR_LOOKUP_OK,
    QCM_ADDR_LOOKUP_BAD_VERSION,
    QCM_ADDR_LOOKUP_BAD_OPCODE,
    QCM_ADDR_LOOKUP_NOT_FOUND,
    QCM_ADDR_LOOKUP_INVALID_REQUEST,
} qcm_address_lookup_status_t;


/**
 * Create the message properties and body for a link route address lookup.  The
 * returned properties and body can be passed directly to
 * qdrc_client_request_CT().
 *
 * @param address - fully qualified link route address to look up.
 * @param dir - QD_INCOMING or QD_OUTGOING
 * @param properties - return value for message application properties section
 * @param body - return value for message body
 * @return zero on success
 */
int qcm_link_route_lookup_request(qd_iterator_t        *address,
                                  qd_direction_t        dir,
                                  qd_composed_field_t **properties,
                                  qd_composed_field_t **body);


/**
 * Parse out the payload of the link route lookup reply message.  The
 * properties and body fields are provided by the on_reply_cb() callback passed
 * to the qdrc_client_request_CT() call.
 *
 * @param properties - application properties as returned in the reply
 * @param body - body from reply message
 * @param is_link_route - set True if the address is a link route address that
 *        exists in the route tables of the queried router.
 * @param has_destination - if is_link_route this indicates whether or not the
 *        queried router has active destinations for this link route.
 * @return QCM_ADDR_LOOKUP_OK if query is successful, else and error code
 */
qcm_address_lookup_status_t qcm_link_route_lookup_decode(qd_iterator_t *properties,
                                                         qd_iterator_t *body,
                                                         bool          *is_link_route,
                                                         bool          *has_destinations);
#endif // router_core_address_lookup_utils_h
