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

#ifndef qd_router_core_endpoint_types
#define qd_router_core_endpoint_types 1

typedef struct qdrc_endpoint_t qdrc_endpoint_t;

#endif



#ifndef qd_router_core_endpoint
#define qd_router_core_endpoint 1

#include "router_core_private.h"

typedef bool (*qdrc_first_attach_t) (void             *bind_context,
                                     qdrc_endpoint_t  *endpoint,
                                     void            **link_context,
                                     qdr_error_t     **error);

typedef bool (*qdrc_second_attach_t) (void *link_context);

typedef void (*qdrc_flow_t) (void *link_context,
                             int   available_credit,
                             int   available_window);

typedef void (*qdrc_update_t) (void           *link_context,
                               qdr_delivery_t *delivery);

typedef void (*qdrc_transfer_t) (void           *link_context,
                                 qdr_delivery_t *delivery;
                                 qd_message_t   *message);

typedef void (*qdrc_detach_t) (void        *link_context,
                               qdr_error_t *error);


typedef struct qdrc_endpoint_descriptor_t {
    qdrc_first_attach_t   on_first_attach;
    qdrc_second_attach_t  on_second_attach;
    qdrc_flow_t           on_flow;
    qdrc_update_t         on_update;
    qdrc_transfer_t       on_transfer;
    qdrc_detach_t         on_detach;
} qdrc_endpoint_descriptor_t;


void qdrc_endpoint_bind_mobile_address(qdr_core_t                 *core,
                                       const char                 *address,
                                       int                         phase,
                                       qdrc_endpoint_descriptor_t *descriptor,
                                       void                       *bind_context);


qdrc_endpoint_t *qdrc_endpoint_create_link(qdr_core_t                 *core,
                                           qd_direction_t              dir,
                                           qdr_terminus_t             *source,
                                           qdr_terminus_t             *target,
                                           qdrc_endpoint_descriptor_t *descriptor,
                                           void                       *link_context);

qd_direction_t qdrc_endpoint_get_direction(const qdrc_endpoint_t *endpoint);

void qdrc_endpoint_flow(qdrc_endpoint_t *endpoint, int credit_added);

void qdrc_endpoint_send(qdrc_endpoint_t *endpoint, qdr_delivery_t *delivery);

void qdrc_endpoint_update(qdrc_endpoint_t *endpoint, qdr_delivery_t *delivery);

void qdrc_endpoint_detach(qdrc_endpoint_t *endpoint, qdr_error_t *error);

#endif
