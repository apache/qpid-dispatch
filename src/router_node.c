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

#include <qpid/dispatch/python_embedded.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <qpid/dispatch.h>
#include "dispatch_private.h"
#include "entity_cache.h"
#include "router_private.h"

const char *QD_ROUTER_NODE_TYPE = "router.node";
const char *QD_ROUTER_ADDRESS_TYPE = "router.address";
const char *QD_ROUTER_LINK_TYPE = "router.link";
const char *CORE_AGENT_ADDRESS = "$management";

static char *router_role    = "inter-router";
static char *on_demand_role = "on-demand";
static char *container_role = "route-container";
static char *direct_prefix;
static char *node_id;

/**
 * Determine the role of a connection
 */
static void qd_router_connection_get_config(const qd_connection_t  *conn,
                                            qdr_connection_role_t  *role,
                                            int                    *cost,
                                            const char            **name,
                                            bool                   *strip_annotations_in,
                                            bool                   *strip_annotations_out,
                                            int                    *link_capacity)
{
    if (conn) {
        const qd_server_config_t *cf = qd_connection_config(conn);

        *strip_annotations_in  = cf ? cf->strip_inbound_annotations  : false;
        *strip_annotations_out = cf ? cf->strip_outbound_annotations : false;
        *link_capacity         = cf ? cf->link_capacity : 1;

        if        (cf && strcmp(cf->role, router_role) == 0) {
            *strip_annotations_in  = false;
            *strip_annotations_out = false;
            *role = QDR_ROLE_INTER_ROUTER;
            *cost = cf->inter_router_cost;
        } else if (cf && (strcmp(cf->role, container_role) == 0 ||
                          strcmp(cf->role, on_demand_role) == 0))  // backward compat
            *role = QDR_ROLE_ROUTE_CONTAINER;
        else
            *role = QDR_ROLE_NORMAL;

        *name = cf ? cf->name : 0;
        if (*name) {
            if (strncmp("listener/", *name, 9) == 0 ||
                strncmp("connector/", *name, 10) == 0)
                *name = 0;
        }
    }
}


static int AMQP_writable_conn_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qdr_connection_t *qconn = (qdr_connection_t*) qd_connection_get_context(conn);

    if (qconn)
        return qdr_connection_process(qconn);
    return 0;
}


static qd_field_iterator_t *router_annotate_message(qd_router_t       *router,
                                                    qd_parsed_field_t *in_ma,
                                                    qd_message_t      *msg,
                                                    qd_bitmask_t     **link_exclusions,
                                                    bool               strip_inbound_annotations)
{
    qd_field_iterator_t *ingress_iter = 0;

    qd_parsed_field_t *trace   = 0;
    qd_parsed_field_t *ingress = 0;
    qd_parsed_field_t *to      = 0;
    qd_parsed_field_t *phase   = 0;

    *link_exclusions = 0;

    if (in_ma && !strip_inbound_annotations) {
        uint32_t count = qd_parse_sub_count(in_ma);
        bool done = false;

        for (uint32_t idx = 0; idx < count && !done; idx++) {
            qd_parsed_field_t *sub  = qd_parse_sub_key(in_ma, idx);
            if (!sub)
                continue;
            qd_field_iterator_t *iter = qd_parse_raw(sub);
            if (!iter)
                continue;

            if        (qd_field_iterator_equal(iter, (unsigned char*) QD_MA_TRACE)) {
                trace = qd_parse_sub_value(in_ma, idx);
            } else if (qd_field_iterator_equal(iter, (unsigned char*) QD_MA_INGRESS)) {
                ingress = qd_parse_sub_value(in_ma, idx);
            } else if (qd_field_iterator_equal(iter, (unsigned char*) QD_MA_TO)) {
                to = qd_parse_sub_value(in_ma, idx);
            } else if (qd_field_iterator_equal(iter, (unsigned char*) QD_MA_PHASE)) {
                phase = qd_parse_sub_value(in_ma, idx);
            }
            done = trace && ingress && to && phase;
        }
    }

    //
    // QD_MA_TRACE:
    // If there is a trace field, append this router's ID to the trace.
    // If the router ID is already in the trace the msg has looped.
    //
    qd_composed_field_t *trace_field = qd_compose_subfield(0);
    qd_compose_start_list(trace_field);
    if (trace) {
        if (qd_parse_is_list(trace)) {
            //
            // Create a link-exclusion map for the items in the trace.  This map will
            // contain a one-bit for each link that leads to a neighbor router that
            // the message has already passed through.
            //
            *link_exclusions = qd_tracemask_create(router->tracemask, trace);

            //
            // Append this router's ID to the trace.
            //
            uint32_t idx = 0;
            qd_parsed_field_t *trace_item = qd_parse_sub_value(trace, idx);
            while (trace_item) {
                qd_field_iterator_t *iter = qd_parse_raw(trace_item);
                qd_address_iterator_reset_view(iter, ITER_VIEW_ALL);
                qd_compose_insert_string_iterator(trace_field, iter);
                idx++;
                trace_item = qd_parse_sub_value(trace, idx);
            }
        }
    }

    qd_compose_insert_string(trace_field, node_id);
    qd_compose_end_list(trace_field);
    qd_message_set_trace_annotation(msg, trace_field);

    //
    // QD_MA_TO:
    // Preserve the existing value.
    //
    if (to) {
        qd_composed_field_t *to_field = qd_compose_subfield(0);
        qd_compose_insert_string_iterator(to_field, qd_parse_raw(to));
        qd_message_set_to_override_annotation(msg, to_field);
    }

    //
    // QD_MA_PHASE:
    // Preserve the existing value.
    //
    if (phase) {
        int phase_val = qd_parse_as_int(phase);
        qd_message_set_phase_annotation(msg, phase_val);
    }

    //
    // QD_MA_INGRESS:
    // If there is no ingress field, annotate the ingress as
    // this router else keep the original field.
    //
    qd_composed_field_t *ingress_field = qd_compose_subfield(0);
    if (ingress && qd_parse_is_scalar(ingress)) {
        ingress_iter = qd_parse_raw(ingress);
        qd_compose_insert_string_iterator(ingress_field, ingress_iter);
    } else
        qd_compose_insert_string(ingress_field, node_id);
    qd_message_set_ingress_annotation(msg, ingress_field);

    //
    // Return the iterator to the ingress field _if_ it was present.
    // If we added the ingress, return NULL.
    //
    return ingress_iter;
}


/**
 * Inbound Delivery Handler
 */
static void AMQP_rx_handler(void* context, qd_link_t *link, pn_delivery_t *pnd)
{
    qd_router_t    *router   = (qd_router_t*) context;
    pn_link_t      *pn_link  = qd_link_pn(link);
    qdr_link_t     *rlink    = (qdr_link_t*) qd_link_get_context(link);
    qdr_delivery_t *delivery = 0;
    qd_message_t   *msg;

    //
    // Receive the message into a local representation.  If the returned message
    // pointer is NULL, we have not yet received a complete message.
    //
    // Note:  In the link-routing case, consider cutting the message through.  There's
    //        no reason to wait for the whole message to be received before starting to
    //        send it.
    //
    msg = qd_message_receive(pnd);
    if (!msg)
        return;

    //
    // Consume the delivery.
    //
    pn_link_advance(pn_link);

    //
    // If there's no router link, free the message and finish.  It's likely that the link
    // is closing.
    //
    if (!rlink) {
        qd_message_free(msg);
        return;
    }

    //
    // Handle the link-routed case
    //
    if (qdr_link_is_routed(rlink)) {
        pn_delivery_tag_t dtag = pn_delivery_tag(pnd);
        delivery = qdr_link_deliver_to_routed_link(rlink, msg, pn_delivery_settled(pnd), (uint8_t*) dtag.start, dtag.size);
        if (delivery) {
            if (pn_delivery_settled(pnd))
                pn_delivery_settle(pnd);
            else {
                pn_delivery_set_context(pnd, delivery);
                qdr_delivery_set_context(delivery, pnd);
                qdr_delivery_incref(delivery);
            }
        }
        return;
    }

    //
    // Determine if the incoming link is anonymous.  If the link is addressed,
    // there are some optimizations we can take advantage of.
    //
    bool anonymous_link = qdr_link_is_anonymous(rlink);

    //
    // Validate the content of the delivery as an AMQP message.  This is done partially, only
    // to validate that we can find the fields we need to route the message.
    //
    // If the link is anonymous, we must validate through the message properties to find the
    // 'to' field.  If the link is not anonymous, we don't need the 'to' field as we will be
    // using the address from the link target.
    //
    qd_message_depth_t  validation_depth = anonymous_link ? QD_DEPTH_PROPERTIES : QD_DEPTH_MESSAGE_ANNOTATIONS;
    bool                valid_message    = qd_message_check(msg, validation_depth);

    if (valid_message) {
        qd_parsed_field_t   *in_ma        = qd_message_message_annotations(msg);
        qd_bitmask_t        *link_exclusions;
        bool                 strip        = qdr_link_strip_annotations_in(rlink);
        qd_field_iterator_t *ingress_iter = router_annotate_message(router, in_ma, msg, &link_exclusions, strip);

        if (anonymous_link) {
            qd_field_iterator_t *addr_iter = 0;
            int phase = 0;
            
            //
            // If the message has delivery annotations, get the to-override field from the annotations.
            //
            if (in_ma) {
                qd_parsed_field_t *ma_to = qd_parse_value_by_key(in_ma, QD_MA_TO);
                if (ma_to) {
                    addr_iter = qd_field_iterator_dup(qd_parse_raw(ma_to));
                    phase = qd_message_get_phase_annotation(msg);
                }
            }

            //
            // Still no destination address?  Use the TO field from the message properties.
            //
            if (!addr_iter)
                addr_iter = qd_message_field_iterator(msg, QD_FIELD_TO);

            if (addr_iter) {
                qd_address_iterator_reset_view(addr_iter, ITER_VIEW_ADDRESS_HASH);
                if (phase > 0)
                    qd_address_iterator_set_phase(addr_iter, '0' + (char) phase);
                delivery = qdr_link_deliver_to(rlink, msg, ingress_iter, addr_iter, pn_delivery_settled(pnd),
                                               link_exclusions);
            }
        } else {
            const char *term_addr = pn_terminus_get_address(qd_link_remote_target(link));
            if (!term_addr)
                term_addr = pn_terminus_get_address(qd_link_source(link));

            if (term_addr) {
                qd_composed_field_t *to_override = qd_compose_subfield(0);
                qd_compose_insert_string(to_override, term_addr);
                qd_message_set_to_override_annotation(msg, to_override);
                int phase = qdr_link_phase(rlink);
                if (phase != 0)
                    qd_message_set_phase_annotation(msg, phase);
            }
            delivery = qdr_link_deliver(rlink, msg, ingress_iter, pn_delivery_settled(pnd), link_exclusions);
        }

        if (delivery) {
            if (pn_delivery_settled(pnd))
                pn_delivery_settle(pnd);
            else {
                pn_delivery_set_context(pnd, delivery);
                qdr_delivery_set_context(delivery, pnd);
                qdr_delivery_incref(delivery);
            }
        } else {
            //
            // The message is now and will always be unroutable because there is no address.
            //
            pn_delivery_update(pnd, PN_REJECTED);
            pn_delivery_settle(pnd);
        }

        //
        // Rules for delivering messages:
        //
        // For addressed (non-anonymous) links:
        //   to-override must be set (done in the core?)
        //   uses qdr_link_deliver to hand over to the core
        //
        // For anonymous links:
        //   If there's a to-override in the annotations, use that address
        //   Or, use the 'to' field in the message properties
        //



    } else {
        //
        // Message is invalid.  Reject the message and don't involve the router core.
        //
        pn_delivery_update(pnd, PN_REJECTED);
        pn_delivery_settle(pnd);
    }
}


/**
 * Delivery Disposition Handler
 */
static void AMQP_disposition_handler(void* context, qd_link_t *link, pn_delivery_t *pnd)
{
    qd_router_t    *router   = (qd_router_t*) context;
    qdr_delivery_t *delivery = (qdr_delivery_t*) pn_delivery_get_context(pnd);
    bool            give_reference = false;

    //
    // It's important to not do any processing without a qdr_delivery.  When pre-settled
    // multi-frame deliveries arrive, it's possible for the settlement to register before
    // the whole message arrives.  Such premature settlement indications must be ignored.
    //
    if (!delivery)
        return;

    //
    // If the delivery is settled, remove the linkage between the PN and QDR deliveries.
    //
    if (pn_delivery_settled(pnd)) {
        pn_delivery_set_context(pnd, 0);
        qdr_delivery_set_context(delivery, 0);

        //
        // Don't decref the delivery here.  Rather, we will _give_ the reference to the core.
        //
        give_reference = true;
    }

    //
    // Update the disposition of the delivery
    //
    qdr_delivery_update_disposition(router->router_core, delivery,
                                    pn_delivery_remote_state(pnd), pn_delivery_settled(pnd),
                                    give_reference);

    //
    // If settled, close out the delivery
    //
    if (pn_delivery_settled(pnd))
        pn_delivery_settle(pnd);
}


/**
 * New Incoming Link Handler
 */
static int AMQP_incoming_link_handler(void* context, qd_link_t *link)
{
    qd_connection_t  *conn     = qd_link_connection(link);
    qdr_connection_t *qdr_conn = (qdr_connection_t*) qd_connection_get_context(conn);
    qdr_link_t       *qdr_link = qdr_link_first_attach(qdr_conn, QD_INCOMING,
                                                       qdr_terminus(qd_link_remote_source(link)),
                                                       qdr_terminus(qd_link_remote_target(link)),
                                                       pn_link_name(qd_link_pn(link)));
    qdr_link_set_context(qdr_link, link);
    qd_link_set_context(link, qdr_link);

    return 0;
}


/**
 * New Outgoing Link Handler
 */
static int AMQP_outgoing_link_handler(void* context, qd_link_t *link)
{
    qd_connection_t  *conn     = qd_link_connection(link);
    qdr_connection_t *qdr_conn = (qdr_connection_t*) qd_connection_get_context(conn);
    qdr_link_t       *qdr_link = qdr_link_first_attach(qdr_conn, QD_OUTGOING,
                                                       qdr_terminus(qd_link_remote_source(link)),
                                                       qdr_terminus(qd_link_remote_target(link)),
                                                       pn_link_name(qd_link_pn(link)));
    qdr_link_set_context(qdr_link, link);
    qd_link_set_context(link, qdr_link);

    return 0;
}


/**
 * Handler for remote opening of links that we initiated.
 */
static int AMQP_link_attach_handler(void* context, qd_link_t *link)
{
    qdr_link_t *qlink = (qdr_link_t*) qd_link_get_context(link);
    qdr_link_second_attach(qlink,
                           qdr_terminus(qd_link_remote_source(link)),
                           qdr_terminus(qd_link_remote_target(link)));

    return 0;
}


/**
 * Handler for flow events on links
 */
static int AMQP_link_flow_handler(void* context, qd_link_t *link)
{
    qd_router_t *router = (qd_router_t*) context;
    qdr_link_t  *rlink  = (qdr_link_t*) qd_link_get_context(link);
    pn_link_t   *pnlink = qd_link_pn(link);

    if (!rlink)
        return 0;

    qdr_link_flow(router->router_core, rlink, pn_link_remote_credit(pnlink), pn_link_get_drain(pnlink));

    return 0;
}


/**
 * Link Detached Handler
 */
static int AMQP_link_detach_handler(void* context, qd_link_t *link, qd_detach_type_t dt)
{
    qdr_link_t     *rlink  = (qdr_link_t*) qd_link_get_context(link);
    pn_condition_t *cond   = qd_link_pn(link) ? pn_link_remote_condition(qd_link_pn(link)) : 0;

    if (rlink) {
        qdr_error_t *error = qdr_error_from_pn(cond);
        qdr_link_detach(rlink, dt, error);

        //
        // This is the last event for this link that we will send into the core.  Remove the
        // core linkage.  Note that the core->qd linkage is still in place.
        //
        qd_link_set_context(link, 0);

        //
        // If the link was lost (due to connection drop), or the linkage from the core
        // object is already gone, finish disconnecting the linkage and free the qd_link
        // because the core will silently free its own resources.
        //
        if (dt == QD_LOST || qdr_link_get_context(rlink) == 0) {
            qdr_link_set_context(rlink, 0);
            qd_link_free(link);
        }
    }

    return 0;
}


static void AMQP_opened_handler(qd_router_t *router, qd_connection_t *conn, bool inbound)
{
    qdr_connection_role_t  role = 0;
    int                    cost = 1;
    int                    remote_cost = 1;
    bool                   strip_annotations_in = false;
    bool                   strip_annotations_out = false;
    int                    link_capacity = 1;
    const char            *name = 0;
    uint64_t               connection_id = qd_connection_connection_id(conn);
    pn_connection_t       *pn_conn = qd_connection_pn(conn);

    qd_router_connection_get_config(conn, &role, &cost, &name,
                                    &strip_annotations_in, &strip_annotations_out, &link_capacity);

    if (role == QDR_ROLE_INTER_ROUTER) {
        //
        // Check the remote properties for an inter-router cost value.
        //
        pn_data_t *props = pn_conn ? pn_connection_remote_properties(pn_conn) : 0;
        if (props) {
            pn_data_rewind(props);
            pn_data_next(props);
            if (props && pn_data_type(props) == PN_MAP) {
                pn_data_enter(props);
                while (pn_data_next(props)) {
                    if (pn_data_type(props) == PN_SYMBOL) {
                        pn_bytes_t sym = pn_data_get_symbol(props);
                        if (sym.size == strlen(QD_CONNECTION_PROPERTY_COST_KEY) &&
                            strcmp(sym.start, QD_CONNECTION_PROPERTY_COST_KEY) == 0) {
                            pn_data_next(props);
                            if (pn_data_type(props) == PN_INT)
                                remote_cost = pn_data_get_int(props);
                            break;
                        }
                    }
                }
            }
        }

        //
        // Use the larger of the local and remote costs for this connection
        //
        if (remote_cost > cost)
            cost = remote_cost;
    }

    qdr_connection_t *qdrc = qdr_connection_opened(router->router_core, inbound, role, cost, connection_id, name,
                                                   pn_connection_remote_container(pn_conn),
                                                   strip_annotations_in, strip_annotations_out, link_capacity);

    qd_connection_set_context(conn, qdrc);
    qdr_connection_set_context(qdrc, conn);
}


static int AMQP_inbound_opened_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qd_router_t *router = (qd_router_t*) type_context;
    AMQP_opened_handler(router, conn, true);
    return 0;
}


static int AMQP_outbound_opened_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qd_router_t *router = (qd_router_t*) type_context;
    AMQP_opened_handler(router, conn, false);
    return 0;
}


static int AMQP_closed_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qdr_connection_t *qdrc = (qdr_connection_t*) qd_connection_get_context(conn);

    if (qdrc) {
        qdr_connection_closed(qdrc);
        qd_connection_set_context(conn, 0);
    }

    return 0;
}


static void qd_router_timer_handler(void *context)
{
    qd_router_t *router = (qd_router_t*) context;

    //
    // Periodic processing.
    //
    qd_pyrouter_tick(router);
    qd_timer_schedule(router->timer, 1000);
}


static qd_node_type_t router_node = {"router", 0, 0,
                                     AMQP_rx_handler,
                                     AMQP_disposition_handler,
                                     AMQP_incoming_link_handler,
                                     AMQP_outgoing_link_handler,
                                     AMQP_writable_conn_handler,
                                     AMQP_link_detach_handler,
                                     AMQP_link_attach_handler,
                                     AMQP_link_flow_handler,
                                     0,   // node_created_handler
                                     0,   // node_destroyed_handler
                                     AMQP_inbound_opened_handler,
                                     AMQP_outbound_opened_handler,
                                     AMQP_closed_handler};
static int type_registered = 0;

qd_router_t *qd_router(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id)
{
    if (!type_registered) {
        type_registered = 1;
        qd_container_register_node_type(qd, &router_node);
    }

    size_t dplen = 9 + strlen(area) + strlen(id);
    node_id = (char*) malloc(dplen);
    strcpy(node_id, area);
    strcat(node_id, "/");
    strcat(node_id, id);

    qd_router_t *router = NEW(qd_router_t);
    ZERO(router);

    router_node.type_context = router;

    qd->router = router;
    router->qd           = qd;
    router->router_core  = 0;
    router->log_source   = qd_log_source("ROUTER");
    router->router_mode  = mode;
    router->router_area  = area;
    router->router_id    = id;
    router->node         = qd_container_set_default_node_type(qd, &router_node, (void*) router, QD_DIST_BOTH);

    router->lock  = sys_mutex();
    router->timer = qd_timer(qd, qd_router_timer_handler, (void*) router);

    //
    // Inform the field iterator module of this router's id and area.  The field iterator
    // uses this to offload some of the address-processing load from the router.
    //
    qd_field_iterator_set_address(area, id);

    //
    // Seed the random number generator
    //
    unsigned int seed = (unsigned int) time(0);
    srandom(seed);

    switch (router->router_mode) {
    case QD_ROUTER_MODE_STANDALONE: qd_log(router->log_source, QD_LOG_INFO, "Router started in Standalone mode");  break;
    case QD_ROUTER_MODE_INTERIOR:   qd_log(router->log_source, QD_LOG_INFO, "Router started in Interior mode, area=%s id=%s", area, id);  break;
    case QD_ROUTER_MODE_EDGE:       qd_log(router->log_source, QD_LOG_INFO, "Router started in Edge mode");  break;
    case QD_ROUTER_MODE_ENDPOINT:   qd_log(router->log_source, QD_LOG_INFO, "Router started in Endpoint mode");  break;
    }

    return router;
}


static void CORE_connection_activate(void *context, qdr_connection_t *conn)
{
    //
    // IMPORTANT:  This is the only core callback that is invoked on the core
    //             thread itself.  It is imperative that this function do nothing
    //             apart from setting the activation in the server for the connection.
    //
    qd_server_activate((qd_connection_t*) qdr_connection_get_context(conn));
}


static void CORE_link_first_attach(void             *context,
                                   qdr_connection_t *conn,
                                   qdr_link_t       *link, 
                                   qdr_terminus_t   *source,
                                   qdr_terminus_t   *target)
{
    qd_router_t     *router = (qd_router_t*) context;
    qd_connection_t *qconn  = (qd_connection_t*) qdr_connection_get_context(conn);

    //
    // Create a new link to be attached
    //
    qd_link_t *qlink = qd_link(router->node, qconn, qdr_link_direction(link), qdr_link_name(link));

    //
    // Copy the source and target termini to the link
    //
    qdr_terminus_copy(source, qd_link_source(qlink));
    qdr_terminus_copy(target, qd_link_target(qlink));

    //
    // Associate the qd_link and the qdr_link to each other
    //
    qdr_link_set_context(link, qlink);
    qd_link_set_context(qlink, link);

    //
    // Open (attach) the link
    //
    pn_link_open(qd_link_pn(qlink));
}


static void CORE_link_second_attach(void *context, qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target)
{
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    qdr_terminus_copy(source, qd_link_source(qlink));
    qdr_terminus_copy(target, qd_link_target(qlink));

    //
    // Open (attach) the link
    //
    pn_link_open(qd_link_pn(qlink));
}


static void CORE_link_detach(void *context, qdr_link_t *link, qdr_error_t *error, bool first)
{
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    pn_link_t *pn_link = qd_link_pn(qlink);
    if (!pn_link)
        return;

    if (error) {
        pn_condition_t *cond = pn_link_condition(pn_link);
        qdr_error_copy(error, cond);
    }
    qd_link_close(qlink);

    //
    // If this is the second detach, free the qd_link
    //
    if (!first)
        qd_link_free(qlink);
}


static void CORE_link_flow(void *context, qdr_link_t *link, int credit)
{
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;
    
    pn_link_t *plink = qd_link_pn(qlink);

    if (plink)
        pn_link_flow(plink, credit);
}


static void CORE_link_offer(void *context, qdr_link_t *link, int delivery_count)
{
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    pn_link_t *plink = qd_link_pn(qlink);

    if (plink)
        pn_link_offered(plink, delivery_count);
}


static void CORE_link_drained(void *context, qdr_link_t *link)
{
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    pn_link_t *plink = qd_link_pn(qlink);

    if (plink)
        pn_link_drained(plink);
}


static void CORE_link_drain(void *context, qdr_link_t *link, bool mode)
{
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    pn_link_t *plink = qd_link_pn(qlink);

    if (plink) {
        if (pn_link_is_receiver(plink))
            pn_link_set_drain(plink, mode);
    }
}


static void CORE_link_push(void *context, qdr_link_t *link)
{
    qd_router_t *router = (qd_router_t*) context;
    qd_link_t   *qlink  = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;
    
    pn_link_t *plink = qd_link_pn(qlink);

    if (plink) {
        int link_credit = pn_link_credit(plink);
        qdr_link_process_deliveries(router->router_core, link, link_credit);
    }
}


static void CORE_link_deliver(void *context, qdr_link_t *link, qdr_delivery_t *dlv, bool settled)
{
    qd_router_t *router = (qd_router_t*) context;
    qd_link_t   *qlink  = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    pn_link_t *plink = qd_link_pn(qlink);
    if (!plink)
        return;

    const char *tag;
    int         tag_length;

    qdr_delivery_tag(dlv, &tag, &tag_length);

    pn_delivery(plink, pn_dtag(tag, tag_length));
    pn_delivery_t *pdlv = pn_link_current(plink);

    //
    // If the remote send settle mode is set to 'settled', we should settle the delivery on behalf of the receiver.
    //
    bool remote_snd_settled = qd_link_remote_snd_settle_mode(qlink) == PN_SND_SETTLED;

    if (!settled && !remote_snd_settled) {
        pn_delivery_set_context(pdlv, dlv);
        qdr_delivery_set_context(dlv, pdlv);
        qdr_delivery_incref(dlv);
    }

    qd_message_send(qdr_delivery_message(dlv), qlink, qdr_link_strip_annotations_out(link));

    if (!settled && remote_snd_settled)
        // Tell the core that the delivery has been accepted and settled, since we are settling on behalf of the receiver
        qdr_delivery_update_disposition(router->router_core, dlv, PN_ACCEPTED, true, false);

    if (settled || remote_snd_settled)
        pn_delivery_settle(pdlv);

    pn_link_advance(plink);
}


static void CORE_delivery_update(void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled)
{
    pn_delivery_t *pnd = (pn_delivery_t*) qdr_delivery_get_context(dlv);

    if (!pnd)
        return;

    //
    // If the disposition has changed, update the proton delivery.
    //
    if (disp != pn_delivery_remote_state(pnd)) {
        if (disp == PN_MODIFIED)
            pn_disposition_set_failed(pn_delivery_local(pnd), true);
        pn_delivery_update(pnd, disp);
    }

    //
    // If the delivery is settled, remove the linkage and settle the proton delivery.
    //
    if (settled) {
        qdr_delivery_set_context(dlv, 0);
        pn_delivery_set_context(pnd, 0);
        pn_delivery_settle(pnd);
        qdr_delivery_decref(dlv);
    }
}


void qd_router_setup_late(qd_dispatch_t *qd)
{
    qd->router->tracemask   = qd_tracemask();
    qd->router->router_core = qdr_core(qd, qd->router->router_mode, qd->router->router_area, qd->router->router_id);

    qdr_connection_handlers(qd->router->router_core, (void*) qd->router,
                            CORE_connection_activate,
                            CORE_link_first_attach,
                            CORE_link_second_attach,
                            CORE_link_detach,
                            CORE_link_flow,
                            CORE_link_offer,
                            CORE_link_drained,
                            CORE_link_drain,
                            CORE_link_push,
                            CORE_link_deliver,
                            CORE_delivery_update);

    qd_router_python_setup(qd->router);
    qd_timer_schedule(qd->router->timer, 1000);
}

void qd_router_free(qd_router_t *router)
{
    if (!router) return;

    qd_container_set_default_node_type(router->qd, 0, 0, QD_DIST_BOTH);

    qdr_core_free(router->router_core);
    qd_tracemask_free(router->tracemask);
    qd_timer_free(router->timer);
    sys_mutex_free(router->lock);
    qd_router_configure_free(router);
    qd_router_python_free(router);

    free(router);
    free(node_id);
    free(direct_prefix);
}


const char *qd_router_id(const qd_dispatch_t *qd)
{
    return node_id;
}


qdr_core_t *qd_router_core(qd_dispatch_t *qd)
{
    return qd->router->router_core;
}

