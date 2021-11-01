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

#include "addr_proxy.h"

#include "core_events.h"
#include "core_link_endpoint.h"
#include "router_core_private.h"

#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/iterator.h"
#include "qpid/dispatch/message.h"
#include "qpid/dispatch/parse.h"

#include <inttypes.h>
#include <stdio.h>

//
// This is the Address Proxy component of the Edge Router module.
//
// Address Proxy has the following responsibilities:
//
//   Related to dynamic (topological) addresses:
//
//    1) When an edge connection becomes active, the "_edge" address is properly linked to an
//       outgoing anonymous link on the active edge connection.
//
//    2) When an edge connection becomes active, an incoming link is established over the edge
//       connection that is used to transfer deliveries to topological (dynamic) addresses
//       on the edge router.
//
//  Related to mobile addresses:
//
//    3) Ensure that if there is an active edge connection, that connection should have one incoming
//       link for every mobile address for which there is at least one local consumer.
//
//    4) Ensure that if there is an active edge connection, that connection should have one outgoing
//       link for every mobile address for which there is at least one local producer.
//
//    5) Maintain an incoming link for edge-address-tracking attached to the edge-address-tracker
//       in the connected interior router.
//
//    6) Handle address tracking updates indicating which producer-addresses have destinations
//       reachable via the edge connection.
//

#define INITIAL_CREDIT 32

struct qcm_edge_addr_proxy_t {
    qdr_core_t                *core;
    qdrc_event_subscription_t *event_sub;
    bool                       edge_conn_established;
    qdr_address_t             *edge_conn_addr;
    qdr_connection_t          *edge_conn;
    qdrc_endpoint_t           *tracking_endpoint;
    qdrc_endpoint_desc_t       endpoint_descriptor;
};


static qdr_terminus_t *qdr_terminus_edge_downlink(const char *addr)
{
    qdr_terminus_t *term = qdr_terminus(0);
    qdr_terminus_add_capability(term, QD_CAPABILITY_EDGE_DOWNLINK);
    if (addr)
        qdr_terminus_set_address(term, addr);
    return term;
}


static qdr_terminus_t *qdr_terminus_normal(const char *addr)
{
    qdr_terminus_t *term = qdr_terminus(0);
    if (addr)
        qdr_terminus_set_address(term, addr);
    return term;
}


static void set_fallback_capability(qdr_terminus_t *term)
{
    qdr_terminus_add_capability(term, QD_CAPABILITY_FALLBACK);
}


static void set_waypoint_capability(qdr_terminus_t *term, char phase_char, qd_direction_t dir, int in_phase, int out_phase)
{
    int  phase    = (int) (phase_char - '0');
    bool fallback = phase_char == QD_ITER_HASH_PHASE_FALLBACK;
    char cap[16];
    char suffix[3];

    if (fallback) {
        strncpy(cap, QD_CAPABILITY_FALLBACK, 15);
        qdr_terminus_add_capability(term, cap);
        return;
    }

    //
    // For links that are outgoing on the in_phase or incoming on the out_phase, don't set the
    // waypoint capability.  These links will behave like normal client links.
    //
    if ((dir == QD_OUTGOING && phase == in_phase) ||
        (dir == QD_INCOMING && phase == out_phase))
        return;

    //
    // If the phase is outside the range of in_phase..out_phase, don't do anything.  This is a
    // misconfiguration.
    //
    if (phase < in_phase || phase > out_phase)
        return;

    //
    // In all remaining cases, the new links are acting as waypoints.
    //
    int ordinal = phase + (dir == QD_OUTGOING ? 0 : 1);

    strncpy(cap, QD_CAPABILITY_WAYPOINT_DEFAULT, 15);
    suffix[0] = '.';
    suffix[1] = '0' + ordinal;
    suffix[2] = '\0';
    strcat(cap, suffix);
    qdr_terminus_add_capability(term, cap);
}


static void add_inlink(qcm_edge_addr_proxy_t *ap, const char *key, qdr_address_t *addr)
{
    if (addr->edge_inlink == 0) {
        qdr_terminus_t *term = qdr_terminus_normal(key + 2);
        const char     *key  = (char*) qd_hash_key_by_handle(addr->hash_handle);

        if (key[1] == QD_ITER_HASH_PHASE_FALLBACK) {
            set_fallback_capability(term);

        } else if (addr->config && addr->config->out_phase > 0) {
            //
            // If this address is configured as multi-phase, we may need to
            // add waypoint capabilities to the terminus.
            //
            if (key[0] == QD_ITER_HASH_PREFIX_MOBILE)
                set_waypoint_capability(term, key[1], QD_INCOMING, addr->config->in_phase, addr->config->out_phase);
        }

        qdr_link_t *link = qdr_create_link_CT(ap->core, ap->edge_conn, QD_LINK_ENDPOINT, QD_INCOMING,
                                              term, qdr_terminus_normal(0), QD_SSN_ENDPOINT,
                                              QDR_DEFAULT_PRIORITY);
        qdr_core_bind_address_link_CT(ap->core, addr, link);
        addr->edge_inlink = link;
    }
}


static void del_inlink(qcm_edge_addr_proxy_t *ap, qdr_address_t *addr)
{
    qdr_link_t *link = addr->edge_inlink;
    if (link) {
        addr->edge_inlink = 0;
        qdr_core_unbind_address_link_CT(ap->core, addr, link);
        qdr_link_outbound_detach_CT(ap->core, link, 0, QDR_CONDITION_NONE, true);
    }
}


static void add_outlink(qcm_edge_addr_proxy_t *ap, const char *key, qdr_address_t *addr)
{
    if (addr->edge_outlink == 0 && DEQ_SIZE(addr->subscriptions) == 0) {
        //
        // Note that this link must not be bound to the address at this time.  That will
        // happen later when the interior tells us that there are upstream destinations
        // for the address (see on_transfer below).
        //
        qdr_terminus_t *term = qdr_terminus_normal(key + 2);
        const char     *key  = (char*) qd_hash_key_by_handle(addr->hash_handle);

        if (key[1] == QD_ITER_HASH_PHASE_FALLBACK) {
            set_fallback_capability(term);

        } else if (addr->config && addr->config->out_phase > 0) {
            //
            // If this address is configured as multi-phase, we may need to
            // add waypoint capabilities to the terminus.
            //
            const char *key = (char*) qd_hash_key_by_handle(addr->hash_handle);
            if (key[0] == QD_ITER_HASH_PREFIX_MOBILE)
                set_waypoint_capability(term, key[1], QD_OUTGOING, addr->config->in_phase, addr->config->out_phase);
        }

        qdr_link_t *link = qdr_create_link_CT(ap->core, ap->edge_conn, QD_LINK_ENDPOINT, QD_OUTGOING,
                                              qdr_terminus_normal(0), term, QD_SSN_ENDPOINT,
                                              QDR_DEFAULT_PRIORITY);
        addr->edge_outlink = link;
    }
}


static void del_outlink(qcm_edge_addr_proxy_t *ap, qdr_address_t *addr)
{
    qdr_link_t *link = addr->edge_outlink;
    if (link) {
        addr->edge_outlink = 0;
        qdr_core_unbind_address_link_CT(ap->core, addr, link);
        qdr_link_outbound_detach_CT(ap->core, link, 0, QDR_CONDITION_NONE, true);
    }
}

static void on_link_event(void *context, qdrc_event_t event, qdr_link_t *link)
{
    if (!link || !link->conn)
        return;

    //
    // We only care if the link event is on an edge connection.
    //
    if (link->conn->role != QDR_ROLE_EDGE_CONNECTION)
            return;

    switch (event) {
        case QDRC_EVENT_LINK_OUT_DETACHED: {
            qdr_address_t *addr = link->owning_addr;
            if (addr && link == addr->edge_outlink) {
                //
                // The link is being detached. If the detaching link is the same as the link's owning_addr's edge_outlink,
                // set the edge_outlink on the address to be zero. We do this because this link is going to be freed
                // and we don't want anyone dereferencing the addr->edge_outlink
                //
                addr->edge_outlink = 0;
            }
            break;
        }

        case QDRC_EVENT_LINK_IN_DETACHED: {
            qdr_address_t *addr = link->owning_addr;
            if (addr && link == addr->edge_inlink) {
                //
                // The link is being detached. If the detaching link is the same as the link's owning_addr's edge_inlink,
                // set the edge_inlink on the address to be zero. We do this because this link is going to be freed
                // and we don't want anyone dereferencing the addr->edge_inlink
                //
                addr->edge_inlink = 0;
            }
            break;
        }

        default:
            assert(false);
            break;
    }
}


static void on_conn_event(void *context, qdrc_event_t event, qdr_connection_t *conn)
{
    qcm_edge_addr_proxy_t *ap = (qcm_edge_addr_proxy_t*) context;

    switch (event) {
    case QDRC_EVENT_CONN_EDGE_ESTABLISHED : {
        //
        // Flag the edge connection as being established.
        //
        ap->edge_conn_established = true;
        ap->edge_conn             = conn;

        //
        // Attach an anonymous sending link to the interior router.
        //
        qdr_link_t *out_link = qdr_create_link_CT(ap->core, conn,
                                                  QD_LINK_ENDPOINT, QD_OUTGOING,
                                                  qdr_terminus(0), qdr_terminus(0),
                                                  QD_SSN_ENDPOINT,
                                                  QDR_DEFAULT_PRIORITY);

        //
        // Associate the anonymous sender with the edge connection address.  This will cause
        // all deliveries destined off-edge to be sent to the interior via the edge connection.
        //
        qdr_core_bind_address_link_CT(ap->core, ap->edge_conn_addr, out_link);

        //
        // Attach a receiving link for edge summary.  This will cause all deliveries
        // destined for this router to be delivered via the edge connection.
        //
        (void) qdr_create_link_CT(ap->core, conn,
                                  QD_LINK_ENDPOINT, QD_INCOMING,
                                  qdr_terminus_edge_downlink(ap->core->router_id),
                                  qdr_terminus_edge_downlink(0),
                                  QD_SSN_ENDPOINT, QDR_DEFAULT_PRIORITY);

        //
        // Attach a receiving link for edge address tracking updates.
        //
        ap->tracking_endpoint =
            qdrc_endpoint_create_link_CT(ap->core, conn, QD_INCOMING,
                                         qdr_terminus_normal(QD_TERMINUS_EDGE_ADDRESS_TRACKING),
                                         qdr_terminus(0), &ap->endpoint_descriptor, ap);

        //
        // Process eligible local destinations
        //
        qdr_address_t *addr = DEQ_HEAD(ap->core->addrs);
        while (addr) {
            const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
            if (*key == QD_ITER_HASH_PREFIX_MOBILE) {
                //
                // If the address has more than zero attached destinations, create an
                // incoming link from the interior to signal the presence of local consumers.
                //
                if (DEQ_SIZE(addr->rlinks) > 0) {
                    if (DEQ_SIZE(addr->rlinks) == 1) {
                        //
                        // If there's only one link and it's on the edge connection, ignore the address.
                        //
                        qdr_link_ref_t *ref = DEQ_HEAD(addr->rlinks);
                        if (ref->link->conn != ap->edge_conn)
                            add_inlink(ap, key, addr);
                    } else
                        add_inlink(ap, key, addr);
                }

                //
                // If the address has more than zero attached sources, create an outgoing link
                // to the interior to signal the presence of local producers.
                //
                bool add = false;
                if (DEQ_SIZE(addr->inlinks) > 0) {
                    if (DEQ_SIZE(addr->inlinks) == 1) {
                        //
                        // If there's only one link and it's on the edge connection, ignore the address.
                        //
                        qdr_link_ref_t *ref = DEQ_HEAD(addr->inlinks);
                        if (ref->link->conn != ap->edge_conn)
                            add = true;
                    } else
                        add = true;

                    if (add) {
                        add_outlink(ap, key, addr);

                        //
                        // If the address has a fallback address, add an outlink for that as well
                        //
                        if (!!addr->fallback)
                            add_outlink(ap, key, addr->fallback);
                    }
                }
            }
            addr = DEQ_NEXT(addr);
        }
        break;
    }

    case QDRC_EVENT_CONN_EDGE_LOST :
        ap->edge_conn_established = false;
        ap->edge_conn             = 0;
        break;

    default:
        assert(false);
        break;
    }
}


static void on_addr_event(void *context, qdrc_event_t event, qdr_address_t *addr)
{
    qcm_edge_addr_proxy_t *ap = (qcm_edge_addr_proxy_t*) context;
    qdr_link_ref_t        *link_ref;

    //
    // If we don't have an established edge connection, there is no further work to be done.
    //
    if (!ap->edge_conn_established)
        return;

    //
    // If the address is not in the Mobile class, no further processing is needed.
    //
    const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
    if (*key != QD_ITER_HASH_PREFIX_MOBILE)
        return;

    switch (event) {
    case QDRC_EVENT_ADDR_BECAME_LOCAL_DEST :
        //
        // Add an edge connection for this address only if the local destination is
        // not the link to the interior.
        //
        link_ref = DEQ_HEAD(addr->rlinks);
        if (link_ref->link->conn != ap->edge_conn)
            add_inlink(ap, key, addr);
        break;

    case QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST :
        del_inlink(ap, addr);
        break;

    case QDRC_EVENT_ADDR_ONE_LOCAL_DEST :
        //
        // If the remaining local destination is the link to the interior,
        // remove the inlink for this address.
        //
        link_ref = DEQ_HEAD(addr->rlinks);
        if (link_ref->link->conn == ap->edge_conn)
            del_inlink(ap, addr);
        break;

    case QDRC_EVENT_ADDR_TWO_DEST :
        add_inlink(ap, key, addr);
        break;

    case QDRC_EVENT_ADDR_BECAME_SOURCE :
        link_ref = DEQ_HEAD(addr->inlinks);
        if (!link_ref || link_ref->link->conn != ap->edge_conn)
            add_outlink(ap, key, addr);
        break;

    case QDRC_EVENT_ADDR_NO_LONGER_SOURCE :
        del_outlink(ap, addr);
        break;

    case QDRC_EVENT_ADDR_TWO_SOURCE :
        add_outlink(ap, key, addr);
        break;

    case QDRC_EVENT_ADDR_ONE_SOURCE :
        link_ref = DEQ_HEAD(addr->inlinks);
        if (!link_ref || link_ref->link->conn == ap->edge_conn)
            del_outlink(ap, addr);
        break;

    default:
        assert(false);
        break;
    }
}


static void on_second_attach(void           *link_context,
                             qdr_terminus_t *remote_source,
                             qdr_terminus_t *remote_target)
{
    qcm_edge_addr_proxy_t *ap = (qcm_edge_addr_proxy_t*) link_context;

    qdrc_endpoint_flow_CT(ap->core, ap->tracking_endpoint, INITIAL_CREDIT, false);

    qdr_terminus_free(remote_source);
    qdr_terminus_free(remote_target);
}


static void on_transfer(void           *link_context,
                        qdr_delivery_t *dlv,
                        qd_message_t   *msg)
{
    qcm_edge_addr_proxy_t *ap = (qcm_edge_addr_proxy_t*) link_context;
    uint64_t dispo = PN_ACCEPTED;

    //
    // Validate the message
    //
    if (qd_message_check_depth(msg, QD_DEPTH_BODY) == QD_MESSAGE_DEPTH_OK) {
        //
        // Get the message body.  It must be a list with two elements.  The first is an address
        // and the second is a boolean indicating whether that address has upstream destinations.
        //
        qd_iterator_t     *iter = qd_message_field_iterator(msg, QD_FIELD_BODY);
        qd_parsed_field_t *body = qd_parse(iter);
        if (!!body && qd_parse_is_list(body) && qd_parse_sub_count(body) == 2) {
            qd_parsed_field_t *addr_field = qd_parse_sub_value(body, 0);
            qd_parsed_field_t *dest_field = qd_parse_sub_value(body, 1);

            if (qd_parse_is_scalar(addr_field) && qd_parse_is_scalar(dest_field)) {
                qd_iterator_t *addr_iter = qd_parse_raw(addr_field);
                bool           dest      = qd_parse_as_bool(dest_field);
                qdr_address_t *addr;

                qd_iterator_reset_view(addr_iter, ITER_VIEW_ALL);
                qd_hash_retrieve(ap->core->addr_hash, addr_iter, (void**) &addr);
                if (addr) {
                    qdr_link_t *link = addr->edge_outlink;
                    if (link) {
                        if (dest)
                            qdr_core_bind_address_link_CT(ap->core, addr, link);
                        else
                            qdr_core_unbind_address_link_CT(ap->core, addr, link);
                    }
                }
            }
        }

        qd_parse_free(body);
        qd_iterator_free(iter);
    } else {
        qd_log(ap->core->log, QD_LOG_ERROR,
               "Edge Address Proxy: received an invalid message body, rejecting");
        dispo = PN_REJECTED;
    }

    qdrc_endpoint_settle_CT(ap->core, dlv, dispo);

    //
    // Replenish the credit for this delivery
    //
    qdrc_endpoint_flow_CT(ap->core, ap->tracking_endpoint, 1, false);
}

qdr_address_t *qcm_edge_conn_addr(void *link_context)
{
    qcm_edge_addr_proxy_t *ap = (qcm_edge_addr_proxy_t*) link_context;
    if (!ap)
        return 0;
    return ap->edge_conn_addr;
}


static void on_cleanup(void *link_context)
{
    qcm_edge_addr_proxy_t *ap = (qcm_edge_addr_proxy_t*) link_context;

    ap->tracking_endpoint = 0;
}


qcm_edge_addr_proxy_t *qcm_edge_addr_proxy(qdr_core_t *core)
{
    qcm_edge_addr_proxy_t *ap = NEW(qcm_edge_addr_proxy_t);

    ZERO(ap);
    ap->core = core;

    ap->endpoint_descriptor.label            = "Edge Address Proxy";
    ap->endpoint_descriptor.on_second_attach = on_second_attach;
    ap->endpoint_descriptor.on_transfer      = on_transfer;
    ap->endpoint_descriptor.on_cleanup       = on_cleanup;

    //
    // Establish the edge connection address to represent destinations reachable via the edge connection
    //
    ap->edge_conn_addr = qdr_add_local_address_CT(core, 'L', "_edge", QD_TREATMENT_ANYCAST_CLOSEST);

    //
    // Subscribe to the core events we'll need to drive this component
    //
    ap->event_sub = qdrc_event_subscribe_CT(core,
                                            QDRC_EVENT_CONN_EDGE_ESTABLISHED
                                            | QDRC_EVENT_CONN_EDGE_LOST
                                            | QDRC_EVENT_ADDR_BECAME_LOCAL_DEST
                                            | QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST
                                            | QDRC_EVENT_ADDR_ONE_LOCAL_DEST
                                            | QDRC_EVENT_ADDR_TWO_DEST
                                            | QDRC_EVENT_ADDR_BECAME_SOURCE
                                            | QDRC_EVENT_ADDR_NO_LONGER_SOURCE
                                            | QDRC_EVENT_ADDR_TWO_SOURCE
                                            | QDRC_EVENT_ADDR_ONE_SOURCE
                                            | QDRC_EVENT_LINK_IN_DETACHED
                                            | QDRC_EVENT_LINK_OUT_DETACHED,
                                            on_conn_event,
                                            on_link_event,
                                            on_addr_event,
                                            0,
                                            ap);                                            

    core->edge_conn_addr = qcm_edge_conn_addr;
    core->edge_context = ap;

    return ap;
}



void qcm_edge_addr_proxy_final(qcm_edge_addr_proxy_t *ap)
{
    qdrc_event_unsubscribe_CT(ap->core, ap->event_sub);
    free(ap);
}

