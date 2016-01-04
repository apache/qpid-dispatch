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

typedef void (*qdr_forward_message_t) (qdr_core_t      *core,
                                       qdr_forwarder_t *forw,
                                       qd_message_t    *msg,
                                       qdr_delivery_t  *in_delivery);
typedef void (*qdr_forward_attach_t) (qdr_core_t      *core,
                                      qdr_forwarder_t *forw,
                                      qdr_link_t      *link);

struct qdr_forwarder_t {
    qdr_forward_message_t forward_message;
    qdr_forward_attach_t  forward_attach;
    bool                  bypass_valid_origins;
};

//==================================================================================
// Built-in Forwarders
//==================================================================================

void qdr_forward_multicast(qdr_core_t      *core,
                           qdr_forwarder_t *forw,
                           qd_message_t    *msg,
                           qdr_delivery_t  *in_delivery)
{
}


void qdr_forward_closest(qdr_core_t      *core,
                         qdr_forwarder_t *forw,
                         qd_message_t    *msg,
                         qdr_delivery_t  *in_delivery)
{
}


void qdr_forward_balanced(qdr_core_t      *core,
                          qdr_forwarder_t *forw,
                          qd_message_t    *msg,
                          qdr_delivery_t  *in_delivery)
{
}


void qdr_forward_link_balanced(qdr_core_t      *core,
                               qdr_forwarder_t *forw,
                               qdr_link_t      *link)
{
}


//==================================================================================
// In-Thread API Functions
//==================================================================================

qdr_forwarder_t *qdr_new_forwarder(qdr_forward_message_t fm, qdr_forward_attach_t fa, bool bypass_valid_origins)
{
    qdr_forwarder_t *forw = NEW(qdr_forwarder_t);

    forw->forward_message      = fm;
    forw->forward_attach       = fa;
    forw->bypass_valid_origins = bypass_valid_origins;

    return forw;
}


void qdr_forwarder_setup_CT(qdr_core_t *core)
{
    //
    // Create message forwarders
    //
    core->forwarders[QD_SEMANTICS_MULTICAST_FLOOD]  = qdr_new_forwarder(qdr_forward_multicast, 0, true);
    core->forwarders[QD_SEMANTICS_MULTICAST_ONCE]   = qdr_new_forwarder(qdr_forward_multicast, 0, false);
    core->forwarders[QD_SEMANTICS_ANYCAST_CLOSEST]  = qdr_new_forwarder(qdr_forward_closest,   0, false);
    core->forwarders[QD_SEMANTICS_ANYCAST_BALANCED] = qdr_new_forwarder(qdr_forward_balanced,  0, false);

    //
    // Create link forwarders
    //
    core->forwarders[QD_SEMANTICS_LINK_BALANCED] = qdr_new_forwarder(0, qdr_forward_link_balanced, false);
}


qdr_forwarder_t *qdr_forwarder_CT(qdr_core_t *core, qd_address_semantics_t semantics)
{
    if (semantics <= QD_SEMANTICS_LINK_BALANCED)
        return core->forwarders[semantics];
    return 0;
}


void qdr_forward_message_CT(qdr_core_t *core, qdr_forwarder_t *forwarder, qd_message_t *msg, qdr_delivery_t *in_delivery)
{
}


void qdr_forward_attach_CT(qdr_core_t *core, qdr_forwarder_t *forwarder, qdr_link_t *in_link)
{
}

