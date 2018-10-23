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
#include "qpid/dispatch/message.h"
#include "qpid/dispatch/iterator.h"
#include "qpid/dispatch/parse.h"
#include <stdio.h>
#include <inttypes.h>

//
// This is the Address Proxy component of the Edge Router module.
//
// Address Proxy has the following responsibilities:
//
//   Related to dynamic (topological) addresses:
//
//    1) When an uplink becomes active, the "_uplink" address is properly linked to an
//       outgoing anonymous link on the active uplink connection.
//
//    2) When an uplink becomes active, an incoming link is established over the uplink
//       connection that is used to transfer deliveries to topological (dynamic) addresses
//       on the edge router.
//
//  Related to mobile addresses:
//
//    3) Ensure that if there is an active uplink, that uplink should have one incoming
//       link for every mobile address for which there is at least one local consumer.
//
//    4) Ensure that if there is an active uplink, that uplink should have one outgoing
//       link for every mobile address for which there is at least one local producer.
//
//    5) Maintain an incoming link for edge-address-tracking attached to the edge-address-tracker
//       in the connected interior router.
//
//    6) Handle address tracking updates indicating which producer-addresses have destinations
//       reachable via the edge uplink.
//

#define INITIAL_CREDIT 32

struct qcm_edge_addr_proxy_t {
    qdr_core_t                *core;
    qdrc_event_subscription_t *event_sub;
    bool                       uplink_established;
    qdr_address_t             *uplink_addr;
    qdr_connection_t          *uplink_conn;
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


static void add_inlink(qcm_edge_addr_proxy_t *ap, const char *key, qdr_address_t *addr)
{
    if (addr->edge_inlink == 0) {
        qdr_link_t *link = qdr_create_link_CT(ap->core, ap->uplink_conn, QD_LINK_ENDPOINT, QD_INCOMING,
                                              qdr_terminus_normal(key + 2), qdr_terminus_normal(0));
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
    //
    // Note that this link must not be bound to the address at this time.  That will
    // happen later when the interior tells us that there are upstream destinations
    // for the address (see on_transfer below).
    //
    qdr_link_t *link = qdr_create_link_CT(ap->core, ap->uplink_conn, QD_LINK_ENDPOINT, QD_OUTGOING,
                                          qdr_terminus_normal(0), qdr_terminus_normal(key + 2));
    addr->edge_outlink = link;
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


static void on_conn_event(void *context, qdrc_event_t event, qdr_connection_t *conn)
{
    qcm_edge_addr_proxy_t *ap = (qcm_edge_addr_proxy_t*) context;

    switch (event) {
    case QDRC_EVENT_CONN_EDGE_ESTABLISHED : {
        //
        // Flag the uplink as being established.
        //
        ap->uplink_established = true;
        ap->uplink_conn        = conn;

        //
        // Attach an anonymous sending link to the interior router.
        //
        qdr_link_t *out_link = qdr_create_link_CT(ap->core, conn,
                                                  QD_LINK_ENDPOINT, QD_OUTGOING,
                                                  qdr_terminus(0), qdr_terminus(0));

        //
        // Associate the anonymous sender with the uplink address.  This will cause
        // all deliveries destined off-edge to be sent to the interior via the uplink.
        //
        qdr_core_bind_address_link_CT(ap->core, ap->uplink_addr, out_link);

        //
        // Attach a receiving link for edge summary.  This will cause all deliveries
        // destined for this router to be delivered via the uplink.
        //
        (void) qdr_create_link_CT(ap->core, conn,
                                  QD_LINK_ENDPOINT, QD_INCOMING,
                                  qdr_terminus_edge_downlink(ap->core->router_id),
                                  qdr_terminus_edge_downlink(0));

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
                // Nullify the edge link references in case there are any left over from an earlier
                // instance of an edge uplink.
                //
                addr->edge_inlink  = 0;
                addr->edge_outlink = 0;

                //
                // If the address has more than zero attached destinations, create an
                // incoming link from the interior to signal the presence of local consumers.
                //
                if (DEQ_SIZE(addr->rlinks) > 0) {
                    if (DEQ_SIZE(addr->rlinks) == 1) {
                        qdr_link_ref_t *ref = DEQ_HEAD(addr->rlinks);
                        if (ref->link->conn != ap->uplink_conn)
                            add_inlink(ap, key, addr);
                    } else
                        add_inlink(ap, key, addr);
                }

                //
                // If the address has more than zero attached sources, create an outgoing link
                // to the interior to signal the presence of local producers.
                //
                if (DEQ_SIZE(addr->inlinks) > 0)
                    add_outlink(ap, key, addr);
            }
            addr = DEQ_NEXT(addr);
        }
        break;
    }

    case QDRC_EVENT_CONN_EDGE_LOST :
        ap->uplink_established = false;
        ap->uplink_conn        = 0;
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
    // If we don't have an established uplink, there is no further work to be done.
    //
    if (!ap->uplink_established)
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
        // Add an uplink for this address only if the local destination is
        // not the link to the interior.
        //
        link_ref = DEQ_HEAD(addr->rlinks);
        if (link_ref->link->conn != ap->uplink_conn)
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
        if (link_ref->link->conn == ap->uplink_conn)
            del_inlink(ap, addr);
        break;

    case QDRC_EVENT_ADDR_TWO_DEST :
        add_inlink(ap, key, addr);
        break;

    case QDRC_EVENT_ADDR_BECAME_SOURCE :
        add_outlink(ap, key, addr);
        break;

    case QDRC_EVENT_ADDR_NO_LONGER_SOURCE :
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
}


static void on_transfer(void           *link_context,
                        qdr_delivery_t *dlv,
                        qd_message_t   *msg)
{
    qcm_edge_addr_proxy_t *ap = (qcm_edge_addr_proxy_t*) link_context;

    //
    // Validate the message
    //
    if (qd_message_check(msg, QD_DEPTH_BODY)) {
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
    }

    //
    // Replenish the credit for this delivery
    //
    qdrc_endpoint_flow_CT(ap->core, ap->tracking_endpoint, 1, false);
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
    // Establish the uplink address to represent destinations reachable via the edge uplink
    //
    ap->uplink_addr = qdr_add_local_address_CT(core, 'L', "_uplink", QD_TREATMENT_ANYCAST_CLOSEST);

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
                                            | QDRC_EVENT_ADDR_NO_LONGER_SOURCE,
                                            on_conn_event,
                                            0,
                                            on_addr_event,
                                            ap);

    return ap;
}


void qcm_edge_addr_proxy_final(qcm_edge_addr_proxy_t *ap)
{
    qdrc_event_unsubscribe_CT(ap->core, ap->event_sub);
    free(ap);
}

