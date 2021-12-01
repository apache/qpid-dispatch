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

#include "core_link_endpoint.h"
#include "module.h"

#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/ctools.h"

#include <stdio.h>

typedef struct qdr_addr_tracking_module_context_t     qdr_addr_tracking_module_context_t;
typedef struct qdr_addr_endpoint_state_t              qdr_addr_endpoint_state_t;

struct qdr_addr_endpoint_state_t {
    DEQ_LINKS(qdr_addr_endpoint_state_t);
    qdrc_endpoint_t                    *endpoint;
    qdr_connection_t                   *conn;    // The connection associated with the endpoint.
    qdr_addr_tracking_module_context_t *mc;
    int                                ref_count;
    bool                               closed; // Is the endpoint that this state belong to closed?
};

DEQ_DECLARE(qdr_addr_endpoint_state_t, qdr_addr_endpoint_state_list_t);
ALLOC_DECLARE(qdr_addr_endpoint_state_t);
ALLOC_DEFINE(qdr_addr_endpoint_state_t);

struct  qdr_addr_tracking_module_context_t {
    qdr_core_t                     *core;
    qdr_addr_endpoint_state_list_t  endpoint_state_list;
    qdrc_event_subscription_t      *event_sub;
    qdrc_endpoint_desc_t           addr_tracking_endpoint;
};


static qd_message_t *qdcm_edge_create_address_dlv(qdr_core_t *core, qdr_address_t   *addr, bool insert_addr)
{
    //
    // Start header
    //
    qd_composed_field_t *fld   = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(fld);
    qd_compose_insert_bool(fld, 0);     // durable
    qd_compose_end_list(fld);

    qd_composed_field_t *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    qd_compose_start_list(body);

    const char *addr_str = (const char *)qd_hash_key_by_handle(addr->hash_handle);

    qd_compose_insert_string(body, addr_str);
    qd_compose_insert_bool(body, insert_addr);
    qd_compose_end_list(body);

    // Finally, compose and return the message so it can be sent out.

    return qd_message_compose(fld, body, 0, true);
}

static qdr_addr_endpoint_state_t *qdrc_get_endpoint_state_for_connection(qdr_addr_endpoint_state_list_t  endpoint_state_list, qdr_connection_t *conn)
{
    qdr_addr_endpoint_state_t *endpoint_state = DEQ_HEAD(endpoint_state_list);
    while(endpoint_state) {
        if (endpoint_state->conn == conn) {
            return endpoint_state;
        }
        endpoint_state = DEQ_NEXT(endpoint_state);
    }
    return 0;
}


static void qdrc_address_endpoint_first_attach(void              *bind_context,
                                               qdrc_endpoint_t   *endpoint,
                                               void             **link_context,
                                               qdr_terminus_t   *remote_source,
                                               qdr_terminus_t   *remote_target)
{
    qdr_addr_tracking_module_context_t *bc = (qdr_addr_tracking_module_context_t *) bind_context;

    qdr_addr_endpoint_state_t *endpoint_state = new_qdr_addr_endpoint_state_t();

    ZERO(endpoint_state);
    endpoint_state->endpoint  = endpoint;
    endpoint_state->mc        = bc;
    endpoint_state->conn      = qdrc_endpoint_get_connection_CT(endpoint);


    DEQ_INSERT_TAIL(bc->endpoint_state_list, endpoint_state);

    //
    // The link to hard coded address QD_TERMINUS_EDGE_ADDRESS_TRACKING should be created only if this is a receiver link
    // and if this link is created inside the QDR_ROLE_EDGE_CONNECTION connection.
    //
    if (qdrc_endpoint_get_direction_CT(endpoint) == QD_OUTGOING && qdrc_endpoint_get_connection_CT(endpoint)->role == QDR_ROLE_EDGE_CONNECTION) {
        *link_context = endpoint_state;
        qdrc_endpoint_second_attach_CT(bc->core, endpoint, remote_source, remote_target);
   }
    else {
        //
        // We simply detach any links that dont match the above condition.
        //
        *link_context = 0;
        qdrc_endpoint_detach_CT(bc->core, endpoint, 0);
        qdr_terminus_free(remote_source);
        qdr_terminus_free(remote_target);
    }
}


static void qdrc_address_endpoint_on_first_detach(void *link_context,
                                              qdr_error_t *error)
{
    qdr_addr_endpoint_state_t *endpoint_state  = (qdr_addr_endpoint_state_t *)link_context;
    qdrc_endpoint_detach_CT(endpoint_state->mc->core, endpoint_state->endpoint, 0);
    qdr_error_free(error);
}

static void qdrc_address_endpoint_cleanup(void *link_context)
{
    qdr_addr_endpoint_state_t *endpoint_state  = (qdr_addr_endpoint_state_t *)link_context;
    if (endpoint_state) {
        qdr_addr_tracking_module_context_t *mc = endpoint_state->mc;
        assert (endpoint_state->conn);
        endpoint_state->closed = true;
        if (endpoint_state->ref_count == 0) {

            //
            // The endpoint has been closed and no other links are referencing this endpoint. Time to free it.
            // Clean out all the states held by the link_context (endpoint_state)
            //
            if (mc) {
                DEQ_REMOVE(mc->endpoint_state_list, endpoint_state);
            }

            endpoint_state->conn = 0;
            endpoint_state->endpoint = 0;
            free_qdr_addr_endpoint_state_t(endpoint_state);
        }
    }
}


static bool qdrc_can_send_address(qdr_address_t *addr, qdr_connection_t *conn)
{
    if (!addr)
        return false;

    bool can_send = false;
    if (DEQ_SIZE(addr->rlinks) > 1 || qd_bitmask_cardinality(addr->rnodes) > 0) {
        // There is at least one receiver for this address somewhere in the router network
        can_send = true;
    }
    if (!can_send) {
        if (DEQ_SIZE(addr->rlinks) == 1) {
            qdr_link_ref_t *link_ref = DEQ_HEAD(addr->rlinks);
            if (link_ref->link->conn != conn)
                can_send=true;
        }
    }
    return can_send;
}


static void qdrc_send_message(qdr_core_t *core, qdr_address_t *addr, qdrc_endpoint_t *endpoint, bool insert_addr)
{
    if (!addr)
        return;

    if (!endpoint)
        return;

    qd_message_t *msg = qdcm_edge_create_address_dlv(core, addr, insert_addr);
    qdr_delivery_t *dlv = qdrc_endpoint_delivery_CT(core, endpoint, msg);

    qdrc_endpoint_send_CT(core, endpoint, dlv, true);
}

static void on_addr_event(void *context, qdrc_event_t event, qdr_address_t *addr)
{
    // We only care about mobile addresses.
    if(!qdr_address_is_mobile_CT(addr))
        return;

    qdr_addr_tracking_module_context_t *addr_tracking = (qdr_addr_tracking_module_context_t*) context;
    switch (event) {
        case QDRC_EVENT_ADDR_BECAME_LOCAL_DEST : {
            //
            // This address transitioned from zero to one local destination. If this address already has more than zero remote destinations, don't do anything
            //
            if (qd_bitmask_cardinality(addr->rnodes) == 0) {
                qdr_link_ref_t *inlink = DEQ_HEAD(addr->inlinks);
                //
                // Every inlink that has an edge context must be informed of the appearence of this address.
                //
                while (inlink) {
                    if(inlink->link->edge_context != 0) {
                        qdr_addr_endpoint_state_t *endpoint_state = (qdr_addr_endpoint_state_t *)inlink->link->edge_context;
                        if (!endpoint_state->closed && qdrc_can_send_address(addr, endpoint_state->conn) ) {
                            qdrc_endpoint_t *endpoint = endpoint_state->endpoint;
                            qdrc_send_message(addr_tracking->core, addr, endpoint, true);
                        }
                    }
                    inlink = DEQ_NEXT(inlink);
                }
            }
            break;
        }
        case QDRC_EVENT_ADDR_BECAME_DEST : {
            //
            // This address transitioned from zero to one destination. If this address already had local destinations
            //
            qdr_link_ref_t *inlink = DEQ_HEAD(addr->inlinks);
            //
            // Every inlink that has an edge context must be informed of the appearence of this address.
            //
            while (inlink) {
                if(inlink->link->edge_context != 0) {
                    qdr_addr_endpoint_state_t *endpoint_state = (qdr_addr_endpoint_state_t *)inlink->link->edge_context;
                    if (!endpoint_state->closed && qdrc_can_send_address(addr, endpoint_state->conn) ) {
                        qdrc_endpoint_t *endpoint = endpoint_state->endpoint;
                        if (endpoint)
                            qdrc_send_message(addr_tracking->core, addr, endpoint, true);
                    }
                }
                inlink = DEQ_NEXT(inlink);
            }
        }
        break;

        case QDRC_EVENT_ADDR_NO_LONGER_DEST :

            // fallthrough

        case QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST : {
            // The address no longer has any local destinations.
            // If there are no remote destinations either, we have to tell the edge routers to delete their sender links
            if (qd_bitmask_cardinality(addr->rnodes) == 0) {
                qdr_link_ref_t *inlink = DEQ_HEAD(addr->inlinks);
                //
                // Every inlink that has an edge context must be informed of the disappearence of this address.
                //
                while (inlink) {
                    if(inlink->link->edge_context != 0) {
                        qdr_addr_endpoint_state_t *endpoint_state = (qdr_addr_endpoint_state_t *)inlink->link->edge_context;
                        if(!endpoint_state->closed) {
                            qdrc_endpoint_t *endpoint = endpoint_state->endpoint;
                            if (endpoint)
                                qdrc_send_message(addr_tracking->core, addr, endpoint, false);
                        }
                    }
                    inlink = DEQ_NEXT(inlink);
                }
            }

            break;
        }
        case QDRC_EVENT_ADDR_ONE_LOCAL_DEST: {
            //
            // This address transitioned from N destinations to one local dest
            // If this address already has non-zero remote destinations, there is no need to tell the edge routers about it
            //
            assert(DEQ_SIZE(addr->rlinks) == 1);
            //
            // There should be only one rlink in the rlinks list
            //
            qdr_link_ref_t *rlink_ref = DEQ_HEAD(addr->rlinks);
            qdr_link_t *link = rlink_ref->link;

            qdr_link_ref_t *inlink = DEQ_HEAD(addr->inlinks);
            while (inlink) {
                if (inlink->link->edge_context != 0) {
                    qdr_addr_endpoint_state_t *endpoint_state = (qdr_addr_endpoint_state_t *)inlink->link->edge_context;
                    qdrc_endpoint_t *endpoint = endpoint_state->endpoint;
                    if (endpoint_state->conn == link->conn && !endpoint_state->closed) {
                        qdrc_send_message(addr_tracking->core, addr, endpoint, false);
                        break;
                    }
                }
                inlink = DEQ_NEXT(inlink);
            }
        }
        break;
        case QDRC_EVENT_ADDR_TWO_DEST: {
            //
            // The address transitioned from one local dest to two destinations, The second destination might be local or remote.
            //
            qdr_link_ref_t *rlink_ref = DEQ_HEAD(addr->rlinks);
            qdr_link_t *link = rlink_ref->link;

            qdr_link_ref_t *inlink = DEQ_HEAD(addr->inlinks);
            while (inlink) {
                if(inlink->link->edge_context != 0) {
                    qdr_addr_endpoint_state_t *endpoint_state = (qdr_addr_endpoint_state_t *)inlink->link->edge_context;
                    qdrc_endpoint_t *endpoint = endpoint_state->endpoint;
                    if (link->conn == endpoint_state->conn && !endpoint_state->closed) {
                        qdrc_send_message(addr_tracking->core, addr, endpoint, true);
                        break;
                    }
                }
                inlink = DEQ_NEXT(inlink);
            }
            break;
        }

        default:
            break;
    }
}

static void on_link_event(void *context, qdrc_event_t event, qdr_link_t *link)
{
    switch (event) {
        case QDRC_EVENT_LINK_EDGE_DATA_ATTACHED :
        {
            qdr_addr_tracking_module_context_t *mc = (qdr_addr_tracking_module_context_t *) context;
            qdr_address_t *addr = link->owning_addr;
            if (addr && qdr_address_is_mobile_CT(addr) && DEQ_SIZE(addr->subscriptions) == 0 && link->link_direction == QD_INCOMING) {
                qdr_addr_endpoint_state_t *endpoint_state = qdrc_get_endpoint_state_for_connection(mc->endpoint_state_list, link->conn);
                // Fix for DISPATCH-1492. Remove the assert(endpoint_state); and add an if condition check for endpoint_state
                // We will not prevent regular endpoints from connecting to the edge listener for now.
                if (endpoint_state) {
                    assert(link->edge_context == 0);
                    link->edge_context = endpoint_state;
                    endpoint_state->ref_count++;
                    if (qdrc_can_send_address(addr, link->conn)) {
                        qdrc_send_message(mc->core, addr, endpoint_state->endpoint, true);
                    }
                }
            }
            break;
        }
        case QDRC_EVENT_LINK_EDGE_DATA_DETACHED :
        {
            if (link->edge_context) {
                qdr_addr_endpoint_state_t *endpoint_state = (qdr_addr_endpoint_state_t *)link->edge_context;
                endpoint_state->ref_count--;
                link->edge_context = 0;
                //
                // The endpoint has been closed and no other links are referencing this endpoint. Time to free it.
                //
                if (endpoint_state->ref_count == 0 && endpoint_state->closed) {
                    qdr_addr_tracking_module_context_t *mc = endpoint_state->mc;
                    if (mc) {
                        DEQ_REMOVE(mc->endpoint_state_list, endpoint_state);
                    }
                    endpoint_state->conn = 0;
                    endpoint_state->endpoint = 0;
                    free_qdr_addr_endpoint_state_t(endpoint_state);
                }
            }
            break;
        }

        default:
            break;
    }
}


static bool qdrc_edge_address_tracking_enable_CT(qdr_core_t *core)
{
    return core->router_mode == QD_ROUTER_MODE_INTERIOR;
}


static void qdrc_edge_address_tracking_init_CT(qdr_core_t *core, void **module_context)
{
    qdr_addr_tracking_module_context_t *context = NEW(qdr_addr_tracking_module_context_t);
    ZERO(context);
    context->core = core;
    *module_context = context;

    //
    // Bind to the static address QD_TERMINUS_EDGE_ADDRESS_TRACKING
    //
    context->addr_tracking_endpoint.label = "qdrc_edge_address_tracking_module_init_CT";
    context->addr_tracking_endpoint.on_first_attach  = qdrc_address_endpoint_first_attach;
    context->addr_tracking_endpoint.on_first_detach  = qdrc_address_endpoint_on_first_detach;
    context->addr_tracking_endpoint.on_cleanup  = qdrc_address_endpoint_cleanup;
    qdrc_endpoint_bind_mobile_address_CT(core, QD_TERMINUS_EDGE_ADDRESS_TRACKING, '0', &context->addr_tracking_endpoint, context);

    //
    // Subscribe to address and link events.
    //
    context->event_sub = qdrc_event_subscribe_CT(core,
            QDRC_EVENT_ADDR_BECAME_LOCAL_DEST | QDRC_EVENT_ADDR_ONE_LOCAL_DEST |
            QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST | QDRC_EVENT_ADDR_BECAME_DEST | QDRC_EVENT_ADDR_TWO_DEST | QDRC_EVENT_ADDR_NO_LONGER_DEST |
            QDRC_EVENT_LINK_EDGE_DATA_ATTACHED | QDRC_EVENT_LINK_EDGE_DATA_DETACHED,
            0,
            on_link_event,
            on_addr_event,
            0,
            context);
}


static void qdrc_edge_address_tracking_final_CT(void *module_context)
{
    qdr_addr_tracking_module_context_t *mc = ( qdr_addr_tracking_module_context_t *)module_context;

    // If there are any endpoint states still hanging around, clean it up.
    qdr_addr_endpoint_state_t *endpoint_state = DEQ_HEAD(mc->endpoint_state_list);
    while (endpoint_state) {
        DEQ_REMOVE_HEAD(mc->endpoint_state_list);
        free_qdr_addr_endpoint_state_t(endpoint_state);
        endpoint_state = DEQ_HEAD(mc->endpoint_state_list);
    }
    qdrc_event_unsubscribe_CT(mc->core, mc->event_sub);
    free(mc);
}


QDR_CORE_MODULE_DECLARE("edge_addr_tracking", qdrc_edge_address_tracking_enable_CT, qdrc_edge_address_tracking_init_CT, qdrc_edge_address_tracking_final_CT)
