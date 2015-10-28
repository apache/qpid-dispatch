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
#include <stdio.h>

static void qdrh_add_router_CT        (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdrh_del_router_CT        (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdrh_set_link_CT          (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdrh_remove_link_CT       (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdrh_set_next_hop_CT      (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdrh_remove_next_hop_CT   (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdrh_set_valid_origins_CT (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdrh_map_destination_CT   (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdrh_unmap_destination_CT (qdr_core_t *core, qdr_action_t *action, bool discard);

static qd_address_semantics_t router_addr_semantics = QD_FANOUT_SINGLE | QD_BIAS_CLOSEST | QD_CONGESTION_DROP | QD_DROP_FOR_SLOW_CONSUMERS | QD_BYPASS_VALID_ORIGINS;


//==================================================================================
// Interface Functions
//==================================================================================

void qdr_core_add_router(qdr_core_t *core, const char *address, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_add_router_CT);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address);
    qdr_action_enqueue(core, action);
}


void qdr_core_del_router(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_del_router_CT);
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_link(qdr_core_t *core, int router_maskbit, int link_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_set_link_CT);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.link_maskbit   = link_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_remove_link(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_remove_link_CT);
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_next_hop(qdr_core_t *core, int router_maskbit, int nh_router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_set_next_hop_CT);
    action->args.route_table.router_maskbit    = router_maskbit;
    action->args.route_table.nh_router_maskbit = nh_router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_remove_next_hop(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdrh_remove_next_hop_CT);
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_valid_origins(qdr_core_t *core, int router_maskbit, qd_bitmask_t *routers)
{
    qdr_action_t *action = qdr_action(qdrh_set_valid_origins_CT);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.router_set     = routers;
    qdr_action_enqueue(core, action);
}


void qdr_core_map_destination(qdr_core_t *core, int router_maskbit, const char *address, char aclass, char phase, qd_address_semantics_t sem)
{
    qdr_action_t *action = qdr_action(qdrh_map_destination_CT);
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address);
    action->args.route_table.address_phase  = phase;
    action->args.route_table.address_class  = aclass;
    action->args.route_table.semantics      = sem;
    qdr_action_enqueue(core, action);
}


void qdr_core_unmap_destination(qdr_core_t *core, int router_maskbit, const char *address, char aclass, char phase)
{
    qdr_action_t *action = qdr_action(qdrh_unmap_destination_CT);
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
// In-Thread Functions
//==================================================================================

void qdr_route_table_setup_CT(qdr_core_t *core)
{
    DEQ_INIT(core->addrs);
    DEQ_INIT(core->links);
    DEQ_INIT(core->routers);
    core->addr_hash = qd_hash(10, 32, 0);

    core->router_addr   = qdr_add_local_address_CT(core, "qdrouter",    QD_SEMANTICS_ROUTER_CONTROL);
    core->routerma_addr = qdr_add_local_address_CT(core, "qdrouter.ma", QD_SEMANTICS_DEFAULT);
    core->hello_addr    = qdr_add_local_address_CT(core, "qdhello",     QD_SEMANTICS_ROUTER_CONTROL);

    core->routers_by_mask_bit   = NEW_PTR_ARRAY(qdr_node_t, qd_bitmask_width());
    core->out_links_by_mask_bit = NEW_PTR_ARRAY(qdr_link_t, qd_bitmask_width());
    for (int idx = 0; idx < qd_bitmask_width(); idx++) {
        core->routers_by_mask_bit[idx]   = 0;
        core->out_links_by_mask_bit[idx] = 0;
    }
}


/**
 * Check an address to see if it no longer has any associated destinations.
 * Depending on its policy, the address may be eligible for being closed out
 * (i.e. Logging its terminal statistics and freeing its resources).
 */
static void qdr_check_addr_CT(qdr_core_t *core, qdr_address_t *addr, bool was_local)
{
    if (addr == 0)
        return;

    bool         to_delete      = false;
    bool         no_more_locals = false;
    qdr_field_t *key_field      = 0;

    //
    // If the address has no in-process consumer or destinations, it should be
    // deleted.
    //
    if (addr->on_message == 0 &&
        DEQ_SIZE(addr->rlinks) == 0 && DEQ_SIZE(addr->rnodes) == 0 &&
        !addr->waypoint && !addr->block_deletion)
        to_delete = true;

    //
    // If we have just removed a local linkage and it was the last local linkage,
    // we need to notify the router module that there is no longer a local
    // presence of this address.
    //
    if (was_local && DEQ_SIZE(addr->rlinks) == 0) {
        no_more_locals = true;
        const unsigned char *key = qd_hash_key_by_handle(addr->hash_handle);
        if (key && (key[0] == 'M' || key[0] == 'C' || key[0] == 'D'))
            key_field = qdr_field((const char*) key);
    }

    if (to_delete) {
        //
        // Delete the address but grab the hash key so we can use it outside the
        // critical section.
        //
        qd_hash_remove_by_handle(core->addr_hash, addr->hash_handle);
        DEQ_REMOVE(core->addrs, addr);
        qd_hash_handle_free(addr->hash_handle);
        free_qdr_address_t(addr);
    }

    //
    // If the address is mobile-class and it was just removed from a local link,
    // tell the router module that it is no longer attached locally.
    //
    if (no_more_locals && key_field) {
        //
        // TODO - Defer-call mobile-removed
        //
    }
}


static void qdrh_add_router_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int          router_maskbit = action->args.route_table.router_maskbit;
    qdr_field_t *address        = action->args.route_table.address;

    if (discard) {
        qdr_field_free(address);
        return;
    }

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "add_router: Router maskbit out of range: %d", router_maskbit);
            break;
        }

        if (core->routers_by_mask_bit[router_maskbit] != 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "add_router: Router maskbit already in use: %d", router_maskbit);
            break;
        }

        //
        // Hash lookup the address to ensure there isn't an existing router address.
        //
        qd_field_iterator_t *iter = address->iterator;
        qdr_address_t       *addr;

        qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
        qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);

        if (addr) {
            qd_log(core->log, QD_LOG_CRITICAL, "add_router: Data inconsistency for router-maskbit %d", router_maskbit);
            assert(addr == 0);  // Crash in debug mode.  This should never happen
            break;
        }

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
    } while (false);

    qdr_field_free(address);
}


static void qdrh_del_router_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int router_maskbit = action->args.route_table.router_maskbit;

    if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "del_router: Router maskbit out of range: %d", router_maskbit);
        return;
    }

    if (core->routers_by_mask_bit[router_maskbit] == 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "del_router: Deleting nonexistent router: %d", router_maskbit);
        return;
    }

    qdr_node_t    *rnode = core->routers_by_mask_bit[router_maskbit];
    qdr_address_t *oaddr = rnode->owning_addr;
    assert(oaddr);

    //
    // Unlink the router node from the address record
    //
    qdr_del_node_ref(&oaddr->rnodes, rnode);

    //
    // While the router node has a non-zero reference count, look for addresses
    // to unlink the node from.
    //
    qdr_address_t *addr = DEQ_HEAD(core->addrs);
    while (addr && rnode->ref_count > 0) {
        qdr_del_node_ref(&addr->rnodes, rnode);
        addr = DEQ_NEXT(addr);
    }
    assert(rnode->ref_count == 0);

    //
    // Free the router node and the owning address records.
    //
    qd_bitmask_free(rnode->valid_origins);
    DEQ_REMOVE(core->routers, rnode);
    free_qdr_node_t(rnode);

    qd_hash_remove_by_handle(core->addr_hash, oaddr->hash_handle);
    DEQ_REMOVE(core->addrs, oaddr);
    qd_hash_handle_free(oaddr->hash_handle);
    core->routers_by_mask_bit[router_maskbit] = 0;
    free_qdr_address_t(oaddr);
}


static void qdrh_set_link_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int router_maskbit = action->args.route_table.router_maskbit;
    int link_maskbit   = action->args.route_table.link_maskbit;

    if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_link: Router maskbit out of range: %d", router_maskbit);
        return;
    }

    if (link_maskbit >= qd_bitmask_width() || link_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_link: Link maskbit out of range: %d", link_maskbit);
        return;
    }

    if (core->out_links_by_mask_bit[link_maskbit] == 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_link: Invalid link reference: %d", link_maskbit);
        return;
    }

    if (core->routers_by_mask_bit[router_maskbit] == 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_link: Router not found");
        return;
    }

    //
    // Add the peer_link reference to the router record.
    //
    qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
    rnode->peer_link = core->out_links_by_mask_bit[link_maskbit];
}


static void qdrh_remove_link_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int router_maskbit = action->args.route_table.router_maskbit;

    if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "remove_link: Router maskbit out of range: %d", router_maskbit);
        return;
    }

    if (core->routers_by_mask_bit[router_maskbit] == 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "remove_link: Router not found");
        return;
    }

    qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
    rnode->peer_link = 0;
}


static void qdrh_set_next_hop_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int router_maskbit    = action->args.route_table.router_maskbit;
    int nh_router_maskbit = action->args.route_table.nh_router_maskbit;

    if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_next_hop: Router maskbit out of range: %d", router_maskbit);
        return;
    }

    if (nh_router_maskbit >= qd_bitmask_width() || nh_router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_next_hop: Next hop router maskbit out of range: %d", router_maskbit);
        return;
    }

    if (core->routers_by_mask_bit[router_maskbit] == 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_next_hop: Router not found");
        return;
    }

    if (core->routers_by_mask_bit[nh_router_maskbit] == 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_next_hop: Next hop router not found");
        return;
    }

    if (router_maskbit != nh_router_maskbit) {
        qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
        rnode->next_hop   = core->routers_by_mask_bit[nh_router_maskbit];
    }
}


static void qdrh_remove_next_hop_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int router_maskbit = action->args.route_table.router_maskbit;

    if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "remove_next_hop: Router maskbit out of range: %d", router_maskbit);
        return;
    }

    qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
    rnode->next_hop = 0;
}


static void qdrh_set_valid_origins_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int           router_maskbit = action->args.route_table.router_maskbit;
    qd_bitmask_t *valid_origins  = action->args.route_table.router_set;

    if (discard) {
        qd_bitmask_free(valid_origins);
        return;
    }

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "set_valid_origins: Router maskbit out of range: %d", router_maskbit);
            break;
        }

        if (core->routers_by_mask_bit[router_maskbit] == 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "set_valid_origins: Router not found");
            break;
        }

        qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
        if (rnode->valid_origins)
            qd_bitmask_free(rnode->valid_origins);
        rnode->valid_origins = valid_origins;
        valid_origins = 0;
    } while (false);

    if (valid_origins)
        qd_bitmask_free(valid_origins);
}


static void qdrh_map_destination_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    //
    // TODO - handle the class-prefix and phase explicitly
    //

    int          router_maskbit = action->args.route_table.router_maskbit;
    qdr_field_t *address        = action->args.route_table.address;

    if (discard) {
        qdr_field_free(address);
        return;
    }

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "map_destination: Router maskbit out of range: %d", router_maskbit);
            break;
        }

        if (core->routers_by_mask_bit[router_maskbit] == 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "map_destination: Router not found");
            break;
        }

        qd_field_iterator_t *iter = address->iterator;
        qdr_address_t       *addr = 0;

        qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
        if (!addr) {
            addr = qdr_address(action->args.route_table.semantics);
            qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_ITEM_INIT(addr);
            DEQ_INSERT_TAIL(core->addrs, addr);
        }

        qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
        qdr_add_node_ref(&addr->rnodes, rnode);

        //
        // TODO - If this affects a waypoint, create the proper side effects
        //
    } while (false);

    qdr_field_free(address);
}


static void qdrh_unmap_destination_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int          router_maskbit = action->args.route_table.router_maskbit;
    qdr_field_t *address        = action->args.route_table.address;

    if (discard) {
        qdr_field_free(address);
        return;
    }

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "unmap_destination: Router maskbit out of range: %d", router_maskbit);
            break;
        }

        if (core->routers_by_mask_bit[router_maskbit] == 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "unmap_destination: Router not found");
            break;
        }

        qdr_node_t          *rnode = core->routers_by_mask_bit[router_maskbit];
        qd_field_iterator_t *iter  = address->iterator;
        qdr_address_t       *addr  = 0;

        qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);

        if (!addr) {
            qd_log(core->log, QD_LOG_CRITICAL, "unmap_destination: Address not found");
            break;
        }

        qdr_del_node_ref(&addr->rnodes, rnode);

        //
        // TODO - If this affects a waypoint, create the proper side effects
        //

        qdr_check_addr_CT(core, addr, false);
    } while (false);

    qdr_field_free(address);
}


