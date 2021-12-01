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

#include "core_attach_address_lookup.h"
#include "core_link_endpoint.h"
#include "core_events.h"
#include "module.h"
#include "router_core_private.h"

#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/discriminator.h"
#include "qpid/dispatch/amqp.h"

#include <stdio.h>


typedef struct qcm_heartbeat_edge_t {
    qdr_core_t                *core;
    qdrc_event_subscription_t *event_sub;
    qdr_connection_t          *edge_conn;
    qdr_core_timer_t          *timer;
    qdrc_endpoint_t           *endpoint;
    uint32_t                   link_credit;
    uint32_t                   next_msg_id;
} qcm_heartbeat_edge_t;


//================================================================================
// Core Link API Handlers
//================================================================================

/**
 * Event - The attachment of a link initiated by the core-endpoint was completed
 *
 * Note that core-endpoint incoming links are _not_ provided credit by the core.  It
 * is the responsibility of the core-endpoint to supply credit at the appropriate time
 * by calling qdrc_endpoint_flow_CT.
 *
 * @param link_context The opaque context supplied in the call to qdrc_endpoint_create_link_CT
 * @param remote_source Pointer to the remote source terminus of the link
 * @param remote_target Pointer to the remote target terminus of the link
 */
static void on_second_attach(void           *link_context,
                             qdr_terminus_t *remote_source,
                             qdr_terminus_t *remote_target)
{
    qcm_heartbeat_edge_t *client = (qcm_heartbeat_edge_t*) link_context;
    qdr_core_timer_schedule_CT(client->core, client->timer, 1);
    qdr_terminus_free(remote_source);
    qdr_terminus_free(remote_target);
}

/**
 * Event - Credit/Drain status for an outgoing core-endpoint link has changed
 *
 * @param link_context The opaque context associated with the endpoint link
 * @param available_credit The number of deliveries that may be sent on this link
 * @param drain True iff the peer receiver is requesting that the credit be drained
 */
static void on_flow(void *link_context,
                    int   available_credit,
                    bool  drain)
{
    qcm_heartbeat_edge_t *client = (qcm_heartbeat_edge_t*) link_context;
    client->link_credit = drain ? 0 : available_credit;
}

/**
 * Event - A core-endpoint link has been detached
 *
 * Note: It is safe to discard objects referenced by the link_context in this handler.
 *       There will be no further references to this link_context returned after this call.
 *
 * @param link_context The opaque context associated with the endpoint link
 * @param error The error information that came with the detach or 0 if no error
 */
static void on_first_detach(void        *link_context,
                            qdr_error_t *error)
{
    qcm_heartbeat_edge_t *client = (qcm_heartbeat_edge_t*) link_context;
    if (!!client->timer) {
        qdr_core_timer_cancel_CT(client->core, client->timer);
    }
}


/**
 * Event - A core-endpoint link is being freed.
 *
 * This handler must free all resources associated with the link-context.
 *
 * @param link_context The opaque context associated with the endpoint link
 */
static void on_cleanup(void *link_context)
{
    qcm_heartbeat_edge_t *client = (qcm_heartbeat_edge_t*) link_context;
    if (!!client->timer) {
        qdr_core_timer_cancel_CT(client->core, client->timer);
    }
}

static qdrc_endpoint_desc_t descriptor = {
    .label = "heartbeat_edge",
    .on_second_attach = on_second_attach,
    .on_flow          = on_flow,
    .on_first_detach  = on_first_detach,
    .on_cleanup       = on_cleanup
};

//================================================================================
// Event Handlers
//================================================================================

static void on_timer(qdr_core_t *core, void *context)
{
    qcm_heartbeat_edge_t *client = (qcm_heartbeat_edge_t*) context;
    qdr_core_timer_schedule_CT(client->core, client->timer, 2);
    if (client->link_credit > 0) {
        client->link_credit--;

        qd_composed_field_t *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
        qd_compose_insert_int(body, client->next_msg_id);
        client->next_msg_id++;

        qd_message_t *msg = qd_message_compose(body, 0, 0, true);

        qdr_delivery_t *dlv = qdrc_endpoint_delivery_CT(client->core, client->endpoint, msg);
        qdrc_endpoint_send_CT(client->core, client->endpoint, dlv, true);
    }
}

static void on_conn_event(void             *context,
                          qdrc_event_t      event_type,
                          qdr_connection_t *conn)
{
    qcm_heartbeat_edge_t *client = (qcm_heartbeat_edge_t*) context;

    switch (event_type) {
    case QDRC_EVENT_CONN_EDGE_ESTABLISHED:
        client->edge_conn   = conn;
        client->link_credit = 0;

        //
        // Set up a Client API session on the edge connection.
        //
        qdr_terminus_t *target = qdr_terminus(0);
        qdr_terminus_set_address(target, QD_TERMINUS_HEARTBEAT);
        client->endpoint = qdrc_endpoint_create_link_CT(client->core, client->edge_conn, QD_OUTGOING,
                                                        0, target, &descriptor, client);
        break;

    case QDRC_EVENT_CONN_EDGE_LOST:
        client->edge_conn   = 0;
        client->link_credit = 0;

        //
        // Remove the allocated resources.
        //
        qdr_core_timer_cancel_CT(client->core, client->timer);
        client->endpoint = 0;
        break;

    default:
        assert(false);
        break;
    }
}

//================================================================================
// Module Handlers
//================================================================================

static bool qcm_heartbeat_edge_enable_CT(qdr_core_t *core)
{
    return core->router_mode == QD_ROUTER_MODE_EDGE;
}


static void qcm_heartbeat_edge_init_CT(qdr_core_t *core, void **module_context)
{
    qcm_heartbeat_edge_t *client = NEW(qcm_heartbeat_edge_t);
    ZERO(client);

    client->core      = core;
    client->event_sub = qdrc_event_subscribe_CT(client->core,
                                                QDRC_EVENT_CONN_EDGE_ESTABLISHED | QDRC_EVENT_CONN_EDGE_LOST,
                                                on_conn_event, 0, 0, 0,
                                                client);
    client->timer = qdr_core_timer_CT(core, on_timer, client);

    *module_context = client;
}


static void qcm_heartbeat_edge_final_CT(void *module_context)
{
    qcm_heartbeat_edge_t *client = (qcm_heartbeat_edge_t*) module_context;
    qdrc_event_unsubscribe_CT(client->core, client->event_sub);
    qdr_core_timer_free_CT(client->core, client->timer);
    free(client);
}


QDR_CORE_MODULE_DECLARE("heartbeat_edge", qcm_heartbeat_edge_enable_CT, qcm_heartbeat_edge_init_CT, qcm_heartbeat_edge_final_CT)
