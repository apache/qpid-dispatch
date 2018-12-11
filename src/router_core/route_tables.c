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
#include "route_control.h"
#include <stdio.h>

static void qdr_add_router_CT        (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_del_router_CT        (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_set_link_CT          (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_remove_link_CT       (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_set_next_hop_CT      (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_remove_next_hop_CT   (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_set_cost_CT          (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_set_valid_origins_CT (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_map_destination_CT   (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_unmap_destination_CT (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_subscribe_CT         (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_unsubscribe_CT       (qdr_core_t *core, qdr_action_t *action, bool discard);


//==================================================================================
// Interface Functions
//==================================================================================

void qdr_core_add_router(qdr_core_t *core, const char *address, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdr_add_router_CT, "add_router");
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address);
    qdr_action_enqueue(core, action);
}


void qdr_core_del_router(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdr_del_router_CT, "del_router");
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_link(qdr_core_t *core, int router_maskbit, int link_maskbit)
{
    qdr_action_t *action = qdr_action(qdr_set_link_CT, "set_link");
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.link_maskbit   = link_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_remove_link(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdr_remove_link_CT, "remove_link");
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_next_hop(qdr_core_t *core, int router_maskbit, int nh_router_maskbit)
{
    qdr_action_t *action = qdr_action(qdr_set_next_hop_CT, "set_next_hop");
    action->args.route_table.router_maskbit    = router_maskbit;
    action->args.route_table.nh_router_maskbit = nh_router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_remove_next_hop(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdr_remove_next_hop_CT, "remove_next_hop");
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_cost(qdr_core_t *core, int router_maskbit, int cost)
{
    qdr_action_t *action = qdr_action(qdr_set_cost_CT, "set_cost");
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.cost           = cost;
    qdr_action_enqueue(core, action);
}


void qdr_core_set_valid_origins(qdr_core_t *core, int router_maskbit, qd_bitmask_t *routers)
{
    qdr_action_t *action = qdr_action(qdr_set_valid_origins_CT, "set_valid_origins");
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.router_set     = routers;
    qdr_action_enqueue(core, action);
}


void qdr_core_map_destination(qdr_core_t *core, int router_maskbit, const char *address_hash, int treatment_hint)
{
    qdr_action_t *action = qdr_action(qdr_map_destination_CT, "map_destination");
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address_hash);
    action->args.route_table.treatment_hint = treatment_hint;
    qdr_action_enqueue(core, action);
}


void qdr_core_unmap_destination(qdr_core_t *core, int router_maskbit, const char *address_hash)
{
    qdr_action_t *action = qdr_action(qdr_unmap_destination_CT, "unmap_destination");
    action->args.route_table.router_maskbit = router_maskbit;
    action->args.route_table.address        = qdr_field(address_hash);
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


qdr_subscription_t *qdr_core_subscribe(qdr_core_t             *core,
                                       const char             *address,
                                       char                    aclass,
                                       char                    phase,
                                       qd_address_treatment_t  treatment,
                                       qdr_receive_t           on_message,
                                       void                   *context)
{
    qdr_subscription_t *sub = NEW(qdr_subscription_t);
    sub->core               = core;
    sub->addr               = 0;
    sub->on_message         = on_message;
    sub->on_message_context = context;

    qdr_action_t *action = qdr_action(qdr_subscribe_CT, "subscribe");
    action->args.io.address       = qdr_field(address);
    action->args.io.address_class = aclass;
    action->args.io.address_phase = phase;
    action->args.io.subscription  = sub;
    action->args.io.treatment     = treatment;
    qdr_action_enqueue(core, action);

    return sub;
}


void qdr_core_unsubscribe(qdr_subscription_t *sub)
{
    if (sub) {
        qdr_action_t *action = qdr_action(qdr_unsubscribe_CT, "unsubscribe");
        action->args.io.subscription = sub;
        qdr_action_enqueue(sub->core, action);
    }
}


//==================================================================================
// In-Thread Functions
//==================================================================================

//
// React to the updated cost of a router node.  The core->routers list is to be kept
// sorted by cost, from least to most.
//
void qdr_route_table_update_cost_CT(qdr_core_t *core, qdr_node_t *rnode)
{
    qdr_node_t *ptr;
    bool needs_reinsertion = false;

    ptr = DEQ_PREV(rnode);
    if (ptr && ptr->cost > rnode->cost)
        needs_reinsertion = true;
    else {
        ptr = DEQ_NEXT(rnode);
        if (ptr && ptr->cost < rnode->cost)
            needs_reinsertion = true;
    }

    if (needs_reinsertion) {
        core->cost_epoch++;
        DEQ_REMOVE(core->routers, rnode);
        ptr = DEQ_TAIL(core->routers);
        while (ptr) {
            if (rnode->cost >= ptr->cost) {
                DEQ_INSERT_AFTER(core->routers, rnode, ptr);
                break;
            }
            ptr = DEQ_PREV(ptr);
        }

        if (!ptr)
            DEQ_INSERT_HEAD(core->routers, rnode);
    }
}


void qdr_route_table_setup_CT(qdr_core_t *core)
{
    DEQ_INIT(core->addrs);
    DEQ_INIT(core->routers);
    core->addr_hash    = qd_hash(12, 32, 0);
    core->conn_id_hash = qd_hash(6, 4, 0);
    core->cost_epoch   = 1;
    core->addr_parse_tree = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    core->link_route_tree[QD_INCOMING] = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    core->link_route_tree[QD_OUTGOING] = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);

    if (core->router_mode == QD_ROUTER_MODE_INTERIOR) {
        core->hello_addr      = qdr_add_local_address_CT(core, 'L', "qdhello",     QD_TREATMENT_MULTICAST_FLOOD);
        core->router_addr_L   = qdr_add_local_address_CT(core, 'L', "qdrouter",    QD_TREATMENT_MULTICAST_FLOOD);
        core->routerma_addr_L = qdr_add_local_address_CT(core, 'L', "qdrouter.ma", QD_TREATMENT_MULTICAST_ONCE);
        core->router_addr_T   = qdr_add_local_address_CT(core, 'T', "qdrouter",    QD_TREATMENT_MULTICAST_FLOOD);
        core->routerma_addr_T = qdr_add_local_address_CT(core, 'T', "qdrouter.ma", QD_TREATMENT_MULTICAST_ONCE);

        core->hello_addr->router_control_only      = true;
        core->router_addr_L->router_control_only   = true;
        core->routerma_addr_L->router_control_only = true;
        core->router_addr_T->router_control_only   = true;
        core->routerma_addr_T->router_control_only = true;

        core->neighbor_free_mask = qd_bitmask(1);

        core->routers_by_mask_bit       = NEW_PTR_ARRAY(qdr_node_t, qd_bitmask_width());
        core->control_links_by_mask_bit = NEW_PTR_ARRAY(qdr_link_t, qd_bitmask_width());
        core->data_links_by_mask_bit    = NEW_ARRAY(qdr_priority_sheaf_t, qd_bitmask_width());
        for (int idx = 0; idx < qd_bitmask_width(); idx++) {
            core->routers_by_mask_bit[idx]   = 0;
            core->control_links_by_mask_bit[idx] = 0;
            core->data_links_by_mask_bit[idx].count = 0;
            for (int priority = 0; priority < QDR_N_PRIORITIES; ++ priority)
              core->data_links_by_mask_bit[idx].links[priority] = 0;

        }
    }
}


static void qdr_add_router_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
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
        qd_iterator_t *iter = address->iterator;
        qdr_address_t *addr;

        qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
        qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);

        if (!addr) {
            //
            // Create an address record for this router and insert it in the hash table.
            // This record will be found whenever a "foreign" topological address to this
            // remote router is looked up.
            //
            addr = qdr_address_CT(core, QD_TREATMENT_ANYCAST_CLOSEST, 0);
            qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_INSERT_TAIL(core->addrs, addr);
        }

        //
        // Set the block-deletion flag on this address for the time that it is associated
        // with an existing remote router node.
        //
        addr->block_deletion = true;

        //
        // Create a router-node record to represent the remote router.
        //
        qdr_node_t *rnode = new_qdr_node_t();
        DEQ_ITEM_INIT(rnode);
        rnode->owning_addr       = addr;
        rnode->mask_bit          = router_maskbit;
        rnode->next_hop          = 0;
        rnode->link_mask_bit     = -1;
        rnode->ref_count         = 0;
        rnode->valid_origins     = qd_bitmask(0);
        rnode->cost              = 0;

        //
        // Insert at the head of the list because we don't yet know the cost to this
        // router node and we've set the cost to zero.  This puts it in a properly-sorted
        // position.  Also, don't bump the cost_epoch here because this new router won't be
        // used until it is assigned a cost.
        //
        DEQ_INSERT_HEAD(core->routers, rnode);

        //
        // Link the router record to the address record.
        //
        qd_bitmask_set_bit(addr->rnodes, router_maskbit);

        //
        // Link the router record to the router address records.
        // Use the T-class addresses only.
        //
        qd_bitmask_set_bit(core->router_addr_T->rnodes, router_maskbit);
        qd_bitmask_set_bit(core->routerma_addr_T->rnodes, router_maskbit);

        //
        // Bump the ref-count by three for each of the above links.
        //
        rnode->ref_count += 3;

        //
        // Add the router record to the mask-bit index.
        //
        core->routers_by_mask_bit[router_maskbit] = rnode;
    } while (false);

    qdr_field_free(address);
}


static void qdr_del_router_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
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
    qd_bitmask_clear_bit(oaddr->rnodes, router_maskbit);
    qd_bitmask_clear_bit(core->router_addr_T->rnodes, router_maskbit);
    qd_bitmask_clear_bit(core->routerma_addr_T->rnodes, router_maskbit);
    rnode->ref_count -= 3;

    //
    // While the router node has a non-zero reference count, look for addresses
    // to unlink the node from.
    //
    qdr_address_t *addr = DEQ_HEAD(core->addrs);
    while (addr && rnode->ref_count > 0) {
        if (qd_bitmask_clear_bit(addr->rnodes, router_maskbit))
            //
            // If the cleared bit was originally set, decrement the ref count
            //
            rnode->ref_count--;
        addr = DEQ_NEXT(addr);
    }
    assert(rnode->ref_count == 0);

    //
    // Free the router node.
    //
    qdr_router_node_free(core, rnode);

    //
    // Check the address and free it if there are no other interested parties tracking it
    //
    oaddr->block_deletion = false;
    qdr_check_addr_CT(core, oaddr);
}


static void qdr_set_link_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
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

    if (core->control_links_by_mask_bit[link_maskbit] == 0) {
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
    rnode->link_mask_bit = link_maskbit;
    qdr_addr_start_inlinks_CT(core, rnode->owning_addr);
}


static void qdr_remove_link_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
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
    rnode->link_mask_bit = -1;
}


static void qdr_set_next_hop_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
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
        qdr_addr_start_inlinks_CT(core, rnode->owning_addr);
    }
}


static void qdr_remove_next_hop_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int router_maskbit = action->args.route_table.router_maskbit;

    if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "remove_next_hop: Router maskbit out of range: %d", router_maskbit);
        return;
    }

    qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
    rnode->next_hop = 0;
}


static void qdr_set_cost_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int router_maskbit = action->args.route_table.router_maskbit;
    int cost           = action->args.route_table.cost;

    if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_cost: Router maskbit out of range: %d", router_maskbit);
        return;
    }

    if (cost < 1) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_cost: Invalid cost %d for maskbit: %d", cost, router_maskbit);
        return;
    }

    qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
    rnode->cost = cost;
    qdr_route_table_update_cost_CT(core, rnode);
}


static void qdr_set_valid_origins_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
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

static qd_address_treatment_t default_treatment(qdr_core_t *core, int hint) {
    switch (hint) {
    case QD_TREATMENT_MULTICAST_FLOOD:
        return QD_TREATMENT_MULTICAST_FLOOD;
    case QD_TREATMENT_MULTICAST_ONCE:
        return QD_TREATMENT_MULTICAST_ONCE;
    case QD_TREATMENT_ANYCAST_CLOSEST:
        return QD_TREATMENT_ANYCAST_CLOSEST;
    case QD_TREATMENT_ANYCAST_BALANCED:
        return QD_TREATMENT_ANYCAST_BALANCED;
    case QD_TREATMENT_LINK_BALANCED:
        return QD_TREATMENT_LINK_BALANCED;
    case QD_TREATMENT_UNAVAILABLE:
        return QD_TREATMENT_UNAVAILABLE;
    default:
        return core->qd->default_treatment == QD_TREATMENT_UNAVAILABLE ? QD_TREATMENT_ANYCAST_BALANCED : core->qd->default_treatment;
    }
}

static void qdr_map_destination_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    int          router_maskbit = action->args.route_table.router_maskbit;
    qdr_field_t *address        = action->args.route_table.address;
    int          treatment_hint = action->args.route_table.treatment_hint;

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

        qd_iterator_t *iter = address->iterator;
        qdr_address_t *addr = 0;

        qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
        if (!addr) {
            qdr_address_config_t   *addr_config;
            qd_address_treatment_t  treatment = qdr_treatment_for_address_hash_with_default_CT(core,
                                                                                               iter,
                                                                                               default_treatment(core, treatment_hint),
                                                                                               &addr_config);
            addr = qdr_address_CT(core, treatment, addr_config);
            if (!addr) {
                qd_log(core->log, QD_LOG_CRITICAL, "map_destination: ignored");
                break;
            }
            qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_ITEM_INIT(addr);
            DEQ_INSERT_TAIL(core->addrs, addr);
            // if the address is a link route, add the pattern to the wildcard
            // address parse tree
            {
                const char *a_str = (const char *)qd_hash_key_by_handle(addr->hash_handle);
                if (QDR_IS_LINK_ROUTE(a_str[0])) {
                    qdr_link_route_map_pattern_CT(core, iter, addr);
                }
            }
        }

        qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
        qd_bitmask_set_bit(addr->rnodes, router_maskbit);
        rnode->ref_count++;
        addr->cost_epoch--;
        qdr_addr_start_inlinks_CT(core, addr);

        //
        // Raise an address event if this is the first destination for the address
        //
        if (qd_bitmask_cardinality(addr->rnodes) + DEQ_SIZE(addr->rlinks) == 1)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_BECAME_DEST, addr);
        else if (qd_bitmask_cardinality(addr->rnodes) == 1 && DEQ_SIZE(addr->rlinks) == 1)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_TWO_DEST, addr);
    } while (false);

    qdr_field_free(address);
}


static void qdr_unmap_destination_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
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

        qdr_node_t    *rnode = core->routers_by_mask_bit[router_maskbit];
        qd_iterator_t *iter  = address->iterator;
        qdr_address_t *addr  = 0;

        qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
        if (!addr) {
            qd_log(core->log, QD_LOG_CRITICAL, "unmap_destination: Address not found");
            break;
        }
        
        qd_bitmask_clear_bit(addr->rnodes, router_maskbit);
        rnode->ref_count--;
        addr->cost_epoch--;

        //
        // Raise an address event if this was the last destination for the address
        //
        if (qd_bitmask_cardinality(addr->rnodes) + DEQ_SIZE(addr->rlinks) == 0)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_NO_LONGER_DEST, addr);
        else if (qd_bitmask_cardinality(addr->rnodes) == 0 && DEQ_SIZE(addr->rlinks) == 1)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_ONE_LOCAL_DEST, addr);

        qdr_check_addr_CT(core, addr);
    } while (false);

    qdr_field_free(address);
}


static void qdr_subscribe_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_field_t        *address = action->args.io.address;
    qdr_subscription_t *sub     = action->args.io.subscription;

    if (!discard) {
        char aclass         = action->args.io.address_class;
        char phase          = action->args.io.address_phase;
        qdr_address_t *addr = 0;

        char *astring = (char*) qd_iterator_copy(address->iterator);
        qd_log(core->log, QD_LOG_INFO, "In-process subscription %c/%s", aclass, astring);
        free(astring);

        qd_iterator_annotate_prefix(address->iterator, aclass);
        if (aclass == 'M')
            qd_iterator_annotate_phase(address->iterator, phase);
        qd_iterator_reset_view(address->iterator, ITER_VIEW_ADDRESS_HASH);

        qd_hash_retrieve(core->addr_hash, address->iterator, (void**) &addr);
        if (!addr) {
            addr = qdr_address_CT(core, action->args.io.treatment, 0);
            if (addr) {
                qd_hash_insert(core->addr_hash, address->iterator, addr, &addr->hash_handle);
                DEQ_ITEM_INIT(addr);
                DEQ_INSERT_TAIL(core->addrs, addr);
            }
        }
        if (addr) {
            sub->addr = addr;
            DEQ_ITEM_INIT(sub);
            DEQ_INSERT_TAIL(addr->subscriptions, sub);
            qdr_addr_start_inlinks_CT(core, addr);
        }
    } else
        free(sub);

    qdr_field_free(address);
}


static void qdr_unsubscribe_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_subscription_t *sub = action->args.io.subscription;

    if (!discard) {
        DEQ_REMOVE(sub->addr->subscriptions, sub);
        sub->addr = 0;
        qdr_check_addr_CT(sub->core, sub->addr);
    }

    free(sub);
}

//==================================================================================
// Call-back Functions
//==================================================================================

static void qdr_do_mobile_added(qdr_core_t *core, qdr_general_work_t *work)
{
    char *address_hash = qdr_field_copy(work->field);
    if (address_hash) {
        core->rt_mobile_added(core->rt_context, address_hash, work->treatment);
        free(address_hash);
    }

    qdr_field_free(work->field);
}


static void qdr_do_mobile_removed(qdr_core_t *core, qdr_general_work_t *work)
{
    char *address_hash = qdr_field_copy(work->field);
    if (address_hash) {
        core->rt_mobile_removed(core->rt_context, address_hash);
        free(address_hash);
    }

    qdr_field_free(work->field);
}


static void qdr_do_link_lost(qdr_core_t *core, qdr_general_work_t *work)
{
    core->rt_link_lost(core->rt_context, work->maskbit);
}


void qdr_post_mobile_added_CT(qdr_core_t *core, const char *address_hash, qd_address_treatment_t treatment)
{
    qdr_general_work_t *work = qdr_general_work(qdr_do_mobile_added);
    work->field = qdr_field(address_hash);
    work->treatment = treatment;
    qdr_post_general_work_CT(core, work);
}


void qdr_post_mobile_removed_CT(qdr_core_t *core, const char *address_hash)
{
    qdr_general_work_t *work = qdr_general_work(qdr_do_mobile_removed);
    work->field = qdr_field(address_hash);
    qdr_post_general_work_CT(core, work);
}


void qdr_post_link_lost_CT(qdr_core_t *core, int link_maskbit)
{
    qdr_general_work_t *work = qdr_general_work(qdr_do_link_lost);
    work->maskbit = link_maskbit;
    qdr_post_general_work_CT(core, work);
}


