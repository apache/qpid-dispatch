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
#include <stdio.h>

#define QDR_ROUTER_NAME                   0
#define QDR_ROUTER_IDENTITY               1
#define QDR_ROUTER_ID                     2
#define QDR_ROUTER_TYPE                   3
#define QDR_ROUTER_MODE                   4
#define QDR_ROUTER_AREA                   5
#define QDR_ROUTER_VERSION                6
#define QDR_ROUTER_HELLO_INTERVAL         7
#define QDR_ROUTER_HELLO_MAX_AGE          8
#define QDR_ROUTER_RA_INTERVAL            9
#define QDR_ROUTER_RA_INTERVAL_FLUX       10
#define QDR_ROUTER_REMOTE_LS_MAX_AGE      11
#define QDR_ROUTER_ADDR_COUNT             12
#define QDR_ROUTER_LINK_COUNT             13
#define QDR_ROUTER_NODE_COUNT             14
#define QDR_ROUTER_LINK_ROUTE_COUNT       15
#define QDR_ROUTER_AUTO_LINK_COUNT        16
#define QDR_ROUTER_WORKER_THREADS         17
#define QDR_ROUTER_DEBUG_DUMP             18
#define QDR_ROUTER_SASL_CONFIG_PATH       19
#define QDR_ROUTER_SASL_CONFIG_NAME       20
#define QDR_ROUTER_ROUTER_ID              21
#define QDR_ROUTER_MOBILE_ADDR_MAX_AGE    22
#define QDR_ROUTER_CONNECTION_COUNT       23

const char *qdr_router_columns[] =
    {"name",
     "identity",
     "id",
     "type",
     "mode",
     "area",
     "version",
     "helloInterval",
     "helloMaxAge",
     "raInterval",
     "raIntervalFlux",
     "remoteLsMaxAge",
     "addrCount",
     "linkCount",
     "nodeCount",
     "linkRouteCount",
     "autoLinkCount", // The connection id of the owner connection
     "workerThreads",
     "debugDump",
     "saslConfigPath",
     "saslConfigName",
     "routerId",
     "mobileAddrMaxAge",
     "connectionCount",
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

    case QDR_ROUTER_HELLO_INTERVAL:
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

    case QDR_ROUTER_ROUTER_ID:
    case QDR_ROUTER_ID:
    case QDR_ROUTER_NAME:
        if (core->router_id)
            qd_compose_insert_string(body, core->router_id);
        else
            qd_compose_insert_null(body);
        break;

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
