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

#include "router_core_private.h"
#include <qpid/dispatch/amqp.h>
#include <stdio.h>

ALLOC_DEFINE(qdr_delivery_t);

static void qdr_link_deliver_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_deliver_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_send_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard);


//==================================================================================
// Internal Functions
//==================================================================================


//==================================================================================
// Interface Functions
//==================================================================================

qdr_delivery_t *qdr_link_deliver(qdr_link_t *link, pn_delivery_t *delivery, qd_message_t *msg)
{
    qdr_action_t   *action = qdr_action(qdr_link_deliver_CT, "link_deliver");
    qdr_delivery_t *dlv    = new_qdr_delivery_t();

    qdr_action_enqueue(link->core, action);
    return dlv;
}


qdr_delivery_t *qdr_link_deliver_to(qdr_link_t *link, pn_delivery_t *delivery, qd_message_t *msg, qd_field_iterator_t *addr)
{
    qdr_action_t   *action = qdr_action(qdr_link_deliver_to_CT, "link_deliver_to");
    qdr_delivery_t *dlv    = new_qdr_delivery_t();

    qdr_action_enqueue(link->core, action);
    return dlv;
}


void qdr_send_to(qdr_core_t *core, qd_message_t *msg, const char *addr, bool exclude_inprocess)
{
    qdr_action_t *action = qdr_action(qdr_send_to_CT, "send_to");
    action->args.io.address           = qdr_field(addr);
    action->args.io.message           = qd_message_copy(msg);
    action->args.io.exclude_inprocess = exclude_inprocess;

    qdr_action_enqueue(core, action);
}


//==================================================================================
// In-Thread Functions
//==================================================================================

static void qdr_route_message_CT(qdr_core_t     *core,
                                 qdr_address_t  *addr,
                                 qd_message_t   *msg,
                                 qdr_delivery_t *dlv,
                                 bool            exclude_inprocess)
{
    const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
    printf("qdr_route_message_CT - %s, %s\n", key, exclude_inprocess ? "yes" : "no");
}


static void qdr_link_deliver_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;
}


static void qdr_link_deliver_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;
}


static void qdr_send_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_field_t  *addr_field = action->args.io.address;
    qd_message_t *msg        = action->args.io.message;

    if (!discard) {
        qdr_address_t *addr;
        qd_address_iterator_reset_view(addr_field->iterator, ITER_VIEW_ADDRESS_HASH);
        qd_hash_retrieve(core->addr_hash, addr_field->iterator, (void**) &addr);
        if (addr)
            qdr_route_message_CT(core, addr, msg, 0, action->args.io.exclude_inprocess);
        else
            qd_log(core->log, QD_LOG_DEBUG, "In-process send to an unknown address");
    }

    qdr_field_free(addr_field);
    qd_message_free(msg);
}

