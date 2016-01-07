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


static void qdr_link_deliver_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_send_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard);


//==================================================================================
// Internal Functions
//==================================================================================


//==================================================================================
// Interface Functions
//==================================================================================

qdr_delivery_t *qdr_link_deliver(qdr_link_t *link, qd_message_t *msg, qd_field_iterator_t *ingress, bool settled)
{
    qdr_action_t   *action = qdr_action(qdr_link_deliver_CT, "link_deliver");
    qdr_delivery_t *dlv    = new_qdr_delivery_t();

    ZERO(dlv);
    dlv->link    = link;
    dlv->msg     = msg;
    dlv->to_addr = 0;
    dlv->origin  = ingress;
    dlv->settled = settled;

    action->args.connection.delivery = dlv;
    qdr_action_enqueue(link->core, action);
    return dlv;
}


qdr_delivery_t *qdr_link_deliver_to(qdr_link_t *link, qd_message_t *msg,
                                    qd_field_iterator_t *ingress, qd_field_iterator_t *addr, bool settled)
{
    qdr_action_t   *action = qdr_action(qdr_link_deliver_CT, "link_deliver");
    qdr_delivery_t *dlv    = new_qdr_delivery_t();

    ZERO(dlv);
    dlv->link    = link;
    dlv->msg     = msg;
    dlv->to_addr = addr;
    dlv->origin  = ingress;
    dlv->settled = settled;

    action->args.connection.delivery = dlv;
    qdr_action_enqueue(link->core, action);
    return dlv;
}


qdr_delivery_t *qdr_link_deliver_to_routed_link(qdr_link_t *link, qd_message_t *msg)
{
    // TODO - Implement this.  Bypass the CT?
    return 0;
}


void qdr_send_to1(qdr_core_t *core, qd_message_t *msg, qd_field_iterator_t *addr, bool exclude_inprocess, bool control)
{
    qdr_action_t *action = qdr_action(qdr_send_to_CT, "send_to");
    //action->args.io.address           = qdr_field(addr);  // TODO - fix this
    action->args.io.message           = qd_message_copy(msg);
    action->args.io.exclude_inprocess = exclude_inprocess;
    action->args.io.control           = control;

    qdr_action_enqueue(core, action);
}


void qdr_send_to2(qdr_core_t *core, qd_message_t *msg, const char *addr, bool exclude_inprocess, bool control)
{
    qdr_action_t *action = qdr_action(qdr_send_to_CT, "send_to");
    action->args.io.address           = qdr_field(addr);
    action->args.io.message           = qd_message_copy(msg);
    action->args.io.exclude_inprocess = exclude_inprocess;
    action->args.io.control           = control;

    qdr_action_enqueue(core, action);
}


void qdr_delivery_set_context(qdr_delivery_t *delivery, void *context)
{
    delivery->context = context;
}


void *qdr_delivery_get_context(qdr_delivery_t *delivery)
{
    return delivery->context;
}


uint64_t qdr_delivery_disposition(const qdr_delivery_t *delivery)
{
    return delivery->disposition;
}


bool qdr_delivery_is_settled(const qdr_delivery_t *delivery)
{
    return delivery->settled;
}


//==================================================================================
// In-Thread Functions
//==================================================================================

static void qdr_link_deliver_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_delivery_t *dlv   = action->args.connection.delivery;
    qdr_link_t     *link  = dlv->link;
    int             count = 0;

    if (DEQ_IS_EMPTY(link->undelivered)) {
        qdr_address_t *addr = link->owning_addr;
        if (!addr && dlv->to_addr) {
            qd_hash_retrieve(core->addr_hash, dlv->to_addr, (void**) &addr);
            if (addr)
                count = qdr_forward_message_CT(core, addr, dlv->msg, dlv, false, link->link_type == QD_LINK_CONTROL);
        }
    }

    if (count == 0) {
        if (link->owning_addr)
            //
            // Message was not delivered and the link is not anonymous.
            // Queue the message for later delivery (when the address gets
            // a valid destination).
            //
            DEQ_INSERT_TAIL(link->undelivered, dlv);
        else {
            //
            // TODO - Release the delivery
            //
        }
    } else if (count == 1) {
        if (qdr_delivery_is_settled(dlv))
            DEQ_INSERT_TAIL(link->unsettled, dlv);
    } else {
        //
        // The count is greater than one.  Do something!  TODO
        //
    }
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
            (void) qdr_forward_message_CT(core, addr, msg, 0, action->args.io.exclude_inprocess, action->args.io.control);
        else
            qd_log(core->log, QD_LOG_DEBUG, "In-process send to an unknown address");
    }

    qdr_field_free(addr_field);
    qd_message_free(msg);
}

