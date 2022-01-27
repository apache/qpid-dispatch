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

#include "agent_router.h"

#include "config.h"

#include <inttypes.h>


#define QDR_ROUTER_NAME                                0
#define QDR_ROUTER_IDENTITY                            1
#define QDR_ROUTER_ID                                  2
#define QDR_ROUTER_TYPE                                3
#define QDR_ROUTER_MODE                                4
#define QDR_ROUTER_AREA                                5
#define QDR_ROUTER_VERSION                             6
#define QDR_ROUTER_METADATA                            7
#define QDR_ROUTER_ADDR_COUNT                          8
#define QDR_ROUTER_LINK_COUNT                          9
#define QDR_ROUTER_NODE_COUNT                          10
#define QDR_ROUTER_LINK_ROUTE_COUNT                    11
#define QDR_ROUTER_AUTO_LINK_COUNT                     12
#define QDR_ROUTER_CONNECTION_COUNT                    13
#define QDR_ROUTER_PRESETTLED_DELIVERIES               14
#define QDR_ROUTER_DROPPED_PRESETTLED_DELIVERIES       15
#define QDR_ROUTER_ACCEPTED_DELIVERIES                 16
#define QDR_ROUTER_REJECTED_DELIVERIES                 17
#define QDR_ROUTER_RELEASED_DELIVERIES                 18
#define QDR_ROUTER_MODIFIED_DELIVERIES                 19
#define QDR_ROUTER_DELAYED_1SEC                        20
#define QDR_ROUTER_DELAYED_10SEC                       21
#define QDR_ROUTER_DELIVERIES_STUCK                    22
#define QDR_ROUTER_DELIVERIES_INGRESS                  23
#define QDR_ROUTER_DELIVERIES_EGRESS                   24
#define QDR_ROUTER_DELIVERIES_TRANSIT                  25
#define QDR_ROUTER_DELIVERIES_INGRESS_ROUTE_CONTAINER  26
#define QDR_ROUTER_DELIVERIES_EGRESS_ROUTE_CONTAINER   27
#define QDR_ROUTER_DELIVERIES_REDIRECTED               28
#define QDR_ROUTER_LINKS_BLOCKED                       29
#define QDR_ROUTER_UPTIME_SECONDS                      30
#define QDR_ROUTER_MEMORY_USAGE                        31
#define QDR_ROUTER_WORKER_THREADS                      32

const char *qdr_router_columns[] =
    {"name",
     "identity",
     "id",
     "type",
     "mode",
     "area",
     "version",
     "metadata",
     "addrCount",
     "linkCount",
     "nodeCount",
     "linkRouteCount",
     "autoLinkCount",
     "connectionCount",
     "presettledDeliveries",
     "droppedPresettledDeliveries",
     "acceptedDeliveries",
     "rejectedDeliveries",
     "releasedDeliveries",
     "modifiedDeliveries",
     "deliveriesDelayed1Sec",
     "deliveriesDelayed10Sec",
     "deliveriesStuck",
     "deliveriesIngress",
     "deliveriesEgress",
     "deliveriesTransit",
     "deliveriesIngressRouteContainer",
     "deliveriesEgressRouteContainer",
     "deliveriesRedirectedToFallback",
     "linksBlocked",
     "uptimeSeconds",
     "memoryUsage",
     "workerThreads",
     0};


static const char *qd_router_mode_names[] = {
    "standalone",
    "interior",
    "edge",
    "endpoint"
};

static const char *router_mode(qd_router_mode_t router_mode)
{
    return qd_router_mode_names[(int)router_mode];

}

static void qdr_agent_write_column_CT(qd_composed_field_t *body, int col, qdr_core_t *core)
{
    switch(col) {
    case QDR_ROUTER_IDENTITY:
        // There is only one instance of router. Just give it an identity of 1
        qd_compose_insert_string(body, "1");
        break;
    case QDR_ROUTER_TYPE:
        qd_compose_insert_string(body, "org.apache.qpid.dispatch.router");
        break;

    case QDR_ROUTER_MODE:
        qd_compose_insert_string(body, router_mode(core->router_mode));
        break;

    case QDR_ROUTER_AREA:
        if (core->router_area)
            qd_compose_insert_string(body, core->router_area);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_ROUTER_VERSION:
        qd_compose_insert_string(body, QPID_DISPATCH_VERSION);
        break;

    case QDR_ROUTER_METADATA:
        if (core->qd->metadata)
            qd_compose_insert_string(body, core->qd->metadata);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_ROUTER_ADDR_COUNT:
        qd_compose_insert_ulong(body, DEQ_SIZE(core->addrs));
        break;

    case QDR_ROUTER_LINK_COUNT:
        qd_compose_insert_ulong(body, DEQ_SIZE(core->open_links));
        break;

    case QDR_ROUTER_NODE_COUNT:
        qd_compose_insert_ulong(body, DEQ_SIZE(core->routers));
        break;

    case QDR_ROUTER_CONNECTION_COUNT:
        qd_compose_insert_ulong(body, DEQ_SIZE(core->open_connections));
        break;

    case QDR_ROUTER_LINK_ROUTE_COUNT:
        qd_compose_insert_ulong(body, DEQ_SIZE(core->link_routes));
        break;

    case QDR_ROUTER_AUTO_LINK_COUNT:
        qd_compose_insert_ulong(body, DEQ_SIZE(core->auto_links));
        break;

    case QDR_ROUTER_ID:
    case QDR_ROUTER_NAME:
        if (core->router_id)
            qd_compose_insert_string(body, core->router_id);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_ROUTER_PRESETTLED_DELIVERIES:
        qd_compose_insert_ulong(body, core->presettled_deliveries);
        break;

    case QDR_ROUTER_DROPPED_PRESETTLED_DELIVERIES:
        qd_compose_insert_ulong(body, core->dropped_presettled_deliveries);
        break;

    case QDR_ROUTER_ACCEPTED_DELIVERIES:
        qd_compose_insert_ulong(body, core->accepted_deliveries);
        break;

    case QDR_ROUTER_REJECTED_DELIVERIES:
        qd_compose_insert_ulong(body, core->rejected_deliveries);
        break;

    case QDR_ROUTER_RELEASED_DELIVERIES:
        qd_compose_insert_ulong(body, core->released_deliveries);
        break;

    case QDR_ROUTER_MODIFIED_DELIVERIES:
        qd_compose_insert_ulong(body, core->modified_deliveries);
        break;

    case QDR_ROUTER_DELAYED_1SEC:
        qd_compose_insert_ulong(body, core->deliveries_delayed_1sec);
        break;

    case QDR_ROUTER_DELAYED_10SEC:
        qd_compose_insert_ulong(body, core->deliveries_delayed_10sec);
        break;

    case QDR_ROUTER_DELIVERIES_STUCK:
        qd_compose_insert_ulong(body, core->deliveries_stuck);
        break;

    case QDR_ROUTER_DELIVERIES_INGRESS:
        qd_compose_insert_ulong(body, core->deliveries_ingress);
        break;

    case QDR_ROUTER_DELIVERIES_EGRESS:
        qd_compose_insert_ulong(body, core->deliveries_egress);
        break;

    case QDR_ROUTER_DELIVERIES_TRANSIT:
        qd_compose_insert_ulong(body, core->deliveries_transit);
        break;

    case QDR_ROUTER_DELIVERIES_INGRESS_ROUTE_CONTAINER:
        qd_compose_insert_ulong(body, core->deliveries_ingress_route_container);
        break;

    case QDR_ROUTER_DELIVERIES_EGRESS_ROUTE_CONTAINER:
        qd_compose_insert_ulong(body, core->deliveries_egress_route_container);
        break;

    case QDR_ROUTER_DELIVERIES_REDIRECTED:
        qd_compose_insert_ulong(body, core->deliveries_redirected);
        break;

    case QDR_ROUTER_LINKS_BLOCKED:
        qd_compose_insert_uint(body, core->links_blocked);
        break;

    case QDR_ROUTER_WORKER_THREADS:
        qd_compose_insert_int(body, core->worker_thread_count);
        break;

    case QDR_ROUTER_UPTIME_SECONDS:
        qd_compose_insert_uint(body, qdr_core_uptime_ticks(core));
        break;

    case QDR_ROUTER_MEMORY_USAGE: {
        uint64_t size = qd_router_memory_usage();
        if (size)
            qd_compose_insert_ulong(body, size);
        else  // memory usage not available
            qd_compose_insert_null(body);
    } break;

    default:
        qd_compose_insert_null(body);
        break;
    }
}



static void qdr_agent_write_router_CT(qdr_query_t *query,  qdr_core_t *core)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    while (query->columns[i] >= 0) {
        qdr_agent_write_column_CT(body, query->columns[i], core);
        i++;
    }
    qd_compose_end_list(body);
}

void qdra_router_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    if (offset >= 1) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Write the columns of core into the response body.
    //
    qdr_agent_write_router_CT(query, core);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

// Nothing to do here. The router has only one entry.
void qdra_router_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{

}
