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
#include <qpid/dispatch/agent.h>
#include "dispatch_private.h"
#include "router_private.h"
#include "c_entity.h"

static const char *qd_router_mode_names[] = {
    "standalone",
    "interior",
    "edge",
    "endpoint"
};
ENUM_DEFINE(qd_router_mode, qd_router_mode_names);

qd_error_t qd_c_entity_update_router(qd_entity_t* entity, void *impl) {
    qd_dispatch_t *qd = (qd_dispatch_t*) impl;
    qd_router_t *router = qd->router;
    if (qd_entity_set_string(entity, "name", router->router_id) == 0 &&
        qd_entity_set_string(entity, "identity", router->router_id) == 0 &&
        qd_entity_set_string(entity, "area", router->router_area) == 0 &&
        qd_entity_set_string(entity, "mode", qd_router_mode_name(router->router_mode)) == 0 &&
        qd_entity_set_long(entity, "addrCount", DEQ_SIZE(router->addrs)) == 0 &&
        qd_entity_set_long(entity, "linkCount", DEQ_SIZE(router->links)) == 0 &&
        qd_entity_set_long(entity, "nodeCount", DEQ_SIZE(router->routers)) == 0
    )
        return QD_ERROR_NONE;
    return qd_error_code();
}

static const char *address_text(qd_address_t *addr)
{
    return addr ? (const char*) qd_hash_key_by_handle(addr->hash_handle) : 0;
}

qd_error_t qd_c_entity_update_router_address(qd_entity_t* entity, void *impl) {
    qd_address_t *addr = (qd_address_t*) impl;
    if ((qd_entity_has(entity, "identity") ||
         qd_entity_set_string(entity, "identity", address_text(addr)) == 0) &&
        qd_entity_set_bool(entity, "inProcess", addr->handler != 0) == 0 &&
        qd_entity_set_long(entity, "subscriberCount", DEQ_SIZE(addr->rlinks)) == 0 &&
        qd_entity_set_long(entity, "remoteCount", DEQ_SIZE(addr->rnodes)) == 0 &&
        qd_entity_set_long(entity, "deliveriesIngress", addr->deliveries_ingress) == 0 &&
        qd_entity_set_long(entity, "deliveriesEgress", addr->deliveries_egress) == 0 &&
        qd_entity_set_long(entity, "deliveriesTransit", addr->deliveries_transit) == 0 &&
        qd_entity_set_long(entity, "deliveriesToContainer", addr->deliveries_to_container) == 0 &&
        qd_entity_set_long(entity, "deliveriesFromContainer", addr->deliveries_from_container) == 0
    )
        return QD_ERROR_NONE;
    return qd_error_code();
}

#define CHECK(err) if (err != 0) return qd_error_code()

qd_error_t qd_c_entity_update_router_node(qd_entity_t* entity, void *impl) {
    qd_router_node_t *rnode = (qd_router_node_t*) impl;

    if (!qd_entity_has(entity, "identity")) {
        CHECK(qd_entity_set_stringf(entity, "identity", "%s-%d", QD_ROUTER_NODE_TYPE, rnode->mask_bit));
    }
    CHECK(qd_entity_set_string(entity, "addr", address_text(rnode->owning_addr)));
    long next_hop = rnode->next_hop ? rnode->next_hop->mask_bit : 0;
    CHECK(qd_entity_set_longp(entity, "nextHop", rnode->next_hop ? &next_hop : 0));
    long router_link = rnode->peer_link ? rnode->peer_link->mask_bit : 0;
    CHECK(qd_entity_set_longp(entity, "routerLink", rnode->peer_link ? &router_link : 0));
    CHECK(qd_entity_set_list(entity, "validOrigins"));
    for (uint32_t bit = 1; bit < qd_bitmask_width(); bit++) {
        if (qd_bitmask_value(rnode->valid_origins, bit)) {
            CHECK(qd_entity_set_long(entity, "validOrigins", bit));
        }
    }
    return QD_ERROR_NONE;
}

static void router_attr_name(void *object_handle, void *cor, void *unused)
{
    qd_router_t *router = (qd_router_t*) object_handle;
    qd_agent_value_string(cor, 0, router->router_id);
}


static void router_attr_area(void *object_handle, void *cor, void *unused)
{
    qd_router_t *router = (qd_router_t*) object_handle;
    qd_agent_value_string(cor, 0, router->router_area);
}


static void router_attr_mode(void *object_handle, void *cor, void *unused)
{
    qd_router_t *router = (qd_router_t*) object_handle;
    switch (router->router_mode) {
    case QD_ROUTER_MODE_STANDALONE:  qd_agent_value_string(cor, 0, "Standalone");  break;
    case QD_ROUTER_MODE_INTERIOR:    qd_agent_value_string(cor, 0, "Interior");    break;
    case QD_ROUTER_MODE_EDGE:        qd_agent_value_string(cor, 0, "Edge");        break;
    case QD_ROUTER_MODE_ENDPOINT:    qd_agent_value_string(cor, 0, "Endpoint");    break;
    }
}


static void router_attr_addrCount(void *object_handle, void *cor, void *unused)
{
    qd_router_t *router = (qd_router_t*) object_handle;
    qd_agent_value_uint(cor, 0, DEQ_SIZE(router->addrs));
}


static void router_attr_linkCount(void *object_handle, void *cor, void *unused)
{
    qd_router_t *router = (qd_router_t*) object_handle;
    qd_agent_value_uint(cor, 0, DEQ_SIZE(router->links));
}


static void router_attr_nodeCount(void *object_handle, void *cor, void *unused)
{
    qd_router_t *router = (qd_router_t*) object_handle;
    qd_agent_value_uint(cor, 0, DEQ_SIZE(router->routers));
}


static const qd_agent_attribute_t ROUTER_ATTRIBUTES[] =
    {{"name", router_attr_name, 0},
     {"identity", router_attr_name, 0},
     {"area", router_attr_area, 0},
     {"mode", router_attr_mode, 0},
     {"addrCount", router_attr_addrCount, 0},
     {"linkCount", router_attr_linkCount, 0},
     {"nodeCount", router_attr_nodeCount, 0},
     {0, 0, 0}};


static void qd_router_query_router(void *context, void *cor)
{
    qd_router_t *router = (qd_router_t*) context;

    sys_mutex_lock(router->lock);
    qd_agent_object(cor, (void*) router);
    sys_mutex_unlock(router->lock);
}

static void link_attr_name(void *object_handle, void *cor, void *unused)
{
    qd_router_link_t *link = (qd_router_link_t*) object_handle;
    qd_agent_value_uint(cor, 0, link->mask_bit);
}


static void link_attr_linkType(void *object_handle, void *cor, void *unused)
{
    qd_router_link_t *link = (qd_router_link_t*) object_handle;
    switch (link->link_type) {
    case QD_LINK_ENDPOINT: qd_agent_value_string(cor, 0, "endpoint");     break;
    case QD_LINK_WAYPOINT: qd_agent_value_string(cor, 0, "waypoint");     break;
    case QD_LINK_ROUTER:   qd_agent_value_string(cor, 0, "inter-router"); break;
    case QD_LINK_AREA:     qd_agent_value_string(cor, 0, "inter-area");   break;
    }
}


static void link_attr_linkDir(void *object_handle, void *cor, void *unused)
{
    qd_router_link_t *link = (qd_router_link_t*) object_handle;
    if (link->link_direction == QD_INCOMING)
        qd_agent_value_string(cor, 0, "in");
    else
        qd_agent_value_string(cor, 0, "out");
}


static void link_attr_owningAddr(void *object_handle, void *cor, void *unused)
{
    qd_router_link_t *link = (qd_router_link_t*) object_handle;
    const char *text = address_text(link->owning_addr);
    if (text)
        qd_agent_value_string(cor, 0, text);
    else
        qd_agent_value_null(cor, 0);
}


static void link_attr_eventFifoDepth(void *object_handle, void *cor, void *unused)
{
    qd_router_link_t *link = (qd_router_link_t*) object_handle;
    qd_agent_value_uint(cor, 0, DEQ_SIZE(link->event_fifo));
}


static void link_attr_msgFifoDepth(void *object_handle, void *cor, void *unused)
{
    qd_router_link_t *link = (qd_router_link_t*) object_handle;
    qd_agent_value_uint(cor, 0, DEQ_SIZE(link->msg_fifo));
}


static const qd_agent_attribute_t LINK_ATTRIBUTES[] =
    {{"name", link_attr_name, 0},
     {"identity", link_attr_name, 0},
     {"linkType", link_attr_linkType, 0},
     {"linkDir", link_attr_linkDir, 0},
     {"owningAddr", link_attr_owningAddr, 0},
     {"eventFifoDepth", link_attr_eventFifoDepth, 0},
     {"msgFifoDepth", link_attr_msgFifoDepth, 0},
     {0, 0, 0}};


static void qd_router_query_link(void *context, void *cor)
{
    qd_router_t *router = (qd_router_t*) context;

    sys_mutex_lock(router->lock);
    qd_router_link_t *link = DEQ_HEAD(router->links);

    while (link) {
        if (!qd_agent_object(cor, (void*) link))
            break;
        link = DEQ_NEXT(link);
    }
    sys_mutex_unlock(router->lock);
}

static const char *qd_link_type_names[] = { "endpoint", "waypoint", "inter-router", "inter-area" };
ENUM_DEFINE(qd_link_type, qd_link_type_names);

static const char *qd_router_addr_text(qd_address_t *addr)
{
    return addr ? (const char*)qd_hash_key_by_handle(addr->hash_handle) : NULL;
}

qd_error_t qd_c_entity_update_router_link(qd_entity_t* entity, void *impl)
{
    qd_router_link_t *link = (qd_router_link_t*) impl;
    /* FIXME aconway 2014-10-17: old management used link->bit_mask as name/identity,
     * but even when prefixed with router.link this is not unique. Let python agent
     * generate a name for now, revisit with a better name later.
     */
    if (!qd_entity_set_string(entity, "linkType", qd_link_type_name(link->link_type)) &&
        !qd_entity_set_string(entity, "linkDir", (link->link_direction == QD_INCOMING) ? "in": "out") &&
        !qd_entity_set_string(entity, "owningAddr", qd_router_addr_text(link->owning_addr)) &&
        !qd_entity_set_long(entity, "eventFifoDepth", DEQ_SIZE(link->event_fifo)) &&
        !qd_entity_set_long(entity, "msgFifoDepth", DEQ_SIZE(link->msg_fifo))
    )
        return QD_ERROR_NONE;
    return qd_error_code();
}

static void node_attr_name(void *object_handle, void *cor, void *unused)
{
    qd_router_node_t *node = (qd_router_node_t*) object_handle;
    qd_agent_value_uint(cor, 0, node->mask_bit);
}


static void node_attr_addr(void *object_handle, void *cor, void *unused)
{
    qd_router_node_t *node = (qd_router_node_t*) object_handle;
    qd_agent_value_string(cor, 0, address_text(node->owning_addr));
}


static void node_attr_nextHop(void *object_handle, void *cor, void *unused)
{
    qd_router_node_t *node = (qd_router_node_t*) object_handle;
    if (node->next_hop)
        qd_agent_value_uint(cor, 0, node->next_hop->mask_bit);
    else
        qd_agent_value_null(cor, 0);
}


static void node_attr_routerLink(void *object_handle, void *cor, void *unused)
{
    qd_router_node_t *node = (qd_router_node_t*) object_handle;
    if (node->peer_link)
        qd_agent_value_uint(cor, 0, node->peer_link->mask_bit);
    else
        qd_agent_value_null(cor, 0);
}


static void node_attr_validOrigins(void *object_handle, void *cor, void *unused)
{
    qd_router_node_t *node = (qd_router_node_t*) object_handle;
    qd_agent_value_start_list(cor, 0);
    for (uint32_t bit = 1; bit < qd_bitmask_width(); bit++)
        if (qd_bitmask_value(node->valid_origins, bit))
            qd_agent_value_uint(cor, 0, bit);
    qd_agent_value_end_list(cor);
}


static const qd_agent_attribute_t NODE_ATTRIBUTES[] =
    {{"name", node_attr_name, 0},
     {"identity", node_attr_name, 0},
     {"addr", node_attr_addr, 0},
     {"nextHop", node_attr_nextHop, 0},
     {"routerLink", node_attr_routerLink, 0},
     {"validOrigins", node_attr_validOrigins, 0},
     {0, 0, 0}};

static void qd_router_query_node(void *context, void *cor)
{
    qd_router_t *router = (qd_router_t*) context;

    sys_mutex_lock(router->lock);
    qd_router_node_t *node = DEQ_HEAD(router->routers);
    while (node) {
        if (!qd_agent_object(cor, (void*) node))
            break;
        node = DEQ_NEXT(node);
    }
    sys_mutex_unlock(router->lock);
}


static void addr_attr_name(void *object_handle, void *cor, void *unused)
{
    qd_address_t *addr = (qd_address_t*) object_handle;
    qd_agent_value_string(cor, 0, address_text(addr));
}


static void addr_attr_inProcess(void *object_handle, void *cor, void *unused)
{
    qd_address_t *addr = (qd_address_t*) object_handle;
    qd_agent_value_boolean(cor, 0, addr->handler != 0);
}


static void addr_attr_subscriberCount(void *object_handle, void *cor, void *unused)
{
    qd_address_t *addr = (qd_address_t*) object_handle;
    qd_agent_value_uint(cor, 0, DEQ_SIZE(addr->rlinks));
}


static void addr_attr_remoteCount(void *object_handle, void *cor, void *unused)
{
    qd_address_t *addr = (qd_address_t*) object_handle;
    qd_agent_value_uint(cor, 0, DEQ_SIZE(addr->rnodes));
}


static void addr_attr_deliveriesIngress(void *object_handle, void *cor, void *unused)
{
    qd_address_t *addr = (qd_address_t*) object_handle;
    qd_agent_value_uint(cor, 0, addr->deliveries_ingress);
}


static void addr_attr_deliveriesEgress(void *object_handle, void *cor, void *unused)
{
    qd_address_t *addr = (qd_address_t*) object_handle;
    qd_agent_value_uint(cor, 0, addr->deliveries_egress);
}


static void addr_attr_deliveriesTransit(void *object_handle, void *cor, void *unused)
{
    qd_address_t *addr = (qd_address_t*) object_handle;
    qd_agent_value_uint(cor, 0, addr->deliveries_transit);
}


static void addr_attr_deliveriesToContainer(void *object_handle, void *cor, void *unused)
{
    qd_address_t *addr = (qd_address_t*) object_handle;
    qd_agent_value_uint(cor, 0, addr->deliveries_to_container);
}


static void addr_attr_deliveriesFromContainer(void *object_handle, void *cor, void *unused)
{
    qd_address_t *addr = (qd_address_t*) object_handle;
    qd_agent_value_uint(cor, 0, addr->deliveries_from_container);
}


static const qd_agent_attribute_t ADDRESS_ATTRIBUTES[] =
    {{"name", addr_attr_name, 0},
     {"identity", addr_attr_name, 0},
     {"inProcess", addr_attr_inProcess, 0},
     {"subscriberCount", addr_attr_subscriberCount, 0},
     {"remoteCount", addr_attr_remoteCount, 0},
     {"deliveriesIngress", addr_attr_deliveriesIngress, 0},
     {"deliveriesEgress", addr_attr_deliveriesEgress, 0},
     {"deliveriesTransit", addr_attr_deliveriesTransit, 0},
     {"deliveriesToContainer", addr_attr_deliveriesToContainer, 0},
     {"deliveriesFromContainer", addr_attr_deliveriesFromContainer, 0},
     {0, 0, 0}};

static void qd_router_query_address(void *context, void *cor)
{
    qd_router_t *router = (qd_router_t*) context;

    sys_mutex_lock(router->lock);
    qd_address_t *addr = DEQ_HEAD(router->addrs);
    while (addr) {
        if (!qd_agent_object(cor, (void*) addr))
            break;
        addr = DEQ_NEXT(addr);
    }
    sys_mutex_unlock(router->lock);
}


qd_error_t qd_router_agent_setup(qd_router_t *router)
{
    qd_error_clear();
    router->class_router =
        qd_agent_register_class(router->qd, QD_ROUTER_TYPE, router, ROUTER_ATTRIBUTES, qd_router_query_router);
    router->class_link =
        qd_agent_register_class(router->qd, QD_ROUTER_LINK_TYPE, router, LINK_ATTRIBUTES, qd_router_query_link);
    router->class_node =
        qd_agent_register_class(router->qd, QD_ROUTER_NODE_TYPE, router, NODE_ATTRIBUTES, qd_router_query_node);
    router->class_address =
        qd_agent_register_class(router->qd, QD_ROUTER_ADDRESS_TYPE, router, ADDRESS_ATTRIBUTES, qd_router_query_address);
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
