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

/**@file
 * Register addresses, send messages.
 *
 * @defgroup router router
 *
 * Register addresses, send messages.
 * @{
 */
#include <qpid/dispatch/dispatch.h>
#include <qpid/dispatch/message.h>
#include <qpid/dispatch/iterator.h>
#include <stdbool.h>

typedef struct qdr_core_t   qdr_core_t;
typedef struct qd_router_t  qd_router_t;
typedef struct qd_address_t qd_address_t;
typedef struct qd_router_delivery_t qd_router_delivery_t;

typedef enum {
    QD_TREATMENT_MULTICAST_FLOOD  = 0,
    QD_TREATMENT_MULTICAST_ONCE   = 1,
    QD_TREATMENT_ANYCAST_CLOSEST  = 2,
    QD_TREATMENT_ANYCAST_BALANCED = 3,
    QD_TREATMENT_LINK_BALANCED    = 4,
    QD_TREATMENT_EXCHANGE         = 5,
    // must be updated when adding new treatments:
    QD_TREATMENT_LAST = QD_TREATMENT_EXCHANGE
} qd_address_treatment_t;

#include <qpid/dispatch/router_core.h>

const char *qd_router_id(const qd_dispatch_t *qd);

qdr_core_t *qd_router_core(qd_dispatch_t *qd);

void qd_address_set_redirect(qd_address_t *address, qd_address_t *redirect);

void qd_address_set_static_cc(qd_address_t *address, qd_address_t *cc);

void qd_address_set_dynamic_cc(qd_address_t *address, qd_address_t *cc);

/** Send msg to local links and next-hops for address */
void qd_router_send(qd_dispatch_t *qd,
                    qd_iterator_t *address,
                    qd_message_t  *msg);

/** Send msg to local links and next-hops for address */
void qd_router_send2(qd_dispatch_t *qd,
                     const char    *address,
                     qd_message_t  *msg);

void qd_router_build_node_list(qd_dispatch_t *qd, qd_composed_field_t *field);

/** String form of address for logging */
const char* qd_address_logstr(qd_address_t* address);

///@}

#endif
