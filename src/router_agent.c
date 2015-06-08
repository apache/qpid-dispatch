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
#include "router_private.h"
#include "entity_cache.h"


const char *QD_ROUTER_TYPE = "router";

static const char *qd_router_mode_names[] = {
    "standalone",
    "interior",
    "edge",
    "endpoint"
};
ENUM_DEFINE(qd_router_mode, qd_router_mode_names);

qd_error_t qd_entity_refresh_router(qd_entity_t* entity, void *impl) {
    qd_dispatch_t *qd = (qd_dispatch_t*) impl;
    qd_router_t *router = qd->router;
    if (qd_entity_set_string(entity, "area", router->router_area) == 0 &&
        qd_entity_set_string(entity, "mode", qd_router_mode_name(router->router_mode)) == 0 &&
        qd_entity_set_long(entity, "addrCount", DEQ_SIZE(router->addrs)) == 0 &&
        qd_entity_set_long(entity, "linkCount", DEQ_SIZE(router->links)) == 0 &&
        qd_entity_set_long(entity, "nodeCount", DEQ_SIZE(router->routers)) == 0
    )
        return QD_ERROR_NONE;
    return qd_error_code();
}

static const char *address_key(qd_address_t *addr) {
    return addr && addr->hash_handle ? (const char*) qd_hash_key_by_handle(addr->hash_handle) : NULL;
}

qd_error_t qd_entity_refresh_router_address(qd_entity_t* entity, void *impl) {
    qd_address_t *addr     = (qd_address_t*) impl;
    uint32_t      subCount = DEQ_SIZE(addr->rlinks);
    if (DEQ_SIZE(addr->lrps) > 0)
        subCount = DEQ_SIZE(addr->lrps);
    if (qd_entity_set_bool(entity, "inProcess", addr->on_message != 0) == 0 &&
        qd_entity_set_long(entity, "subscriberCount", subCount) == 0 &&
        qd_entity_set_long(entity, "remoteCount", DEQ_SIZE(addr->rnodes)) == 0 &&
        qd_entity_set_long(entity, "deliveriesIngress", addr->deliveries_ingress) == 0 &&
        qd_entity_set_long(entity, "deliveriesEgress", addr->deliveries_egress) == 0 &&
        qd_entity_set_long(entity, "deliveriesTransit", addr->deliveries_transit) == 0 &&
        qd_entity_set_long(entity, "deliveriesToContainer", addr->deliveries_to_container) == 0 &&
        qd_entity_set_long(entity, "deliveriesFromContainer", addr->deliveries_from_container) == 0 &&
        qd_entity_set_string(entity, "key", address_key(addr))
    )
        return QD_ERROR_NONE;
    return qd_error_code();
}

static const char *qd_link_type_names[] = { "endpoint", "waypoint", "inter-router", "inter-area" };
ENUM_DEFINE(qd_link_type, qd_link_type_names);

static const char* qd_router_link_remote_container(qd_router_link_t* link) {
    if (!link->link || !qd_link_pn(link->link))
        return "";
    return pn_connection_remote_container(
        pn_session_connection(qd_link_pn_session(link->link)));
}

static const char* qd_router_link_name(qd_router_link_t* link) {
    if (!link->link || !qd_link_pn(link->link))
        return "";
    return pn_link_name(qd_link_pn(link->link));
}

qd_error_t qd_entity_refresh_router_link(qd_entity_t* entity, void *impl)
{
    qd_router_link_t *link = (qd_router_link_t*) impl;
    if (!qd_entity_set_string(entity, "linkType", qd_link_type_name(link->link_type)) &&
        !qd_entity_set_string(entity, "linkDir", (link->link_direction == QD_INCOMING) ? "in": "out") &&
        !qd_entity_set_string(entity, "linkName", qd_router_link_name(link)) &&
        !qd_entity_set_string(entity, "owningAddr", address_key(link->owning_addr)) &&
        !qd_entity_set_long(entity, "eventFifoDepth", DEQ_SIZE(link->event_fifo)) &&
        !qd_entity_set_long(entity, "msgFifoDepth", DEQ_SIZE(link->msg_fifo)) &&
        !qd_entity_set_string(entity, "remoteContainer", qd_router_link_remote_container(link))
    )
        return QD_ERROR_NONE;
    return qd_error_code();
}

void qd_router_build_node_list(qd_dispatch_t *qd, qd_composed_field_t *field)
{
    qd_router_t *router = qd->router;
    char         temp[1000];

    sys_mutex_lock(router->lock);
    qd_router_node_t *rnode = DEQ_HEAD(router->routers);
    while (rnode) {
        const unsigned char* addr = qd_hash_key_by_handle(rnode->owning_addr->hash_handle);
        snprintf(temp, sizeof(temp), "amqp:/_topo/%s/%s/$management",
                router->router_area, &((char*) addr)[1]);
        qd_compose_insert_string(field, temp);
        rnode = DEQ_NEXT(rnode);
    }
    sys_mutex_unlock(router->lock);
}
