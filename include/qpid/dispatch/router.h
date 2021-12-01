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
#include "qpid/dispatch/dispatch.h"
#include "qpid/dispatch/iterator.h"
#include "qpid/dispatch/message.h"

#include <stdbool.h>

#define QD_ROUTER_ID_MAX 127  // max length of router id in chars

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
    QD_TREATMENT_UNAVAILABLE      = 5
} qd_address_treatment_t;

#include "qpid/dispatch/router_core.h"

/** Message forwarding descriptor
 *
 * Defines a forwarding method that can be associated with an address
 * (qd_address_t).  This method is called for each message that matches the
 * associated address.  The qd_router_forwarder_t is a 'base class' that can be
 * subclassed to provide a per-forwarder context for custom forwarding
 * algorithms.
 */
typedef struct qd_router_forwarder_t qd_router_forwarder_t;
struct qd_router_forwarder_t {

    /** forwarding method
     *
     * Returns true if the message was successfully forwarded or has been
     * scheduled to be forwarded at a later time.  Returns false if the handler
     * is unable to forward the message.
     *
     * If the message is going to be forwarded at a later time (asynchronous
     * forwarding), then this method must make a copy of the message.
     *
     * NOTE: ** Called with router lock held! **
     */
    bool (*forward)(qd_router_forwarder_t *forwarder,
                    qd_router_t *router,
                    qd_message_t *msg,
                    qd_router_delivery_t *delivery,
                    qd_address_t *addr,
                    qd_iterator_t *ingress_iterator,
                    bool is_direct);

    /** release the descriptor
     *
     * Called when the associated qd_address_t is freed.
     * NOTE: ** Called with router lock held! **
     */
    void (*release)(qd_router_forwarder_t *forwarder);
};

typedef void (*qd_router_message_cb_t)(void *context, qd_message_t *msg, int link_id);

const char *qd_router_id(void);
const uint8_t *qd_router_id_encoded(size_t *len);  // encoded as AMQP STR

qdr_core_t *qd_router_core(qd_dispatch_t *qd);

/** Register an address in the router's hash table.
 * @param qd Pointer to the dispatch instance.
 * @param address String form of address
 * @param on_message Optional callback to be called when a message is received
 * for the address.
 * @param context Context to be passed to the on_message handler.
 * @param treatment Treatment for the address.
 * @param global True if the address is global.
 * @param forwarder Optional custom forwarder to use when a message is received
 * for the address.  If null, a default forwarder based on the treatment will
 * be used.
 */
qd_address_t *qd_router_register_address(qd_dispatch_t          *qd,
                                         const char             *address,
                                         qd_router_message_cb_t  on_message,
                                         void                   *context,
                                         qd_address_treatment_t  treatment,
                                         bool                    global,
                                         qd_router_forwarder_t  *forwarder);

void qd_router_unregister_address(qd_address_t *address);

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

/** Retrieve the proper forwarder for a given semantic */
qd_router_forwarder_t *qd_router_get_forwarder(qd_address_treatment_t t);

/** Retrieve the routers current memory usage (in bytes) */
uint64_t qd_router_memory_usage(void);

///@}

#endif
