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
    qdr_action_t *action = qdr_action(qdr_link_delivery_CT, "link_delivery");

    qdr_action_enqueue(core, action);
}


qdr_delivery_t *qdr_link_deliver_to(qdr_link_t *link, pn_delivery_t *delivery, qd_message_t *msg, qd_field_iterator_t *addr)
{
    qdr_action_t *action = qdr_action(qdr_link_delivery_to_CT, "link_delivery_to");

    qdr_action_enqueue(core, action);
}


void qdr_send_to(qdr_core_t *core, qd_message_t *msg, const char *addr, bool exclude_inprocess)
{
    qdr_action_t *action = qdr_action(qdr_send_to_CT, "send_to");

    qdr_action_enqueue(core, action);
}


//==================================================================================
// In-Thread Functions
//==================================================================================

static void qdr_link_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;
}


static void qdr_link_delivery_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;
}


static void qdr_send_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;
}

