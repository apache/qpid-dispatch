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

static const char *address_hash(qd_address_t *addr) {
    return addr ? (const char*) qd_hash_key_by_handle(addr->hash_handle) : 0;
}

static const char *address_router_id(qd_address_t *addr) {
    const char* hash = address_hash(addr);
    return hash && hash[0] == 'R' ? hash+1 : "";
}

qd_error_t qd_entity_refresh_router_address(qd_entity_t* entity, void *impl) {
    qd_address_t *addr = (qd_address_t*) impl;
    if (qd_entity_set_bool(entity, "inProcess", addr->handler != 0) == 0 &&
        qd_entity_set_long(entity, "subscriberCount", DEQ_SIZE(addr->rlinks)) == 0 &&
        qd_entity_set_long(entity, "remoteCount", DEQ_SIZE(addr->rnodes)) == 0 &&
        qd_entity_set_long(entity, "deliveriesIngress", addr->deliveries_ingress) == 0 &&
        qd_entity_set_long(entity, "deliveriesEgress", addr->deliveries_egress) == 0 &&
        qd_entity_set_long(entity, "deliveriesTransit", addr->deliveries_transit) == 0 &&
        qd_entity_set_long(entity, "deliveriesToContainer", addr->deliveries_to_container) == 0 &&
        qd_entity_set_long(entity, "deliveriesFromContainer", addr->deliveries_from_container) == 0 &&
        qd_entity_set_string(entity, "hash", address_hash(addr))
    )
        return QD_ERROR_NONE;
    return qd_error_code();
}

#define CHECK(err) if (err != 0) return qd_error_code()

qd_error_t qd_entity_refresh_router_node(qd_entity_t* entity, void *impl) {
    qd_router_node_t *rnode = (qd_router_node_t*) impl;

    /* FIXME aconway 2015-01-29: Fix all "identity settings in C" */
    CHECK(qd_entity_set_string(entity, "routerId", address_router_id(rnode->owning_addr)));
    CHECK(qd_entity_set_string(entity, "addr", address_hash(rnode->owning_addr)));
    long next_hop = rnode->next_hop ? rnode->next_hop->mask_bit : 0;
    CHECK(qd_entity_set_stringf(entity, "nextHop", "%ld", rnode->next_hop ? next_hop : 0));
    long router_link = rnode->peer_link ? rnode->peer_link->mask_bit : 0;
    CHECK(qd_entity_set_stringf(entity, "routerLink", "%ld", rnode->peer_link ? router_link : 0));
    CHECK(qd_entity_set_list(entity, "validOrigins"));
    for (uint32_t bit = 1; bit < qd_bitmask_width(); bit++) {
        if (qd_bitmask_value(rnode->valid_origins, bit)) {
            CHECK(qd_entity_set_stringf(entity, "validOrigins", "%d", bit));
        }
    }
    return QD_ERROR_NONE;
}

static const char *qd_link_type_names[] = { "endpoint", "waypoint", "inter-router", "inter-area" };
ENUM_DEFINE(qd_link_type, qd_link_type_names);

static const char *qd_router_addr_text(qd_address_t *addr)
{
    return addr ? (const char*)qd_hash_key_by_handle(addr->hash_handle) : NULL;
}

static const char* qd_router_link_remote_container(qd_router_link_t* link) {
    return pn_connection_remote_container(
        pn_session_connection(qd_link_pn_session(link->link)));
}

static const char* qd_router_link_name(qd_router_link_t* link) {
    return pn_link_name(qd_link_pn(link->link));
}

qd_error_t qd_entity_refresh_router_link(qd_entity_t* entity, void *impl)
{
    qd_router_link_t *link = (qd_router_link_t*) impl;
    if (!qd_entity_set_string(entity, "linkType", qd_link_type_name(link->link_type)) &&
        !qd_entity_set_string(entity, "linkDir", (link->link_direction == QD_INCOMING) ? "in": "out") &&
        !qd_entity_set_string(entity, "linkName", qd_router_link_name(link)) &&
        !qd_entity_set_string(entity, "owningAddr", qd_router_addr_text(link->owning_addr)) &&
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
        strcpy(temp, "amqp:/_topo/");
        strcat(temp, router->router_area);
        strcat(temp, "/");
        const unsigned char* addr = qd_hash_key_by_handle(rnode->owning_addr->hash_handle);
        strcat(temp, &((char*) addr)[1]);
        strcat(temp, "/$management");
        qd_compose_insert_string(field, temp);
        rnode = DEQ_NEXT(rnode);
    }
    sys_mutex_unlock(router->lock);
}
