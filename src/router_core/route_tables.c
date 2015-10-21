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

#include "router_core_private.h"

static qdr_action_t *qdr_action(qdr_action_handler_t action_handler);
static void qdr_action_enqueue(qdr_core_t *core, qdr_action_t *action);

static void qdrh_add_router(qdr_core_t *core, qdr_action_t *action);
static void qdrh_del_router(qdr_core_t *core, qdr_action_t *action);
static void qdrh_set_link(qdr_core_t *core, qdr_action_t *action);
static void qdrh_remove_link(qdr_core_t *core, qdr_action_t *action);
static void qdrh_set_next_hop(qdr_core_t *core, qdr_action_t *action);
static void qdrh_remove_next_hop(qdr_core_t *core, qdr_action_t *action);
static void qdrh_set_valid_origins(qdr_core_t *core, qdr_action_t *action);
static void qdrh_map_destination(qdr_core_t *core, qdr_action_t *action);
static void qdrh_unmap_destination(qdr_core_t *core, qdr_action_t *action);

static qd_address_semantics_t router_addr_semantics = QD_FANOUT_SINGLE | QD_BIAS_CLOSEST | QD_CONGESTION_DROP | QD_DROP_FOR_SLOW_CONSUMERS | QD_BYPASS_VALID_ORIGINS;


//==================================================================================
// Interface Functions
//==================================================================================

void qdr_core_add_router(qdr_core_t *core, const char *address, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_add_router);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address);
    qdr_action_enqueue(core, action);
}


void qdr_core_del_router(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_del_router);
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_link(qdr_core_t *core, int router_maskbit, int link_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_set_link);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.link_maskbit   = link_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_remove_link(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_remove_link);
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_next_hop(qdr_core_t *core, int router_maskbit, int nh_router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_set_next_hop);
    action->args.route_table.router_maskbit    = router_maskbit;
    action->args.route_table.nh_router_maskbit = nh_router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_remove_next_hop(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_remove_next_hop);
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_valid_origins(qdr_core_t *core, int router_maskbit, qd_bitmask_t *routers)
{
    qdr_action_t *action = qdr_action(qdrh_set_valid_origins);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.router_set     = routers;
    qdr_action_enqueue(core, action);
}


void qdr_core_map_destination(qdr_core_t *core, int router_maskbit, const char *address, char aclass, char phase)
{
    qdr_action_t *action = qdr_action(qdrh_map_destination);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address);
    action->args.route_table.address_phase  = phase;
    action->args.route_table.address_class  = aclass;
    qdr_action_enqueue(core, action);
}


void qdr_core_unmap_destination(qdr_core_t *core, int router_maskbit, const char *address, char aclass, char phase)
{
    qdr_action_t *action = qdr_action(qdrh_unmap_destination);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address);
    action->args.route_table.address_phase  = phase;
    action->args.route_table.address_class  = aclass;
    qdr_action_enqueue(core, action);
}

void qdr_core_route_table_handlers(qdr_core_t           *core, 
                                   void                 *context,
                                   qdr_mobile_added_t    mobile_added,
                                   qdr_mobile_removed_t  mobile_removed,
                                   qdr_link_lost_t       link_lost)
{
    core->rt_context        = context;
    core->rt_mobile_added   = mobile_added;
    core->rt_mobile_removed = mobile_removed;
    core->rt_link_lost      = link_lost;
}


//==================================================================================
// Internal Functions
//==================================================================================

static qdr_action_t *qdr_action(qdr_action_handler_t action_handler)
{
    qdr_action_t *action = new_qdr_action_t();
    ZERO(action);
    action->action_handler = action_handler;
    return action;
}

static void qdr_action_enqueue(qdr_core_t *core, qdr_action_t *action)
{
    sys_mutex_lock(core->lock);
    DEQ_INSERT_TAIL(core->action_list, action);
    sys_mutex_unlock(core->lock);
}


//==================================================================================
// In-Thread Functions
//==================================================================================

void qdr_route_table_setup(qdr_core_t *core)
{
    DEQ_INIT(core->addrs);
    //DEQ_INIT(core->links);
    DEQ_INIT(core->routers);
    core->addr_hash = qd_hash(10, 32, 0);

    core->router_addr   = qdr_add_local_address(core, "qdrouter",    QD_SEMANTICS_ROUTER_CONTROL);
    core->routerma_addr = qdr_add_local_address(core, "qdrouter.ma", QD_SEMANTICS_DEFAULT);
    core->hello_addr    = qdr_add_local_address(core, "qdhello",     QD_SEMANTICS_ROUTER_CONTROL);

    core->routers_by_mask_bit = NEW_PTR_ARRAY(qdr_node_t, qd_bitmask_width());
    for (int idx = 0; idx < qd_bitmask_width(); idx++)
        core->routers_by_mask_bit[idx] = 0;
}


static void qdrh_add_router(qdr_core_t *core, qdr_action_t *action)
{
    int router_maskbit = action->args.route_table.router_maskbit;

    if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "add_router: Router maskbit out of range: %d", router_maskbit);
        return;
    }

    if (core->routers_by_mask_bit[router_maskbit] != 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "add_router: Router maskbit already in use: %d", router_maskbit);
        return;
    }

    //
    // Hash lookup the address to ensure there isn't an existing router address.
    //
    qd_field_iterator_t *iter = action->args.route_table.address->iterator;
    qdr_address_t       *addr;

    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    assert(addr == 0);

    //
    // Create an address record for this router and insert it in the hash table.
    // This record will be found whenever a "foreign" topological address to this
    // remote router is looked up.
    //
    addr = qdr_address(router_addr_semantics);
    qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
    DEQ_INSERT_TAIL(core->addrs, addr);

    //
    // Create a router-node record to represent the remote router.
    //
    qdr_node_t *rnode = new_qdr_node_t();
    DEQ_ITEM_INIT(rnode);
    rnode->owning_addr   = addr;
    rnode->mask_bit      = router_maskbit;
    rnode->next_hop      = 0;
    rnode->peer_link     = 0;
    rnode->ref_count     = 0;
    rnode->valid_origins = qd_bitmask(0);

    DEQ_INSERT_TAIL(core->routers, rnode);

    //
    // Link the router record to the address record.
    //
    qdr_add_node_ref(&addr->rnodes, rnode);

    //
    // Link the router record to the router address records.
    //
    qdr_add_node_ref(&core->router_addr->rnodes, rnode);
    qdr_add_node_ref(&core->routerma_addr->rnodes, rnode);

    //
    // Add the router record to the mask-bit index.
    //
    core->routers_by_mask_bit[router_maskbit] = rnode;

    qdr_field_free(action->args.route_table.address);
}


static void qdrh_del_router(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_set_link(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_remove_link(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_set_next_hop(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_remove_next_hop(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_set_valid_origins(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_map_destination(qdr_core_t *core, qdr_action_t *action)
{
}


static void qdrh_unmap_destination(qdr_core_t *core, qdr_action_t *action)
{
}


