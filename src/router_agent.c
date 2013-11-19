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

//static char *module = "router.agent";

#define QD_ROUTER_CLASS_ROUTER  1
#define QD_ROUTER_CLASS_LINK    2
#define QD_ROUTER_CLASS_NODE    3
#define QD_ROUTER_CLASS_ADDRESS 4

typedef struct qd_router_class_t {
    qd_router_t *router;
    int          class_id;
} qd_router_class_t;


static void qd_router_schema_handler(void *context, void *correlator)
{
}


static const char *qd_router_addr_text(qd_address_t *addr)
{
    if (addr) {
        const unsigned char *text = qd_hash_key_by_handle(addr->hash_handle);
        if (text)
            return (const char*) text;
    }
    return 0;
}


static void qd_router_query_router(qd_router_t *router, void *cor)
{
    qd_agent_value_string(cor, "area",      router->router_area);
    qd_agent_value_string(cor, "router_id", router->router_id);

    char *mode = "";
    switch (router->router_mode) {
    case QD_ROUTER_MODE_STANDALONE:  mode = "Standalone";  break;
    case QD_ROUTER_MODE_INTERIOR:    mode = "Interior";    break;
    case QD_ROUTER_MODE_EDGE:        mode = "Edge";        break;
    }
    qd_agent_value_string(cor, "mode", mode);

    sys_mutex_lock(router->lock);
    qd_agent_value_uint(cor, "addr_count", DEQ_SIZE(router->addrs));
    qd_agent_value_uint(cor, "link_count", DEQ_SIZE(router->links));
    qd_agent_value_uint(cor, "node_count", DEQ_SIZE(router->routers));
    sys_mutex_unlock(router->lock);

    qd_agent_value_complete(cor, 0);
}


static void qd_router_query_link(qd_router_t *router, void *cor)
{
    sys_mutex_lock(router->lock);
    qd_router_link_t *link = DEQ_HEAD(router->links);
    const char       *link_type = "?";
    const char       *link_dir;

    while (link) {
        qd_agent_value_uint(cor, "index", link->mask_bit);
        switch (link->link_type) {
        case QD_LINK_ENDPOINT: link_type = "endpoint";     break;
        case QD_LINK_ROUTER:   link_type = "inter-router"; break;
        case QD_LINK_AREA:     link_type = "inter-area";   break;
        }
        qd_agent_value_string(cor, "link-type", link_type);

        if (link->link_direction == QD_INCOMING)
            link_dir = "in";
        else
            link_dir = "out";
        qd_agent_value_string(cor, "link-dir", link_dir);

        const char *text = qd_router_addr_text(link->owning_addr);
        if (text)
            qd_agent_value_string(cor, "owning-addr", text);
        else
            qd_agent_value_null(cor, "owning-addr");

        link = DEQ_NEXT(link);
        qd_agent_value_complete(cor, link != 0);
    }
    sys_mutex_unlock(router->lock);
}


static void qd_router_query_node(qd_router_t *router, void *cor)
{
    sys_mutex_lock(router->lock);
    qd_router_node_t *node = DEQ_HEAD(router->routers);
    while (node) {
        qd_agent_value_uint(cor, "index", node->mask_bit);
        qd_agent_value_string(cor, "addr", qd_router_addr_text(node->owning_addr));
        if (node->next_hop)
            qd_agent_value_uint(cor, "next-hop", node->next_hop->mask_bit);
        else
            qd_agent_value_null(cor, "next-hop");
        if (node->peer_link)
            qd_agent_value_uint(cor, "router-link", node->peer_link->mask_bit);
        else
            qd_agent_value_null(cor, "router-link");
        node = DEQ_NEXT(node);
        qd_agent_value_complete(cor, node != 0);
    }
    sys_mutex_unlock(router->lock);
}


static void qd_router_query_address(qd_router_t *router, void *cor)
{
    sys_mutex_lock(router->lock);
    qd_address_t *addr = DEQ_HEAD(router->addrs);
    while (addr) {
        qd_agent_value_string(cor, "addr", qd_router_addr_text(addr));
        qd_agent_value_boolean(cor, "in-process", addr->handler != 0);
        qd_agent_value_uint(cor, "subscriber-count", DEQ_SIZE(addr->rlinks));
        qd_agent_value_uint(cor, "remote-count", DEQ_SIZE(addr->rnodes));
        qd_agent_value_uint(cor, "deliveries-ingress", addr->deliveries_ingress);
        qd_agent_value_uint(cor, "deliveries-egress", addr->deliveries_egress);
        qd_agent_value_uint(cor, "deliveries-transit", addr->deliveries_transit);
        qd_agent_value_uint(cor, "deliveries-to-container", addr->deliveries_to_container);
        qd_agent_value_uint(cor, "deliveries-from-container", addr->deliveries_from_container);
        addr = DEQ_NEXT(addr);
        qd_agent_value_complete(cor, addr != 0);
    }
    sys_mutex_unlock(router->lock);
}


static void qd_router_query_handler(void* context, const char *id, void *correlator)
{
    qd_router_class_t *cls = (qd_router_class_t*) context;
    switch (cls->class_id) {
    case QD_ROUTER_CLASS_ROUTER:  qd_router_query_router(cls->router, correlator); break;
    case QD_ROUTER_CLASS_LINK:    qd_router_query_link(cls->router, correlator); break;
    case QD_ROUTER_CLASS_NODE:    qd_router_query_node(cls->router, correlator); break;
    case QD_ROUTER_CLASS_ADDRESS: qd_router_query_address(cls->router, correlator); break;
    }
}


static qd_agent_class_t *qd_router_setup_class(qd_router_t *router, const char *fqname, int id)
{
    qd_router_class_t *cls = NEW(qd_router_class_t);
    cls->router   = router;
    cls->class_id = id;

    return qd_agent_register_class(router->qd, fqname, cls,
                                   qd_router_schema_handler,
                                   qd_router_query_handler);
}


void qd_router_agent_setup(qd_router_t *router)
{
    router->class_router =
        qd_router_setup_class(router, "org.apache.qpid.dispatch.router", QD_ROUTER_CLASS_ROUTER);
    router->class_link =
        qd_router_setup_class(router, "org.apache.qpid.dispatch.router.link", QD_ROUTER_CLASS_LINK);
    router->class_node =
        qd_router_setup_class(router, "org.apache.qpid.dispatch.router.node", QD_ROUTER_CLASS_NODE);
    router->class_address =
        qd_router_setup_class(router, "org.apache.qpid.dispatch.router.address", QD_ROUTER_CLASS_ADDRESS);
}


void qd_router_build_node_list(qd_dispatch_t *qd, qd_composed_field_t *field)
{
    qd_router_t *router = qd->router;
    char         temp[1000];  // FIXME

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

