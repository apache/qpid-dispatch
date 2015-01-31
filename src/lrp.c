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

#include "dispatch_private.h"
#include "entity_cache.h"
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/connection_manager.h>
#include <qpid/dispatch/timer.h>
#include <memory.h>
#include <stdio.h>

static const char qd_link_route_addr_prefix = 'C';

static void qd_lrpc_open_handler(void *context, qd_connection_t *conn)
{
    qd_lrp_container_t *lrpc   = (qd_lrp_container_t*) context;
    qd_lrp_t           *lrp    = DEQ_HEAD(lrpc->lrps);
    qd_router_t        *router = lrpc->qd->router;

    lrpc->conn = conn;

    while (lrp) {
        qd_address_t        *addr;
        qd_field_iterator_t *iter;
        bool                 propagate;
        char                 unused;

        qd_log(router->log_source, QD_LOG_INFO, "Activating Prefix '%s' for routed links to '%s'",
               lrp->prefix, qd_config_connector_name(lrpc->cc));

        //
        // Create an address iterator for the prefix address with the namespace
        // prefix for link-attach routed addresses.
        //
        iter = qd_field_iterator_string(lrp->prefix, ITER_VIEW_ADDRESS_HASH);
        qd_field_iterator_override_prefix(iter, qd_link_route_addr_prefix);

        //
        // Find the address in the router's hash table.  If not found, create one
        // and hash it into the table.
        //
        sys_mutex_lock(router->lock);
        qd_hash_retrieve(router->addr_hash, iter, (void**) &addr);
        if (!addr) {
            addr = qd_address();
            qd_hash_insert(router->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_INSERT_TAIL(router->addrs, addr);
            addr->semantics = router_semantics_for_addr(router, iter, '\0', &unused);
            qd_entity_cache_add(QD_ROUTER_ADDRESS_TYPE, addr);
        }

        //
        // Link the LRP record into the address as a local endpoint for routed link-attaches.
        // If this is the first instance for this address, flag the address for propagation
        // across the network.
        //
        qd_router_add_lrp_ref_LH(&addr->lrps, lrp);
        propagate = DEQ_SIZE(addr->lrps) == 1;
        sys_mutex_unlock(router->lock);

        //
        // Propagate the address if appropriate
        //
        if (propagate)
            qd_router_mobile_added(router, iter);

        qd_field_iterator_free(iter);
        lrp = DEQ_NEXT(lrp);
    }
}


static void qd_lrpc_close_handler(void *context, qd_connection_t *conn)
{
    qd_lrp_container_t *lrpc   = (qd_lrp_container_t*) context;
    qd_lrp_t           *lrp    = DEQ_HEAD(lrpc->lrps);
    qd_router_t        *router = lrpc->qd->router;

    lrpc->conn = 0;

    while (lrp) {
        qd_address_t        *addr;
        qd_field_iterator_t *iter;
        bool                 propagate = false;

        qd_log(router->log_source, QD_LOG_INFO, "Removing Prefix '%s' for routed links to '%s'",
               lrp->prefix, qd_config_connector_name(lrpc->cc));

        //
        // Create an address iterator for the prefix address with the namespace
        // prefix for link-attach routed addresses.
        //
        iter = qd_field_iterator_string(lrp->prefix, ITER_VIEW_ADDRESS_HASH);
        qd_field_iterator_override_prefix(iter, qd_link_route_addr_prefix);

        //
        // Find the address in the router's hash table.
        //
        sys_mutex_lock(router->lock);
        qd_hash_retrieve(router->addr_hash, iter, (void**) &addr);
        if (addr) {
            //
            // Unlink the lrp from the address.  If this is the last lrp in the address, we need
            // to tell the other routers.
            //
            qd_router_del_lrp_ref_LH(&addr->lrps, lrp);
            propagate = DEQ_SIZE(addr->lrps) == 0;
        }
        sys_mutex_unlock(router->lock);

        //
        // Propagate the address if appropriate
        //
        if (propagate) {
            char *text = (char*) qd_field_iterator_copy(iter);
            qd_router_mobile_removed(router, text);
            free(text);
        }

        qd_field_iterator_free(iter);
        lrp = DEQ_NEXT(lrp);
    }
}


void qd_lrpc_timer_handler(void *context)
{
    qd_lrp_container_t *lrpc = (qd_lrp_container_t*) context;
    qd_connection_manager_set_handlers(lrpc->cc,
                                       qd_lrpc_open_handler,
                                       qd_lrpc_close_handler,
                                       (void*) lrpc);
    qd_connection_manager_start_on_demand(lrpc->qd, lrpc->cc);
}


qd_lrp_t *qd_lrp_LH(const char *prefix, qd_lrp_container_t *lrpc)
{
    qd_lrp_t *lrp = NEW(qd_lrp_t);

    if (lrp) {
        DEQ_ITEM_INIT(lrp);
        lrp->prefix    = strdup(prefix);
        lrp->container = lrpc;
        DEQ_INSERT_TAIL(lrpc->lrps, lrp);
    }

    return lrp;
}


void qd_lrp_free(qd_lrp_t *lrp)
{
    if (lrp) {
        free(lrp->prefix);
        DEQ_REMOVE(lrp->container->lrps, lrp);
        free(lrp);
    }
}

