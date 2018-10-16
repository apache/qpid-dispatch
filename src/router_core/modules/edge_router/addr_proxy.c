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
#include "router_core_private.h"
#include <stdio.h>
#include <inttypes.h>

//
// This is the Address Proxy component of the Edge Router module.
//
// Address Proxy has three main responsibilities:
//  1) When an uplink becomes active, the "_uplink" address is properly linked to an
//     outgoing anonymous link on the active uplink connection.
//  2) When an uplink becomes active, an incoming link is established over the uplink
//     connection that is used to transfer deliveries to topological (dynamic) addresses
//     on the edge router.
//  3) Ensure that if there is an active uplink, that uplink should have one incoming
//     link for every address for which there is at least one local consumer.
//

struct qcm_edge_addr_proxy_t {
    qdr_core_t                *core;
    qdrc_event_subscription_t *event_sub;
    bool                       uplink_established;
    qdr_address_t             *uplink_addr;
    qdr_connection_t          *uplink_conn;
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
        // Process eligible local destinations
        //
        qdr_address_t *addr = DEQ_HEAD(ap->core->addrs);
        while (addr) {
            const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
            if (*key == QD_ITER_HASH_PREFIX_MOBILE && DEQ_SIZE(addr->rlinks) > 0) {
                qdr_link_t *addr_link =
                    qdr_create_link_CT(ap->core, ap->uplink_conn, QD_LINK_ENDPOINT, QD_INCOMING,
                                       qdr_terminus_normal(key + 2), qdr_terminus_normal(0));
                qdr_core_bind_address_link_CT(ap->core, addr, addr_link);
                addr->edge_inlink = addr_link;
            }
            addr = DEQ_NEXT(addr);
        }

        //
        // Raise an event for the establishment of the edge out-link
        //
        qdrc_event_link_raise(ap->core, QDRC_EVENT_LINK_EDGE_OUTLINK, out_link);

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
    qdr_link_t            *link;

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
        link = qdr_create_link_CT(ap->core, ap->uplink_conn, QD_LINK_ENDPOINT, QD_INCOMING,
                                  qdr_terminus_normal(key + 2), qdr_terminus_normal(0));
        qdr_core_bind_address_link_CT(ap->core, addr, link);
        addr->edge_inlink = link;

        break;

    case QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST :
        link = addr->edge_inlink;
        qdr_core_unbind_address_link_CT(ap->core, addr, link);
        qdr_link_outbound_detach_CT(ap->core, link, 0, QDR_CONDITION_NONE, true);

        break;

    default:
        assert(false);
        break;
    }
}


qcm_edge_addr_proxy_t *qcm_edge_addr_proxy(qdr_core_t *core)
{
    qcm_edge_addr_proxy_t *ap = NEW(qcm_edge_addr_proxy_t);

    ap->core = core;
    ap->uplink_established = false;

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
                                            | QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST,
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

