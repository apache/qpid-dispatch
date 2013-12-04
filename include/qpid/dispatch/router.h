#ifndef __dispatch_router_h__
#define __dispatch_router_h__ 1
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

#include <qpid/dispatch/dispatch.h>
#include <qpid/dispatch/message.h>
#include <qpid/dispatch/iterator.h>
#include <stdbool.h>

/**
 * Address Forwarding Semantics
 *
 * MULTICAST   - Send a copy of the message to every subscribed consumer
 * LOWEST_COST - Send a copy to only the lowest-cost (closest) subscribed consumer
 * ROUND_ROBIN - Distribute messages approximately evenly across subscribed consumers
 * BALANCED    - Distribute messages based on cost/buffer-depth/outstanding-acks
 */
typedef enum {
    QD_FORWARD_MULTICAST,
    QD_FORWARD_LOWEST_COST,
    QD_FORWARD_ROUND_ROBIN,
    QD_FORWARD_BALANCED
} qd_forwarding_rule_t;

typedef struct {
    bool                 bypass_valid_origins;
    qd_forwarding_rule_t forward_rule;
} qd_address_semantics_t;

typedef struct qd_address_t qd_address_t;

typedef void (*qd_router_message_cb_t)(void *context, qd_message_t *msg, int link_id);

const char *qd_router_id(const qd_dispatch_t *qd);

qd_address_t *qd_router_register_address(qd_dispatch_t                *qd,
                                         const char                   *address,
                                         qd_router_message_cb_t        handler,
                                         const qd_address_semantics_t *semantics,
                                         void                         *context);

void qd_router_unregister_address(qd_address_t *address);


void qd_router_send(qd_dispatch_t       *qd,
                    qd_field_iterator_t *address,
                    qd_message_t        *msg);

void qd_router_send2(qd_dispatch_t *qd,
                     const char    *address,
                     qd_message_t  *msg);

void qd_router_build_node_list(qd_dispatch_t *qd, qd_composed_field_t *field);

#endif
