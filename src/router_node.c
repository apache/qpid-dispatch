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

#include "delivery.h"
#include "dispatch_private.h"
#include "policy.h"
#include "router_private.h"

#include "qpid/dispatch.h"
#include "qpid/dispatch/protocol_adaptor.h"
#include "qpid/dispatch/proton_utils.h"

#include <proton/sasl.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *QD_ROUTER_NODE_TYPE = "router.node";
const char *QD_ROUTER_ADDRESS_TYPE = "router.address";
const char *QD_ROUTER_LINK_TYPE = "router.link";

static qdr_protocol_adaptor_t *amqp_direct_adaptor = 0;

static char *router_role    = "inter-router";
static char *container_role = "route-container";
static char *edge_role      = "edge";
static char *direct_prefix;
static char *node_id;
static uint8_t *encoded_node_id;
static size_t encoded_node_id_len;

static void deferred_AMQP_rx_handler(void *context, bool discard);
static bool parse_failover_property_list(qd_router_t *router, qd_connection_t *conn, pn_data_t *props);

const char *QD_AMQP_COND_OVERSIZE_DESCRIPTION = "Message size exceeded";

//==============================================================================
// Functions to handle the linkage between proton deliveries and qdr deliveries
//==============================================================================

//
// qd_link.list_of_references(pn_delivery_t)
// pn_delivery.context => reference-entry
// qdr_delivery.context => pn_delivery
//

// reject a delivery, setting the apropriate condition fields so the sender can
// determine the reason the message was rejected
//
static inline void _reject_delivery(pn_delivery_t *pnd, const char *error_name, const char *description)
{
    assert(error_name && description);
    pn_condition_t *lcond = pn_disposition_condition(pn_delivery_local(pnd));
    (void) pn_condition_set_name(lcond, error_name);
    (void) pn_condition_set_description(lcond, description);
    pn_delivery_update(pnd, PN_REJECTED);
}


static inline const char *_get_tenant_space(qd_connection_t *conn, int *length)
{
    qdr_connection_t *qdr_conn = (qdr_connection_t*) qd_connection_get_context(conn);
    return qdr_connection_get_tenant_space(qdr_conn, length);
}


static void qdr_node_connect_deliveries(qd_link_t *link, qdr_delivery_t *qdlv, pn_delivery_t *pdlv)
{
    qd_link_ref_t      *ref  = new_qd_link_ref_t();
    qd_link_ref_list_t *list = qd_link_get_ref_list(link);
    ZERO(ref);
    ref->ref = qdlv;
    DEQ_INSERT_TAIL(*list, ref);

    pn_delivery_set_context(pdlv, ref);
    qdr_delivery_set_context(qdlv, pdlv);
    qdr_delivery_incref(qdlv, "referenced by a pn_delivery");
    
}


static void qdr_node_disconnect_deliveries(qdr_core_t *core, qd_link_t *link, qdr_delivery_t *qdlv, pn_delivery_t *pdlv)
{
    if (!link)
        return;

    qd_link_ref_t      *ref  = (qd_link_ref_t*) pn_delivery_get_context(pdlv);
    qd_link_ref_list_t *list = qd_link_get_ref_list(link);

    if (ref) {
        DEQ_REMOVE(*list, ref);
        free_qd_link_ref_t(ref);

        pn_delivery_set_context(pdlv, 0);
        qdr_delivery_set_context(qdlv, 0);
        qdr_delivery_decref(core, qdlv, "removed reference from pn_delivery");
    }
}


static pn_delivery_t *qdr_node_delivery_pn_from_qdr(qdr_delivery_t *dlv)
{
    return dlv ? (pn_delivery_t*) qdr_delivery_get_context(dlv) : 0;
}


static qdr_delivery_t *qdr_node_delivery_qdr_from_pn(pn_delivery_t *dlv)
{
    qd_link_ref_t *ref = (qd_link_ref_t*) pn_delivery_get_context(dlv);
    return ref ? (qdr_delivery_t*) ref->ref : 0;
}


void qd_link_abandoned_deliveries_handler(void *context, qd_link_t *link)
{
    qd_router_t    *router = (qd_router_t*) context;
    qd_link_ref_list_t *list = qd_link_get_ref_list(link);
    qd_link_ref_t      *ref  = DEQ_HEAD(*list);

    while (ref) {
        DEQ_REMOVE_HEAD(*list);
        qdr_delivery_t *dlv = (qdr_delivery_t*) ref->ref;
        ref->ref = 0;
        qdr_delivery_set_context(dlv, 0);
        qdr_delivery_decref(router->router_core, dlv, "qd_link_abandoned_deliveries_handler");
        free_qd_link_ref_t(ref);
        ref = DEQ_HEAD(*list);
    }
}


// read the delivery-state set by the remote endpoint
//
static qd_delivery_state_t *qd_delivery_read_remote_state(pn_delivery_t *pnd)
{
    qd_delivery_state_t *dstate = 0;
    uint64_t            outcome = pn_delivery_remote_state(pnd);

    switch (outcome) {
    case 0:
        // not set - no delivery-state
        break;
    case PN_RECEIVED: {
        pn_disposition_t *disp = pn_delivery_remote(pnd);
        dstate = qd_delivery_state();
        dstate->section_number = pn_disposition_get_section_number(disp);
        dstate->section_offset = pn_disposition_get_section_offset(disp);
        break;
    }
    case PN_ACCEPTED:
    case PN_RELEASED:
        // no associated state (that we care about)
        break;
    case PN_REJECTED: {
        // See AMQP 1.0 section 3.4.4 Rejected
        pn_condition_t *cond = pn_disposition_condition(pn_delivery_remote(pnd));
        dstate = qd_delivery_state();
        dstate->error = qdr_error_from_pn(cond);
        break;
    }
    case PN_MODIFIED: {
        // See AMQP 1.0 section 3.4.5 Modified
        pn_disposition_t *disp = pn_delivery_remote(pnd);
        bool failed = pn_disposition_is_failed(disp);
        bool undeliverable = pn_disposition_is_undeliverable(disp);
        pn_data_t *anno = pn_disposition_annotations(disp);

        // avoid expensive alloc if only default values found
        const bool need_anno = (anno && pn_data_size(anno) > 0);
        if (failed || undeliverable || need_anno) {
            dstate = qd_delivery_state();
            dstate->delivery_failed = failed;
            dstate->undeliverable_here = undeliverable;
            if (need_anno) {
                dstate->annotations = pn_data(0);
                pn_data_copy(dstate->annotations, anno);
            }
        }
        break;
    }
    default: {
        // Check for custom state data. Custom outcomes and AMQP 1.0
        // Transaction defined outcomes will all be numerically >
        // PN_MODIFIED. See Part 4: Transactions and section 1.5 Descriptor
        // Values in the AMQP 1.0 spec.
        if (outcome > PN_MODIFIED) {
            pn_data_t *data = pn_disposition_data(pn_delivery_remote(pnd));
            if (data && pn_data_size(data) > 0) {
                dstate = qd_delivery_state();
                dstate->extension = pn_data(0);
                pn_data_copy(dstate->extension, data);
            }
        }
        break;
    }
    } // end switch

    return dstate;
}


// Set the delivery-state to be sent to the remote endpoint.
//
static void qd_delivery_write_local_state(pn_delivery_t *pnd, uint64_t outcome, const qd_delivery_state_t *dstate)
{
    if (pnd && dstate) {
        switch (outcome) {
        case PN_RECEIVED: {
            pn_disposition_t *ldisp = pn_delivery_local(pnd);
            pn_disposition_set_section_number(ldisp, dstate->section_number);
            pn_disposition_set_section_offset(ldisp, dstate->section_offset);
            break;
        }
        case PN_ACCEPTED:
        case PN_RELEASED:
            // no associated state (that we care about)
            break;
        case PN_REJECTED:
            if (dstate->error) {
                pn_condition_t *condition = pn_disposition_condition(pn_delivery_local(pnd));
                char *name = qdr_error_name(dstate->error);
                char *description = qdr_error_description(dstate->error);
                pn_condition_set_name(condition, (const char*) name);
                pn_condition_set_description(condition, (const char*) description);
                if (qdr_error_info(dstate->error)) {
                    pn_data_copy(pn_condition_info(condition), qdr_error_info(dstate->error));
                }
                //proton makes copies of name and description, so it is ok to free them here.
                free(name);
                free(description);
            }
            break;
        case PN_MODIFIED: {
            pn_disposition_t *ldisp = pn_delivery_local(pnd);
            pn_disposition_set_failed(ldisp, dstate->delivery_failed);
            pn_disposition_set_undeliverable(ldisp, dstate->undeliverable_here);
            if (dstate->annotations)
                pn_data_copy(pn_disposition_annotations(ldisp), dstate->annotations);
            break;
        }
        default:
            if (dstate->extension)
                pn_data_copy(pn_disposition_data(pn_delivery_local(pnd)), dstate->extension);
            break;
        }
    }
}


/**
 * Determine the role of a connection
 */
static void qd_router_connection_get_config(const qd_connection_t  *conn,
                                            qdr_connection_role_t  *role,
                                            int                    *cost,
                                            const char            **name,
                                            bool                   *multi_tenant,
                                            bool                   *strip_annotations_in,
                                            bool                   *strip_annotations_out,
                                            int                    *link_capacity)
{
    if (conn) {
        const qd_server_config_t *cf = qd_connection_config(conn);

        *strip_annotations_in  = cf ? cf->strip_inbound_annotations  : true;
        *strip_annotations_out = cf ? cf->strip_outbound_annotations : true;
        *link_capacity         = cf ? cf->link_capacity : 1;

        if (cf && (strcmp(cf->role, router_role) == 0)) {
            *strip_annotations_in  = false;
            *strip_annotations_out = false;
            *role = QDR_ROLE_INTER_ROUTER;
            *cost = cf->inter_router_cost;
        } else if (cf && (strcmp(cf->role, edge_role) == 0)) {
            *strip_annotations_in  = false;
            *strip_annotations_out = false;
            *role = QDR_ROLE_EDGE_CONNECTION;
            *cost = cf->inter_router_cost;
        } else if (cf && (strcmp(cf->role, container_role) == 0))  // backward compat
            *role = QDR_ROLE_ROUTE_CONTAINER;
        else
            *role = QDR_ROLE_NORMAL;

        *name = cf ? cf->name : 0;
        if (*name) {
            if (strncmp("listener/", *name, 9) == 0 ||
                strncmp("connector/", *name, 10) == 0)
                *name = 0;
        }

        *multi_tenant = cf ? cf->multi_tenant : false;
    }
}


static int AMQP_writable_conn_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qdr_connection_t *qconn = (qdr_connection_t*) qd_connection_get_context(conn);

    if (qconn)
        return qdr_connection_process(qconn);
    return 0;
}


static qd_iterator_t *process_router_annotations(qd_router_t   *router,
                                                 qd_message_t  *msg,
                                                 qd_bitmask_t **link_exclusions,
                                                 uint32_t      *distance,
                                                 int           *ingress_index)
{
    qd_iterator_t *ingress_iter = 0;
    bool           edge_mode    = router->router_mode == QD_ROUTER_MODE_EDGE;

    *link_exclusions = 0;
    *distance        = 0;
    *ingress_index   = 0;

    if (!edge_mode) { // Edge routers do not use trace or ingress meta-data
        qd_parsed_field_t *trace = qd_message_get_trace(msg);
        if (trace && qd_parse_is_list(trace)) {
            //
            // Return the distance in hops that this delivery has traveled.
            //
            *distance = qd_parse_sub_count(trace);

            //
            // Create a link-exclusion map for the items in the trace.  This map will
            // contain a one-bit for each link that leads to a neighbor router that
            // the message has already passed through.
            //
            *link_exclusions = qd_tracemask_create(router->tracemask, trace, ingress_index);
        }

        qd_parsed_field_t *ingress = qd_message_get_ingress_router(msg);
        if (ingress && qd_parse_is_scalar(ingress)) {
            ingress_iter = qd_parse_raw(ingress);
        }

    } else {
        // Edge routers do not propagate trace or ingress
        qd_message_disable_trace_annotation(msg);
        qd_message_disable_ingress_router_annotation(msg);
    }

    //
    // Return the iterator to the ingress field _if_ it was present.
    // Otherwise this router is the ingress - return NULL.
    //
    return ingress_iter;
}

static void log_link_message(qd_connection_t *conn, pn_link_t *pn_link, qd_message_t *msg)
{
    assert(conn && pn_link && msg);
    qd_log_source_t *logger = qd_message_log_source();

    // the message processing is expensive as this is done for every message received.
    // Do not bother if not tracing.

    if (qd_log_enabled(logger, QD_LOG_TRACE)) {
        const qd_server_config_t *cf = qd_connection_config(conn);
        if (!cf) return;
        size_t repr_len = qd_message_repr_len();
        char *buf = qd_malloc(repr_len);
        const char *msg_str = qd_message_oversize(msg) ? "oversize message" :
            qd_message_aborted(msg) ? "aborted message" :
            qd_message_repr(msg, buf, repr_len, cf->log_bits);
        if (msg_str) {
            const char *src = pn_terminus_get_address(pn_link_source(pn_link));
            const char *tgt = pn_terminus_get_address(pn_link_target(pn_link));
            qd_log(logger, QD_LOG_TRACE,
                   "[C%"PRIu64"]: %s %s on link '%s' (%s -> %s)",
                   qd_connection_connection_id(conn),
                   pn_link_is_sender(pn_link) ? "Sent" : "Received",
                   msg_str,
                   pn_link_name(pn_link),
                   src ? src : "",
                   tgt ? tgt : "");
        }
        free(buf);
    }
}

/**
 * Inbound Delivery Handler
 *
 * @return true if we've advanced to the next delivery on this link and it is
 * ready for rx processing.  This will cause the container to immediately
 * re-call this function with the next delivery.
 */
static bool AMQP_rx_handler(void* context, qd_link_t *link)
{
    qd_router_t    *router  = (qd_router_t*) context;
    pn_link_t      *pn_link = qd_link_pn(link);

    assert(pn_link);

    if (!pn_link)
        return false;

    // ensure the current delivery is readable
    pn_delivery_t *pnd = pn_link_current(pn_link);
    if (!pnd)
        return false;

    qd_connection_t  *conn   = qd_link_connection(link);

    // DISPATCH-1628 DISPATCH-975 exit if router already closed this connection
    if (conn->closed_locally) {
        return false;
    }

    qdr_delivery_t *delivery = qdr_node_delivery_qdr_from_pn(pnd);
    bool       next_delivery = false;

    //
    // Receive the message into a local representation.
    //
    qd_message_t   *msg   = qd_message_receive(pnd);
    bool receive_complete = qd_message_receive_complete(msg);

    if (!qd_message_oversize(msg)) {
        // message not rejected as oversize
        if (receive_complete) {
            //
            // The entire message has been received and we are ready to consume the delivery by calling pn_link_advance().
            //
            pn_link_advance(pn_link);
            next_delivery = pn_link_current(pn_link) != 0;
        }

        if (qd_message_is_discard(msg)) {
            //
            // Message has been marked for discard, no further processing necessary
            //
            if (receive_complete) {
                // If this discarded delivery has already been settled by proton,
                // set the presettled flag on the delivery to true if it is not already true.
                // Since the entire message has already been received, we directly call the
                // function to set the pre-settled flag since we cannot go thru the core-thread
                // to do this since the delivery has been discarded.
                // Discarded streaming deliveries are not put thru the core thread via the continue action.
                if (pn_delivery_settled(pnd))
                    qdr_delivery_set_presettled(delivery);

                uint64_t local_disp = qdr_delivery_disposition(delivery);
                //
                // Call pn_delivery_update only if the local disposition is different than the pn_delivery's local disposition.
                // This will make sure we call pn_delivery_update only when necessary.
                //
                if (local_disp != 0 && local_disp != pn_delivery_local_state(pnd)) {
                    //
                    // DISPATCH-1626 - This enables pn_delivery_update() and pn_delivery_settle() to be called back to back in the same function call.
                    // CORE_delivery_update() will handle most of the other cases where we need to call pn_delivery_update() followed by pn_delivery_settle().
                    //
                    pn_delivery_update(pnd, local_disp);
                }

                // note: expected that the code that set discard has handled
                // setting disposition and updating flow!
                pn_delivery_settle(pnd);
                if (delivery) {
                    // if delivery already exists then the core thread discarded this
                    // delivery, it will eventually free the qdr_delivery_t and its
                    // associated message - do not free it here.
                    qdr_node_disconnect_deliveries(router->router_core, link, delivery, pnd);
                } else {
                    qd_message_free(msg);
                }
            }
            return next_delivery;
        }
    } else {
        // message is oversize
        if (receive_complete) {
            // set condition, reject, and settle the incoming delivery
            _reject_delivery(pnd, QD_AMQP_COND_MESSAGE_SIZE_EXCEEDED, QD_AMQP_COND_OVERSIZE_DESCRIPTION);
            pn_delivery_settle(pnd);
            // close the link
            pn_link_close(pn_link);
            // set condition and close the connection
            pn_connection_t * pn_conn = qd_connection_pn(conn);
            pn_condition_t * cond = pn_connection_condition(pn_conn);
            (void) pn_condition_set_name(       cond, QD_AMQP_COND_CONNECTION_FORCED);
            (void) pn_condition_set_description(cond, QD_AMQP_COND_OVERSIZE_DESCRIPTION);
            pn_connection_close(pn_conn);
            if (!delivery) {
                // this message has not been forwarded yet, so it will not be
                // cleaned up when the link is freed.
                qd_message_free(msg);
            }
            // stop all message reception on this connection
            conn->closed_locally = true;
        }
        return false;
        // oversize messages are not processed any further
    }

    //
    // If the delivery already exists we've already passed it to the core (2nd
    // frame for a multi-frame transfer). Simply continue.
    //

    if (delivery) {
        qdr_delivery_continue(router->router_core, delivery, pn_delivery_settled(pnd));
        return next_delivery;
    }

    //
    // No pre-existing delivery means we're starting a new delivery or
    // continuing a delivery that has not accumulated enough of the message
    // for forwarding.
    //

    qdr_link_t *rlink = (qdr_link_t*) qd_link_get_context(link);
    if (!rlink) {
        // receive link was closed or deleted - can't be forwarded
        // so no use setting disposition or adding flow
        qd_message_set_discard(msg, true);
        if (receive_complete) {
            pn_delivery_settle(pnd);
            qd_message_free(msg);
        }
        return next_delivery;
    }

    //
    // Validate the content of the delivery as an AMQP message.  This is done
    // partially, only to validate that we can find the fields we need to route
    // the message.
    //
    // If per-message tracing is configured then validate the sections
    // necessary for logging.
    //
    // link-routing: it is not necessary to validate any sections, but doing so
    // will force a message validity check and ensure the message is not null.
    //
    // If the link is anonymous, we must validate through the message
    // properties to find the 'to' field.  If the link is not anonymous, we
    // don't need the 'to' field as we will be using the address from the link
    // target.
    //
    // Check if the user id needs to be validated (see below). If it does we
    // need to validate the message properties section.
    //
    // Otherwise check the message annotations for router annotations necessary
    // for forwarding.
    //
    const bool link_routed    = qdr_link_is_routed(rlink);
    const bool anonymous_link = qdr_link_is_anonymous(rlink);
    const bool check_user     = (conn->policy_settings && !conn->policy_settings->spec.allowUserIdProxy);
    const qd_server_config_t *cf = qd_connection_config(conn);
    const qd_message_depth_t depth = (cf && cf->log_bits != 0) ? QD_DEPTH_APPLICATION_PROPERTIES
        : (link_routed) ? QD_DEPTH_HEADER
        : (anonymous_link || check_user) ? QD_DEPTH_PROPERTIES
        : QD_DEPTH_MESSAGE_ANNOTATIONS;

    const qd_message_depth_status_t depth_valid = qd_message_check_depth(msg, depth);
    switch (depth_valid) {
    case QD_MESSAGE_DEPTH_INVALID:
        qd_log(router->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Incoming message validation failed - rejected",
               conn->connection_id,
               qd_link_link_id(link));
        qd_message_set_discard(msg, true);
        pn_link_flow(pn_link, 1);
        _reject_delivery(pnd, QD_AMQP_COND_DECODE_ERROR, "invalid message format");
        pn_delivery_settle(pnd);
        qd_message_free(msg);
        return next_delivery;
    case QD_MESSAGE_DEPTH_INCOMPLETE:
        return false;  // stop rx processing
    case QD_MESSAGE_DEPTH_OK:
        break;
    }

    // Handle the link-routed case
    //
    if (link_routed) {
        pn_delivery_tag_t dtag = pn_delivery_tag(pnd);

        if (dtag.size > QDR_DELIVERY_TAG_MAX) {
            qd_log(router->log_source, QD_LOG_DEBUG, "link route delivery failure: msg tag size exceeded %zd (max=%d)",
                   dtag.size, QDR_DELIVERY_TAG_MAX);
            qd_message_set_discard(msg, true);
            pn_link_flow(pn_link, 1);
            _reject_delivery(pnd, QD_AMQP_COND_INVALID_FIELD, "delivery tag length exceeded");
            if (receive_complete) {
                pn_delivery_settle(pnd);
                qd_message_free(msg);
            }
            return next_delivery;
        }

        log_link_message(conn, pn_link, msg);
        delivery = qdr_link_deliver_to_routed_link(rlink,
                                                   msg,
                                                   pn_delivery_settled(pnd),
                                                   (uint8_t*) dtag.start,
                                                   dtag.size,
                                                   pn_delivery_remote_state(pnd),
                                                   qd_delivery_read_remote_state(pnd));
        qd_link_set_incoming_msg(link, (qd_message_t*) 0);  // msg no longer exclusive to qd_link
        qdr_node_connect_deliveries(link, delivery, pnd);
        qdr_delivery_decref(router->router_core, delivery, "release protection of return from deliver_to_routed_link");
        return next_delivery;
    }

    // Determine if the user of this connection is allowed to proxy the user_id
    // of messages. A message user_id is proxied when the value in the message
    // properties section differs from the authenticated user name of the
    // connection.  If the user is not allowed to proxy the user_id then the
    // message user_id must be blank or it must be equal to the connection user
    // name.
    //
    if (check_user) {
        // This connection must not allow proxied user_id
        qd_iterator_t *userid_iter  = qd_message_field_iterator(msg, QD_FIELD_USER_ID);
        if (userid_iter) {
            // The user_id property has been specified
            if (qd_iterator_remaining(userid_iter) > 0) {
                // user_id property in message is not blank
                if (!qd_iterator_equal(userid_iter, (const unsigned char *)conn->user_id)) {
                    // This message is rejected: attempted user proxy is disallowed
                    qd_log(router->log_source, QD_LOG_DEBUG,
                           "[C%"PRIu64"][L%"PRIu64"] Message rejected due to user_id proxy violation. User:%s",
                           conn->connection_id,
                           qd_link_link_id(link),
                           conn->user_id);
                    qd_message_set_discard(msg, true);
                    pn_link_flow(pn_link, 1);
                    _reject_delivery(pnd, QD_AMQP_COND_UNAUTHORIZED_ACCESS, "user_id proxy violation");
                    if (receive_complete) {
                        pn_delivery_settle(pnd);
                        qd_message_free(msg);
                    }
                    qd_iterator_free(userid_iter);
                    return next_delivery;
                }
            }
            qd_iterator_free(userid_iter);
        }
    }

    const char *ma_error = qd_message_parse_annotations(msg);
    if (ma_error) {
        qd_log(router->log_source, QD_LOG_WARNING,
               "[C%"PRIu64"][L%"PRIu64"] Message rejected - invalid MA section: %s",
               conn->connection_id, qd_link_link_id(link), ma_error);

        pn_condition_t *condition = pn_disposition_condition(pn_delivery_local(pnd));
        pn_condition_set_name(condition, "amqp:invalid-field");
        pn_condition_set_description(condition, ma_error);
        pn_delivery_update(pnd, PN_REJECTED);
        qd_message_set_discard(msg, true);
        pn_link_flow(pn_link, 1);
        if (receive_complete) {
            pn_delivery_settle(pnd);
            qd_message_free(msg);
        }
        return next_delivery;
    }

    //
    // Head of line blocking avoidance (DISPATCH-1545)
    //
    // Before we can forward a message we need to determine whether or not this
    // message is "streaming" - a large message that has the potential to block
    // other messages sharing the trunk link.  At this point we cannot for sure
    // know the actual length of the incoming message, so we employ the
    // following heuristic to determine if the message is "streaming":
    //
    // - If the message is receive-complete it is NOT a streaming message.
    // - If it is NOT receive-complete:
    //   Continue buffering incoming data until:
    //   - receive has completed => NOT a streaming message
    //   - not rx-complete AND Q2 threshold hit => a streaming message
    //
    // Once Q2 is hit we MUST forward the message regardless of rx-complete
    // since Q2 will block forever unless the incoming data is drained via
    // forwarding.
    //
    if (!receive_complete) {
        if (qd_message_is_streaming(msg) || qd_message_is_Q2_blocked(msg)) {
            qd_log(router->log_source, QD_LOG_DEBUG,
                   "[C%"PRIu64"][L%"PRIu64"] Incoming message classified as streaming. User:%s",
                   conn->connection_id,
                   qd_link_link_id(link),
                   conn->user_id);
        } else {
            // Continue buffering this message
            return false;
        }
    }

    uint32_t       distance = 0;
    int            ingress_index = 0; // Default to _this_ router
    qd_bitmask_t  *link_exclusions = 0;
    qd_iterator_t *ingress_iter = process_router_annotations(router, msg, &link_exclusions, &distance, &ingress_index);

    //
    // If this delivery has traveled further than the known radius of the network topology (plus 1),
    // release and settle the delivery.  This can happen in the case of "flood" multicast where the
    // deliveries follow all available paths.  This will only discard messages that will reach their
    // destinations via shorter paths.
    //
    if (distance > (router->topology_radius + 1)) {
        qd_bitmask_free(link_exclusions);
        qd_message_set_discard(msg, true);
        pn_link_flow(pn_link, 1);
        pn_delivery_update(pnd, PN_RELEASED);
        if (receive_complete) {
            pn_delivery_settle(pnd);
            qd_message_free(msg);
        }
        return next_delivery;
    }

    if (anonymous_link) {
        qd_iterator_t *addr_iter = 0;
        int phase = 0;

        //
        // If the message has delivery annotations, get the to-override field from the annotations.
        //
        qd_parsed_field_t *ma_to = qd_message_get_to_override(msg);
        if (ma_to) {
            addr_iter = qd_iterator_dup(qd_parse_raw(ma_to));
            phase = qd_message_get_phase_annotation(msg);
        }

        //
        // Still no destination address?  Use the TO field from the message properties.
        //
        if (!addr_iter) {
            addr_iter = qd_message_field_iterator(msg, QD_FIELD_TO);

            //
            // If the address came from the TO field and we need to apply a tenant-space,
            // set the to-override with the annotated address.
            //
            if (addr_iter) {
                int tenant_space_length;
                const char *tenant_space = _get_tenant_space(conn, &tenant_space_length);
                if (tenant_space) {
                    qd_iterator_reset_view(addr_iter, ITER_VIEW_ADDRESS_WITH_SPACE);
                    qd_iterator_annotate_space(addr_iter, tenant_space, tenant_space_length);
                    char *iter_str = (char *)qd_iterator_copy(addr_iter);
                    qd_message_set_to_override_annotation(msg, iter_str);
                    free(iter_str);
                }
            }
        }

        if (addr_iter) {
            if (!conn->policy_settings || qd_policy_approve_message_target(addr_iter, conn)) {
                qd_iterator_reset_view(addr_iter, ITER_VIEW_ADDRESS_HASH);
                if (phase > 0)
                    qd_iterator_annotate_phase(addr_iter, '0' + (char) phase);

                log_link_message(conn, pn_link, msg);
                delivery = qdr_link_deliver_to(rlink, msg, ingress_iter, addr_iter, pn_delivery_settled(pnd),
                                               link_exclusions, ingress_index,
                                               pn_delivery_remote_state(pnd),
                                               qd_delivery_read_remote_state(pnd));
            } else {
                //reject
                qd_log(router->log_source, QD_LOG_DEBUG,
                       "[C%"PRIu64"][L%"PRIu64"] Message rejected due to policy violation on target. User:%s",
                       conn->connection_id,
                       qd_link_link_id(link),
                       conn->user_id);
                qd_message_set_discard(msg, true);
                pn_link_flow(pn_link, 1);
                _reject_delivery(pnd, QD_AMQP_COND_UNAUTHORIZED_ACCESS, "policy violation on target");
                if (receive_complete) {
                    pn_delivery_settle(pnd);
                    qd_message_free(msg);
                }
                qd_iterator_free(addr_iter);
                qd_bitmask_free(link_exclusions);
                return next_delivery;
            }
        }
    } else {
        //
        // This is a targeted link, not anonymous.
        //

        //
        // Look in a series of locations for the terminus address, starting
        // with the qdr_link (in case this is an auto-link with separate
        // internal and external addresses).
        //
        const char *term_addr = qdr_link_internal_address(rlink);
        if (!term_addr) {
            term_addr = pn_terminus_get_address(qd_link_remote_target(link));
            if (!term_addr)
                term_addr = pn_terminus_get_address(qd_link_source(link));
        }

        if (term_addr) {
            int tenant_space_length;
            const char *tenant_space = _get_tenant_space(conn, &tenant_space_length);
            if (tenant_space) {
                qd_iterator_t *aiter = qd_iterator_string(term_addr, ITER_VIEW_ADDRESS_WITH_SPACE);
                qd_iterator_annotate_space(aiter, tenant_space, tenant_space_length);
                char *iter_str = (char *) qd_iterator_copy(aiter);
                qd_message_set_to_override_annotation(msg, iter_str);
                free(iter_str);
                qd_iterator_free(aiter);
            } else
                qd_message_set_to_override_annotation(msg, term_addr);

            int phase = qdr_link_phase(rlink);
            if (phase != 0)
                qd_message_set_phase_annotation(msg, phase);
        }

        log_link_message(conn, pn_link, msg);
        delivery = qdr_link_deliver(rlink, msg, ingress_iter, pn_delivery_settled(pnd), link_exclusions, ingress_index,
                                    pn_delivery_remote_state(pnd),
                                    qd_delivery_read_remote_state(pnd));
    }

    //
    // End of new delivery processing
    //

    if (delivery) {
        qd_link_set_incoming_msg(link, (qd_message_t*) 0);  // msg no longer exclusive to qd_link
        qdr_node_connect_deliveries(link, delivery, pnd);
        qdr_delivery_decref(router->router_core, delivery, "release protection of return from deliver");
    } else {
        //
        // If there is no delivery, the message is now and will always be unroutable because there is no address.
        //
        qd_log(router->log_source, QD_LOG_DEBUG,
               "[C%"PRIu64"][L%"PRIu64"] Message rejected - no address present",
               conn->connection_id,
               qd_link_link_id(link));
        qd_bitmask_free(link_exclusions);
        qd_message_set_discard(msg, true);
        pn_link_flow(pn_link, 1);
        _reject_delivery(pnd, QD_AMQP_COND_PRECONDITION_FAILED, "Routing failure: no address present");
        if (receive_complete) {
            pn_delivery_settle(pnd);
            qd_message_free(msg);
        }
    }

    return next_delivery;
}


/**
 * Deferred callback for inbound delivery handler
 */
static void deferred_AMQP_rx_handler(void *context, bool discard)
{
    qd_link_t_sp *safe_qdl = (qd_link_t_sp*) context;

    if (!discard) {
        qd_link_t *qdl = safe_deref_qd_link_t(*safe_qdl);
        if (!!qdl) {
            assert(qd_link_direction(qdl) == QD_INCOMING);
            qd_router_t *qdr = (qd_router_t*) qd_link_get_node_context(qdl);
            assert(qdr != 0);
            while (true) {
                if (!AMQP_rx_handler(qdr, qdl))
                    break;
            }
        }
    }

    free(safe_qdl);
}


/**
 * Delivery Disposition Handler
 */
static void AMQP_disposition_handler(void* context, qd_link_t *link, pn_delivery_t *pnd)
{
    qd_router_t    *router   = (qd_router_t*) context;
    qdr_delivery_t *delivery = qdr_node_delivery_qdr_from_pn(pnd);

    //
    // It's important to not do any processing without a qdr_delivery.
    if (!delivery)
        return;

    uint64_t dstate = pn_delivery_remote_state(pnd);
    bool settled = pn_delivery_settled(pnd);

    //
    // When pre-settled multi-frame deliveries arrive, it's possible for the
    // settlement to register before the whole message arrives.  Such premature
    // settlement indications must be ignored.
    //
    if (settled && !qdr_delivery_receive_complete(delivery))
        settled = false;

    if (dstate || settled) {
        //
        // Update the disposition of the delivery
        //
        qdr_delivery_remote_state_updated(router->router_core, delivery,
                                          dstate,
                                          settled,
                                          qd_delivery_read_remote_state(pnd),
                                          false);

        //
        // If settled, close out the delivery
        //
        if (settled) {
            qdr_node_disconnect_deliveries(router->router_core, link, delivery, pnd);
            pn_delivery_settle(pnd);
        }
    }
}


/**
 * New Incoming Link Handler
 */
static int AMQP_incoming_link_handler(void* context, qd_link_t *link)
{
    qd_connection_t  *conn     = qd_link_connection(link);
    uint64_t link_id;

    // The connection that this link belongs to is gone. Perhaps an AMQP close came in.
    // This link handler should not continue since there is no connection.
    if (conn == 0)
        return 0;

    qdr_connection_t *qdr_conn = (qdr_connection_t*) qd_connection_get_context(conn);

    char *terminus_addr = (char*)pn_terminus_get_address(pn_link_remote_target((pn_link_t  *)qd_link_pn(link)));

    qdr_link_t       *qdr_link = qdr_link_first_attach(qdr_conn, QD_INCOMING,
                                                       qdr_terminus(qd_link_remote_source(link)),
                                                       qdr_terminus(qd_link_remote_target(link)),
                                                       pn_link_name(qd_link_pn(link)),
                                                       terminus_addr,
                                                       false,
                                                       0,
                                                       &link_id);
    qd_link_set_link_id(link, link_id);
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
    uint64_t link_id;

    // The connection that this link belongs to is gone. Perhaps an AMQP close came in.
    // This link handler should not continue since there is no connection.
    if (conn == 0)
        return 0;

    qdr_connection_t *qdr_conn = (qdr_connection_t*) qd_connection_get_context(conn);
    char *terminus_addr = (char*)pn_terminus_get_address(pn_link_remote_source((pn_link_t  *)qd_link_pn(link)));
    qdr_link_t *qdr_link = qdr_link_first_attach(qdr_conn, QD_OUTGOING,
                                                 qdr_terminus(qd_link_remote_source(link)),
                                                 qdr_terminus(qd_link_remote_target(link)),
                                                 pn_link_name(qd_link_pn(link)),
                                                 terminus_addr,
                                                 false,
                                                 0,
                                                 &link_id);
    qd_link_set_link_id(link, link_id);
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
 * Handler for flow events on links.  Flow updates include session window
 * state, which needs to be checked for unblocking Q3.
 */
static int AMQP_link_flow_handler(void* context, qd_link_t *link)
{
    qd_router_t *router  = (qd_router_t*) context;
    pn_link_t   *pnlink  = qd_link_pn(link);
    qdr_link_t  *rlink   = (qdr_link_t*) qd_link_get_context(link);

    if (rlink) {
        qdr_link_flow(router->router_core, rlink, pn_link_remote_credit(pnlink), pn_link_get_drain(pnlink));
    }

    // check if Q3 can be unblocked
    pn_session_t *pn_ssn = pn_link_session(pnlink);
    if (pn_ssn) {
        qd_session_t *qd_ssn = qd_session_from_pn(pn_ssn);
        if (qd_ssn && qd_session_is_q3_blocked(qd_ssn)) {
            // Q3 blocked - have we drained enough outgoing bytes?
            const size_t q3_lower = BUFFER_SIZE * QD_QLIMIT_Q3_LOWER;
            if (pn_session_outgoing_bytes(pn_ssn) < q3_lower) {
                // yes.  We must now unblock all links that have been blocked by Q3
                qd_link_list_t *blinks = qd_session_q3_blocked_links(qd_ssn);
                qd_link_t *blink = DEQ_HEAD(*blinks);
                while (blink) {
                    qd_link_q3_unblock(blink);  // removes from blinks list!
                    if (blink != link) {        // already flowed this link
                        rlink = (qdr_link_t *) qd_link_get_context(blink);
                        if (rlink) {
                            pnlink = qd_link_pn(blink);
                            // signalling flow to the core causes the link to be re-activated
                            qdr_link_flow(router->router_core, rlink, pn_link_remote_credit(pnlink), pn_link_get_drain(pnlink));
                        }
                    }
                    blink = DEQ_HEAD(*blinks);
                }
            }
        }
    }
    return 0;
}


/**
 * Link Detached Handler
 */
static int AMQP_link_detach_handler(void* context, qd_link_t *link, qd_detach_type_t dt)
{
    if (!link)
        return 0;

    pn_link_t *pn_link = qd_link_pn(link);
    if (!pn_link)
        return 0;

    // DISPATCH-1085: If link is in the middle of receiving a message it is
    // possible that the message is actually complete but the remaining message
    // data is still in proton's buffers.  (e.g. a large message is sent then
    // the sender immediately detaches) Force a call to the rx_handler for the
    // link in order to pull the buffered data into the message.
    if (pn_link_is_receiver(pn_link)) {
        pn_delivery_t *pnd = pn_link_current(pn_link);
        if (pnd) {
            qd_message_t *msg = qd_get_message_context(pnd);
            if (msg) {
                if (!qd_message_receive_complete(msg)) {
                    qd_link_set_q2_limit_unbounded(link, true);

                    // since this thread owns link we can call the
                    // rx_hander directly rather than schedule it via
                    // the unblock handler:
                    qd_message_clear_q2_unblocked_handler(msg);
                    qd_message_Q2_holdoff_disable(msg);
                    while (AMQP_rx_handler((qd_router_t*) context, link))
                           ;
                }
            }
        }
    }

    qdr_link_t     *rlink  = (qdr_link_t*) qd_link_get_context(link);
    pn_condition_t *cond   = qd_link_pn(link) ? pn_link_remote_condition(qd_link_pn(link)) : 0;

    if (rlink) {
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

        qdr_error_t *error = qdr_error_from_pn(cond);
        qdr_link_detach(rlink, dt, error);
    }

    return 0;
}

static void bind_connection_context(qdr_connection_t *qdrc, void* token)
{
    qd_connection_t *conn = (qd_connection_t*) token;
    qd_connection_set_context(conn, qdrc);
    qdr_connection_set_context(qdrc, conn);
}


static void save_original_and_current_conn_info(qd_connection_t *conn)
{
    // The failover list is present but it is empty. We will wipe the old backup information from the failover list.
    // The only items we want to keep in this list is the original connection information (from the config file)
    // and the current connection information.

    if (conn->connector && DEQ_SIZE(conn->connector->conn_info_list) > 1) {
        // Here we are simply removing all other failover information except the original connection information and the one we used to make a successful connection.
        int i = 1;
        qd_failover_item_t *item = DEQ_HEAD(conn->connector->conn_info_list);
        qd_failover_item_t *next_item = 0;

        bool match_found = false;
        int dec_conn_index=0;

        while(item) {

            //The first item on this list is always the original connector, so we want to keep that item in place
            // We have to delete items in the list that were left over from the previous failover list from the previous connection
            // because the new connection might have its own failover list.
            if (i != conn->connector->conn_index) {
                if (item != DEQ_HEAD(conn->connector->conn_info_list)) {
                    next_item = DEQ_NEXT(item);
                    free(item->scheme);
                    free(item->host);
                    free(item->port);
                    free(item->hostname);
                    free(item->host_port);

                    DEQ_REMOVE(conn->connector->conn_info_list, item);

                    free(item);
                    item = next_item;

                    // We are removing an item from the list before the conn_index match was found. We need to
                    // decrement the conn_index
                    if (!match_found)
                        dec_conn_index += 1;
                }
                else {
                    item = DEQ_NEXT(item);
                }
            }
            else {
                match_found = true;
                item = DEQ_NEXT(item);
            }
            i += 1;
        }

        conn->connector->conn_index -= dec_conn_index;
    }
}

static void AMQP_opened_handler(qd_router_t *router, qd_connection_t *conn, bool inbound)
{
    qdr_connection_role_t  role = 0;
    int                    cost = 1;
    int                    link_capacity = 1;
    const char            *name = 0;
    bool                   multi_tenant = false;
    bool                   streaming_links = false;
    const char            *vhost = 0;
    char                   rversion[128];
    uint64_t               connection_id = qd_connection_connection_id(conn);
    pn_connection_t       *pn_conn = qd_connection_pn(conn);
    pn_transport_t *tport = 0;
    pn_sasl_t      *sasl  = 0;
    pn_ssl_t       *ssl   = 0;
    const char     *mech  = 0;
    const char     *user  = 0;
    const char *container = conn->pn_conn ? pn_connection_remote_container(conn->pn_conn) : 0;

    rversion[0] = 0;
    conn->strip_annotations_in  = false;
    conn->strip_annotations_out = false;
    if (conn->pn_conn) {
        tport = pn_connection_transport(conn->pn_conn);
        ssl   = conn->ssl;
    }
    if (tport) {
        sasl = pn_sasl(tport);
        if(conn->user_id)
            user = conn->user_id;
        else
            user = pn_transport_get_user(tport);
    }

    if (sasl)
        mech = pn_sasl_get_mech(sasl);

    const char *host = 0;
    char host_local[255];
    const qd_server_config_t *config;
    if (qd_connection_connector(conn)) {
        config = qd_connector_config(qd_connection_connector(conn));
        snprintf(host_local, 254, "%s", config->host_port);
        host = &host_local[0];
    }
    else
        host = qd_connection_name(conn);


    qd_router_connection_get_config(conn, &role, &cost, &name, &multi_tenant,
                                    &conn->strip_annotations_in, &conn->strip_annotations_out, &link_capacity);

    // check offered capabilities for streaming link support
    //
    pn_data_t *ocaps = pn_connection_remote_offered_capabilities(pn_conn);
    if (ocaps) {
        size_t sl_len = strlen(QD_CAPABILITY_STREAMING_LINKS);
        pn_data_rewind(ocaps);
        if (pn_data_next(ocaps)) {
            if (pn_data_type(ocaps) == PN_ARRAY) {
                pn_data_enter(ocaps);
                pn_data_next(ocaps);
            }
            do {
                if (pn_data_type(ocaps) == PN_SYMBOL) {
                    pn_bytes_t s = pn_data_get_symbol(ocaps);
                    streaming_links = (s.size == sl_len
                                       && strncmp(s.start, QD_CAPABILITY_STREAMING_LINKS, sl_len) == 0);
                }
            } while (pn_data_next(ocaps) && !streaming_links);
        }
    }

    // if connection properties are present parse out any important data
    //
    pn_data_t *props = pn_conn ? pn_connection_remote_properties(pn_conn) : 0;
    if (props) {
        const bool is_router = (role == QDR_ROLE_INTER_ROUTER || role == QDR_ROLE_EDGE_CONNECTION);
        pn_data_rewind(props);
        if (pn_data_next(props) && pn_data_type(props) == PN_MAP) {
            const size_t num_items = pn_data_get_map(props);
            int props_found = 0;  // once all props found exit loop
            pn_data_enter(props);
            for (int i = 0; i < num_items / 2 && props_found < 4; ++i) {
                if (!pn_data_next(props)) break;
                if (pn_data_type(props) != PN_SYMBOL) break;  // invalid properties map
                pn_bytes_t key = pn_data_get_symbol(props);

                if (key.size == strlen(QD_CONNECTION_PROPERTY_COST_KEY) &&
                    strncmp(key.start, QD_CONNECTION_PROPERTY_COST_KEY, key.size) == 0) {
                    props_found += 1;
                    if (!pn_data_next(props)) break;
                    if (is_router) {
                        if (pn_data_type(props) == PN_INT) {
                            const int remote_cost = (int) pn_data_get_int(props);
                            if (remote_cost > cost)
                                cost = remote_cost;
                        }
                    }

                } else if (key.size == strlen(QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY) &&
                           strncmp(key.start, QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY, key.size) == 0) {
                    props_found += 1;
                    if (!pn_data_next(props)) break;
                    parse_failover_property_list(router, conn, props);

                } else if (key.size == strlen(QD_CONNECTION_PROPERTY_VERSION_KEY)
                           && strncmp(key.start, QD_CONNECTION_PROPERTY_VERSION_KEY, key.size) == 0) {
                    props_found += 1;
                    if (!pn_data_next(props)) break;
                    if (is_router) {
                        pn_bytes_t vdata = pn_data_get_string(props);
                        size_t vlen = MIN(sizeof(rversion) - 1, vdata.size);
                        strncpy(rversion, vdata.start, vlen);
                        rversion[vlen] = 0;
                    }

                } else if ((key.size == strlen(QD_CONNECTION_PROPERTY_ANNOTATIONS_VERSION_KEY)
                           && strncmp(key.start, QD_CONNECTION_PROPERTY_ANNOTATIONS_VERSION_KEY, key.size) == 0)) {
                    props_found += 1;
                    if (!pn_data_next(props)) break;
                    if (is_router && pn_data_type(props) == PN_INT) {
                        const int annos_version = (int) pn_data_get_int(props);
                        qd_log(router->log_source, QD_LOG_DEBUG,
                               "Remote router annotations version: %d", annos_version);
                    }

                } else {
                    // skip this key
                    if (!pn_data_next(props)) break;
                }
            }
        }
    }


    if (multi_tenant)
        vhost = (conn->policy_settings && conn->policy_settings->vhost_name) ?
                conn->policy_settings->vhost_name :
                pn_connection_remote_hostname(pn_conn);

    char proto[50];
    memset(proto, 0, 50);
    char cipher[50];
    memset(cipher, 0, 50);

    int ssl_ssf = 0;
    bool is_ssl = false;

    if (ssl) {
        pn_ssl_get_protocol_name(ssl, proto, 50);
        pn_ssl_get_cipher_name(ssl, cipher, 50);
        ssl_ssf = pn_ssl_get_ssf(ssl);
        is_ssl = true;
    }


    bool encrypted     = tport && pn_transport_is_encrypted(tport);
    bool authenticated = tport && pn_transport_is_authenticated(tport);

    qdr_connection_info_t *connection_info = qdr_connection_info(encrypted,
                                                                 authenticated,
                                                                 conn->opened,
                                                                 (char*) mech,
                                                                 conn->connector ? QD_OUTGOING : QD_INCOMING,
                                                                 host,
                                                                 proto,
                                                                 cipher,
                                                                 (char*) user,
                                                                 container,
                                                                 props,
                                                                 ssl_ssf,
                                                                 is_ssl,
                                                                 rversion,
                                                                 streaming_links);

    qdr_connection_opened(router->router_core,
                          amqp_direct_adaptor,
                          inbound,
                          role,
                          cost,
                          connection_id,
                          name,
                          pn_connection_remote_container(pn_conn),
                          conn->strip_annotations_in,
                          conn->strip_annotations_out,
                          link_capacity,
                          vhost,
                          !!conn->policy_settings ? &conn->policy_settings->spec : 0,
                          connection_info,
                          bind_connection_context,
                          conn);

    if (conn->connector) {
        char conn_msg[300];
        qd_format_string(conn_msg, 300, "[C%"PRIu64"] Connection Opened: dir=%s host=%s vhost=%s encrypted=%s"
                " auth=%s user=%s container_id=%s",
                connection_id, inbound ? "in" : "out", host, vhost ? vhost : "", encrypted ? proto : "no",
                        authenticated ? mech : "no", (char*) user, container);
        sys_mutex_lock(conn->connector->lock);
        strcpy(conn->connector->conn_msg, conn_msg);
        sys_mutex_unlock(conn->connector->lock);
    }
}


// We are attempting to find a connection property called failover-server-list which is a list of failover host names and ports..
// failover-server-list looks something like this
//      :"failover-server-list"=[{:"network-host"="some-host", :port="35000"}, {:"network-host"="localhost", :port="25000"}]
// There are three cases here -
// 1. The failover-server-list is present but the content of the list is empty in which case we scrub the failover list except we keep the original connector information and current connection information.
// 2. If the failover list contains one or more maps that contain failover connection information, that information will be appended to the list which already contains the original connection information
//    and the current connection information. Any other failover information left over from the previous connection is deleted
// 3. If the failover-server-list is not present at all in the connection properties, the failover list we maintain is untouched.
//
// props should be pointing at the value that corresponds to the QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY
// returns true if failover list properly parsed.
//
static bool parse_failover_property_list(qd_router_t *router, qd_connection_t *conn, pn_data_t *props)
{
    bool found_failover = false;

    if (pn_data_type(props) != PN_LIST)
        return false;

    size_t list_num_items = pn_data_get_list(props);

    if (list_num_items > 0) {

        save_original_and_current_conn_info(conn);

        pn_data_enter(props); // enter list

        for (int i=0; i < list_num_items; i++) {
            pn_data_next(props);// this is the first element of the list, a map.
            if (props && pn_data_type(props) == PN_MAP) {

                size_t map_num_items = pn_data_get_map(props);
                pn_data_enter(props);

                qd_failover_item_t *item = NEW(qd_failover_item_t);
                ZERO(item);

                // We have found a map with the connection information. Step thru the map contents and create qd_failover_item_t

                for (int j=0; j < map_num_items/2; j++) {

                    pn_data_next(props);
                    if (pn_data_type(props) == PN_SYMBOL) {
                        pn_bytes_t sym = pn_data_get_symbol(props);
                        if (sym.size == strlen(QD_CONNECTION_PROPERTY_FAILOVER_NETHOST_KEY) &&
                            strcmp(sym.start, QD_CONNECTION_PROPERTY_FAILOVER_NETHOST_KEY) == 0) {
                            pn_data_next(props);
                            if (pn_data_type(props) == PN_STRING) {
                                item->host = strdup(pn_data_get_string(props).start);
                            }
                        }
                        else if (sym.size == strlen(QD_CONNECTION_PROPERTY_FAILOVER_PORT_KEY) &&
                                 strcmp(sym.start, QD_CONNECTION_PROPERTY_FAILOVER_PORT_KEY) == 0) {
                            pn_data_next(props);
                            item->port = qdpn_data_as_string(props);

                        }
                        else if (sym.size == strlen(QD_CONNECTION_PROPERTY_FAILOVER_SCHEME_KEY) &&
                                 strcmp(sym.start, QD_CONNECTION_PROPERTY_FAILOVER_SCHEME_KEY) == 0) {
                            pn_data_next(props);
                            if (pn_data_type(props) == PN_STRING) {
                                item->scheme = strdup(pn_data_get_string(props).start);
                            }

                        }
                        else if (sym.size == strlen(QD_CONNECTION_PROPERTY_FAILOVER_HOSTNAME_KEY) &&
                                 strcmp(sym.start, QD_CONNECTION_PROPERTY_FAILOVER_HOSTNAME_KEY) == 0) {
                            pn_data_next(props);
                            if (pn_data_type(props) == PN_STRING) {
                                item->hostname = strdup(pn_data_get_string(props).start);
                            }
                        }
                    }
                }

                int host_length = strlen(item->host);
                //
                // We will not even bother inserting the item if there is no host available.
                //
                if (host_length != 0) {
                    if (item->scheme == 0)
                        item->scheme = strdup("amqp");
                    if (item->port == 0)
                        item->port = strdup("5672");

                    int hplen = strlen(item->host) + strlen(item->port) + 2;
                    item->host_port = malloc(hplen);
                    snprintf(item->host_port, hplen, "%s:%s", item->host, item->port);

                    //
                    // Iterate through failover list items and sets insert_tail to true
                    // when list has just original connector's host and port or when new
                    // reported host and port is not yet part of the current list.
                    //
                    bool insert_tail = false;
                    if ( DEQ_SIZE(conn->connector->conn_info_list) == 1 ) {
                        insert_tail = true;
                    } else {
                        qd_failover_item_t *conn_item = DEQ_HEAD(conn->connector->conn_info_list);
                        insert_tail = true;
                        while ( conn_item ) {
                            if ( !strcmp(conn_item->host_port, item->host_port) ) {
                                insert_tail = false;
                                break;
                            }
                            conn_item = DEQ_NEXT(conn_item);
                        }
                    }

                    // Only inserts if not yet part of failover list
                    if ( insert_tail ) {
                        DEQ_INSERT_TAIL(conn->connector->conn_info_list, item);
                        qd_log(router->log_source, QD_LOG_DEBUG, "Added %s as backup host", item->host_port);
                        found_failover = true;
                    }
                    else {
                        free(item->scheme);
                        free(item->host);
                        free(item->port);
                        free(item->hostname);
                        free(item->host_port);
                        free(item);
                    }

                }
                else {
                    free(item->scheme);
                    free(item->host);
                    free(item->port);
                    free(item->hostname);
                    free(item->host_port);
                    free(item);
                }
            }
            pn_data_exit(props);
        }
    } // list_num_items > 0
    else {
        save_original_and_current_conn_info(conn);
    }

    return found_failover;
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
    qdr_connection_t *qdrc   = (qdr_connection_t*) qd_connection_get_context(conn);
    qd_router_t      *router = (qd_router_t*) type_context;

    if (qdrc) {
        sys_mutex_lock(qd_server_get_activation_lock(router->qd->server));
        qdr_connection_set_context(qdrc, 0);
        sys_mutex_unlock(qd_server_get_activation_lock(router->qd->server));

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

    // This sends a tick into the core and this happens every second.
    qdr_process_tick(router->router_core);
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
                                     qd_link_abandoned_deliveries_handler,
                                     AMQP_link_flow_handler,
                                     0,   // node_created_handler
                                     0,   // node_destroyed_handler
                                     AMQP_inbound_opened_handler,
                                     AMQP_outbound_opened_handler,
                                     AMQP_closed_handler};


// not api, but needed by unit tests
void qd_router_id_initialize(const char *area, const char *id)
{
    size_t dplen = 2 + strlen(area) + strlen(id);
    node_id = (char*) qd_malloc(dplen);
    strcpy(node_id, area);
    strcat(node_id, "/");
    strcat(node_id, id);

    // Node ID as an AMQP encoded str value.  Used when composing trace list
    // and ingress message annotations into the outgoing message

    const uint32_t id_len = strlen(node_id);
    const uint32_t extra = 5;  // 5 octets = max AMQP STRx header length
    encoded_node_id = (uint8_t*) qd_malloc(id_len + extra + 1); // 1 = string terminator
    const int hdr_len = qd_compose_str_header(encoded_node_id, id_len);
    assert(hdr_len <= extra);
    strcpy((char*) &encoded_node_id[hdr_len], node_id);
    encoded_node_id_len = hdr_len + id_len;
}


// not api, but needed by unit tests
void qd_router_id_finalize(void)
{
    free(node_id);
    node_id = 0;
    free(encoded_node_id);
    encoded_node_id = 0;
    encoded_node_id_len = 0;
}


qd_router_t *qd_router(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id)
{
    qd_container_register_node_type(qd, &router_node);

    qd_router_id_initialize(area, id);

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

    sys_atomic_init(&global_delivery_id, 1);

    //
    // Inform the field iterator module of this router's mode, id, and area.  The field iterator
    // uses this to offload some of the address-processing load from the router.
    //
    qd_iterator_set_address(mode == QD_ROUTER_MODE_EDGE, area, id);

    switch (router->router_mode) {
    case QD_ROUTER_MODE_STANDALONE: qd_log(router->log_source, QD_LOG_INFO, "Router started in Standalone mode");  break;
    case QD_ROUTER_MODE_INTERIOR:   qd_log(router->log_source, QD_LOG_INFO, "Router started in Interior mode, area=%s id=%s", area, id);  break;
    case QD_ROUTER_MODE_EDGE:       qd_log(router->log_source, QD_LOG_INFO, "Router started in Edge mode");  break;
    case QD_ROUTER_MODE_ENDPOINT:   qd_log(router->log_source, QD_LOG_INFO, "Router started in Endpoint mode");  break;
    }

    qd_log(router->log_source, QD_LOG_INFO, "Version: %s", QPID_DISPATCH_VERSION);

    return router;
}


static void CORE_connection_activate(void *context, qdr_connection_t *conn)
{
    qd_router_t *router = (qd_router_t*) context;

    //
    // IMPORTANT:  This is the only core callback that is invoked on the core
    //             thread itself. It must not take locks that could deadlock the core.
    //
    sys_mutex_lock(qd_server_get_activation_lock(router->qd->server));
    qd_server_activate((qd_connection_t*) qdr_connection_get_context(conn));
    sys_mutex_unlock(qd_server_get_activation_lock(router->qd->server));
}


static void CORE_link_first_attach(void             *context,
                                   qdr_connection_t *conn,
                                   qdr_link_t       *link, 
                                   qdr_terminus_t   *source,
                                   qdr_terminus_t   *target,
                                   qd_session_class_t ssn_class)
{
    qd_router_t     *router = (qd_router_t*) context;
    qd_connection_t *qconn  = (qd_connection_t*) qdr_connection_get_context(conn);
    if (!qconn) return;        /* Connection is already closed */

    //
    // Create a new link to be attached
    //
    qd_link_t *qlink = qd_link(router->node, qconn, qdr_link_direction(link), qdr_link_name(link), ssn_class);

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

    //
    // Mark the link as stalled and waiting for initial credit.
    //
    if (qdr_link_direction(link) == QD_OUTGOING)
        qdr_link_stalled_outbound(link);
}


static void CORE_link_second_attach(void *context, qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target)
{
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    pn_link_t *pn_link = qd_link_pn(qlink);
    if (!pn_link)
        return;

    qdr_terminus_copy(source, qd_link_source(qlink));
    qdr_terminus_copy(target, qd_link_target(qlink));

    //
    // Open (attach) the link
    //
    pn_link_open(pn_link);

    //
    // Mark the link as stalled and waiting for initial credit.
    //
    if (qdr_link_direction(link) == QD_OUTGOING)
        qdr_link_stalled_outbound(link);
}


static void CORE_conn_trace(void *context, qdr_connection_t *qdr_conn, bool trace)
{
    qd_connection_t *qconn = (qd_connection_t*) qdr_connection_get_context(qdr_conn);

    if (!qconn)
        return;

    pn_transport_t *tport = pn_connection_transport(qconn->pn_conn);

    if (!tport)
        return;

    if (trace) {
        pn_transport_trace(tport, PN_TRACE_FRM);
        pn_transport_set_tracer(tport, connection_transport_tracer);
    }
    else {
        pn_transport_trace(tport, PN_TRACE_OFF);
    }
}

static void CORE_close_connection(void *context, qdr_connection_t *qdr_conn, qdr_error_t *error)
{
    if (qdr_conn) {
        qd_connection_t *qd_conn = qdr_connection_get_context(qdr_conn);
        if (qd_conn) {
            pn_connection_t *pn_conn = qd_connection_pn(qd_conn);
            if (pn_conn) {
                //
                // Go down to the transport and close the head and tail.  This will
                // drop the socket to the peer without providing any error indication.
                // Due to issues in Proton that cause different behaviors in different
                // bindings depending on whether there is a connection:forced error,
                // this has been deemed the best way to force the peer to reconnect.
                //
                pn_transport_t *tport = pn_connection_transport(pn_conn);
                pn_transport_close_head(tport);
                pn_transport_close_tail(tport);
            }
        }
    }
}

static void CORE_link_detach(void *context, qdr_link_t *link, qdr_error_t *error, bool first, bool close)
{
    qd_link_t   *qlink  = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    pn_link_t *pn_link = qd_link_pn(qlink);
    if (!pn_link)
        return;

    if (error) {
        pn_condition_t *cond = pn_link_condition(pn_link);
        qdr_error_copy(error, cond);
    }

    //
    // If the link is only half open, then this DETACH constitutes the rejection of
    // an incoming ATTACH.  We must nullify the source and target in order to be
    // compliant with the AMQP specification.  This is because Proton will generate
    // the missing ATTACH before the DETACH and will include spurious terminus data
    // if we don't nullify it here.
    //
    if (pn_link_state(pn_link) & PN_LOCAL_UNINIT) {
        if (pn_link_is_receiver(pn_link)) {
            pn_terminus_set_type(pn_link_target(pn_link), PN_UNSPECIFIED);
            pn_terminus_copy(pn_link_source(pn_link), pn_link_remote_source(pn_link));
        } else {
            pn_terminus_set_type(pn_link_source(pn_link), PN_UNSPECIFIED);
            pn_terminus_copy(pn_link_target(pn_link), pn_link_remote_target(pn_link));
        }
    }

    if (close)
        qd_link_close(qlink);
    else
        qd_link_detach(qlink);

    //
    // This is the last event for this link that we are going to send into Proton.
    // Remove the core->proton linkage.  Note that the proton->core linkage may still
    // be intact and needed.
    //
    qdr_link_set_context(link, 0);

    //
    // If this is the second detach, free the qd_link
    //
    if (!first) {
        qd_link_free(qlink);
    }
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
    qd_router_t *router = (qd_router_t*) context;
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    pn_link_t *plink = qd_link_pn(qlink);

    if (plink) {
        pn_link_drained(plink);
        qdr_link_set_drained(router->router_core, link);
    }
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


static int CORE_link_push(void *context, qdr_link_t *link, int limit)
{
    qd_router_t *router = (qd_router_t*) context;
    qd_link_t   *qlink  = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return 0;

    pn_link_t *plink = qd_link_pn(qlink);

    if (plink) {
        int link_credit = pn_link_credit(plink);
        if (link_credit > limit)
            link_credit = limit;

        if (link_credit > 0)
            // We will not bother calling qdr_link_process_deliveries if we have no credit.
            return qdr_link_process_deliveries(router->router_core, link, link_credit);
    }
    return 0;
}

static uint64_t CORE_link_deliver(void *context, qdr_link_t *link, qdr_delivery_t *dlv, bool settled)
{
    qd_router_t *router = (qd_router_t*) context;
    qd_link_t   *qlink  = (qd_link_t*) qdr_link_get_context(link);
    qd_connection_t *qconn = qd_link_connection(qlink);

    uint64_t update = 0;

    if (!qlink)
        return 0;

    pn_link_t *plink = qd_link_pn(qlink);
    if (!plink)
        return 0;

    //
    // If the remote send settle mode is set to 'settled' then settle the delivery on behalf of the receiver.
    //
    bool remote_snd_settled = qd_link_remote_snd_settle_mode(qlink) == PN_SND_SETTLED;
    pn_delivery_t *pdlv = 0;

    if (!qdr_delivery_tag_sent(dlv)) {
        const char *tag;
        int         tag_length;
        uint64_t    disposition = 0;

        qdr_delivery_tag(dlv, &tag, &tag_length);

        // Create a new proton delivery on link 'plink'
        pn_delivery(plink, pn_dtag(tag, tag_length));

        pdlv = pn_link_current(plink);

        // handle any delivery-state on the transfer e.g. transactional-state
        qd_delivery_state_t *dstate = qdr_delivery_take_local_delivery_state(dlv, &disposition);
        if (disposition) {
            if (dstate)
                qd_delivery_write_local_state(pdlv, disposition, dstate);
            pn_delivery_update(pdlv, disposition);
        }
        qd_delivery_state_free(dstate);

        //
        // If the remote send settle mode is set to 'settled', we should settle the delivery on behalf of the receiver.
        //
        if (qdr_delivery_get_context(dlv) == 0)
            qdr_node_connect_deliveries(qlink, dlv, pdlv);

        qdr_delivery_set_tag_sent(dlv, true);
    } else {
        pdlv = qdr_node_delivery_pn_from_qdr(dlv);
    }

    if (!pdlv)
        return 0;

    bool q3_stalled = false;

    qd_message_t *msg_out = qdr_delivery_message(dlv);

    qd_message_send(msg_out, qlink, qdr_link_strip_annotations_out(link), &q3_stalled);

    if (q3_stalled) {
        qd_link_q3_block(qlink);
        qdr_link_stalled_outbound(link);
    }

    bool send_complete = qdr_delivery_send_complete(dlv);

    if (send_complete) {
        if (qd_message_aborted(msg_out)) {
            // Aborted messages must be settled locally
            // Settling does not produce any disposition to message sender.
            if (pdlv) {
                pn_link_advance(plink);
                qdr_node_disconnect_deliveries(router->router_core, qlink, dlv, pdlv);
                pn_delivery_settle(pdlv);
            }
        } else {
            if (!settled && remote_snd_settled) {
                // The caller must tell the core that the delivery has been
                // accepted and settled, since we are settling on behalf of the
                // receiver
                update = PN_ACCEPTED;  // schedule the settle
            }

            pn_link_advance(plink);

            if (settled || remote_snd_settled) {
                if (pdlv) {
                    qdr_node_disconnect_deliveries(router->router_core, qlink, dlv, pdlv);
                    pn_delivery_settle(pdlv);
                }
            }
        }
        log_link_message(qconn, plink, msg_out);
    }
    return update;
}


static int CORE_link_get_credit(void *context, qdr_link_t *link)
{
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    pn_link_t *plink = !!qlink ? qd_link_pn(qlink) : 0;

    if (!plink)
        return 0;

    return pn_link_remote_credit(plink);
}


static void CORE_delivery_update(void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled)
{
    qd_router_t   *router = (qd_router_t*) context;
    pn_delivery_t *pnd    = qdr_node_delivery_pn_from_qdr(dlv);

    if (!pnd)
        return;

    // If the delivery's link is somehow gone (maybe because of a connection drop, we don't proceed.
    if (!pn_delivery_link(pnd))
        return;

    qdr_link_t      *qlink   = qdr_delivery_link(dlv);
    qd_link_t       *link    = 0;
    qd_connection_t *qd_conn = 0;

    if (qlink) {
        link = (qd_link_t*) qdr_link_get_context(qlink);
        if (link) {
            qd_conn = qd_link_connection(link);
             if (qd_conn == 0)
                return;
        }
        else
            return;
    }
    else
        return;

    if (disp && !pn_delivery_settled(pnd)) {
        uint64_t ignore = 0;
        qd_delivery_state_t *dstate = qdr_delivery_take_local_delivery_state(dlv, &ignore);

        // update if the disposition has changed or there is new state associated with it
        if (disp != pn_delivery_local_state(pnd) || dstate) {
            // handle propagation of delivery state from qdr_delivery_t to proton:
            qd_delivery_write_local_state(pnd, disp, dstate);
            pn_delivery_update(pnd, disp);
            qd_delivery_state_free(dstate);
            if (disp == PN_MODIFIED)  // @TODO(kgiusti) why do we need this???
                pn_disposition_set_failed(pn_delivery_local(pnd), true);
        }
    }

    if (settled) {
        qd_message_t *msg = qdr_delivery_message(dlv);
        if (qd_message_receive_complete(msg)) {
            //
            // If the delivery is settled and the message has fully arrived, disconnect
            // the linkages and settle it in Proton now.
            //
            qdr_node_disconnect_deliveries(router->router_core, link, dlv, pnd);
            pn_delivery_settle(pnd);
        } else {
            if (disp == PN_RELEASED || disp == PN_MODIFIED || disp == PN_REJECTED || qdr_delivery_presettled(dlv)) {
                //
                // If the delivery is settled and it is still arriving, defer the settlement
                // until the content has fully arrived. For now set the disposition on the qdr_delivery
                // We will use this disposition later on to set the disposition on the proton delivery.
                //
                qdr_delivery_set_disposition(dlv, disp);
                //
                // We have set the message to be discarded. We will use this information
                // in AMQP_rx_handler to only update the disposition on the proton delivery if the message is discarded.
                //
                qd_message_set_discard(msg, true);
                //
                // If the disposition is RELEASED or MODIFIED, set the message to discard
                // and if it is blocked by Q2 holdoff, get the link rolling again.
                //
                qd_message_Q2_holdoff_disable(msg);
            }
        }
    }
}


QD_EXPORT void qd_router_setup_late(qd_dispatch_t *qd)
{
    qd->router->tracemask   = qd_tracemask();
    qd->router->router_core = qdr_core(qd, qd->router->router_mode, qd->router->router_area, qd->router->router_id);

    amqp_direct_adaptor = qdr_protocol_adaptor(qd->router->router_core,
                                               "amqp",
                                               (void*) qd->router,
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
                                               CORE_link_get_credit,
                                               CORE_delivery_update,
                                               CORE_close_connection,
                                               CORE_conn_trace);

    qd_router_python_setup(qd->router);
    qd_timer_schedule(qd->router->timer, 1000);
}

void qd_router_free(qd_router_t *router)
{
    if (!router) return;

    //
    // The char* router->router_id and router->router_area are owned by qd->router_id and qd->router_area respectively
    // We will set them to zero here just in case anybody tries to use these fields.
    //
    router->router_id = 0;
    router->router_area = 0;

    qd_container_set_default_node_type(router->qd, 0, 0, QD_DIST_BOTH);

    qdr_core_free(router->router_core);
    qd_tracemask_free(router->tracemask);
    qd_timer_free(router->timer);
    sys_mutex_free(router->lock);
    qd_router_configure_free(router);
    qd_router_python_free(router);

    free(router);
    qd_router_id_finalize();
    free(direct_prefix);
}


const char *qd_router_id(void)
{
    assert(node_id);
    return node_id;
}


const uint8_t *qd_router_id_encoded(size_t *len)
{
    assert(encoded_node_id && encoded_node_id_len);
    *len = encoded_node_id_len;
    return encoded_node_id;
}


qdr_core_t *qd_router_core(qd_dispatch_t *qd)
{
    return qd->router->router_core;
}


// invoked by an I/O thread when enough buffers have been released deactivate
// the Q2 block.  Note that this method will likely be running on a worker
// thread that is not the same thread that "owns" the qd_link_t passed in.
//
void qd_link_q2_restart_receive(qd_alloc_safe_ptr_t context)
{
    qd_link_t *in_link = (qd_link_t*) qd_alloc_deref_safe_ptr(&context);
    if (!in_link)
        return;

    assert(qd_link_direction(in_link) == QD_INCOMING);

    qd_connection_t *in_conn = qd_link_connection(in_link);
    if (in_conn) {
        qd_link_t_sp *safe_ptr = NEW(qd_alloc_safe_ptr_t);
        *safe_ptr = context;  // use original to keep old sequence counter
        qd_connection_invoke_deferred(in_conn, deferred_AMQP_rx_handler, safe_ptr);
    }
}


// Issue a warning POLICY log message with connection and link identities
// prepended to the policy denial text string.
void qd_connection_log_policy_denial(qd_link_t *link, const char *text)
{
    qdr_link_t *rlink = (qdr_link_t*) qd_link_get_context(link);
    uint64_t l_id = 0;
    uint64_t c_id = 0;
    if (rlink) {
        l_id = rlink->identity;
        if (rlink->conn) {
            c_id = rlink->conn->identity;
        }
    }    
    qd_log(qd_policy_log_source(), QD_LOG_WARNING, "[C%"PRIu64"][L%"PRIu64"] %s",
           c_id, l_id, text);
}
