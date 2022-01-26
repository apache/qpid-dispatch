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

static void qdr_add_router_CT          (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_del_router_CT          (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_set_link_CT            (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_remove_link_CT         (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_set_next_hop_CT        (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_remove_next_hop_CT     (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_set_cost_CT            (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_set_valid_origins_CT   (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_flush_destinations_CT  (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_mobile_seq_advanced_CT (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_subscribe_CT           (qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_unsubscribe_CT         (qdr_core_t *core, qdr_action_t *action, bool discard);


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


void qdr_core_flush_destinations(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdr_flush_destinations_CT, "flush_destinations");
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_mobile_seq_advanced(qdr_core_t *core, int router_maskbit)
{
    qdr_action_t *action = qdr_action(qdr_mobile_seq_advanced_CT, "mobile_seq_advanced");
    action->args.route_table.router_maskbit = router_maskbit;
    qdr_action_enqueue(core, action);
}


void qdr_core_route_table_handlers(qdr_core_t              *core, 
                                   void                    *context,
                                   qdr_set_mobile_seq_t     set_mobile_seq,
                                   qdr_set_my_mobile_seq_t  set_my_mobile_seq,
                                   qdr_link_lost_t          link_lost)
{
    core->rt_context           = context;
    core->rt_set_mobile_seq    = set_mobile_seq;
    core->rt_set_my_mobile_seq = set_my_mobile_seq;
    core->rt_link_lost         = link_lost;
}


qdr_subscription_t *qdr_core_subscribe(qdr_core_t             *core,
                                       const char             *address,
                                       char                    aclass,
                                       char                    phase,
                                       qd_address_treatment_t  treatment,
                                       bool                    in_core,
                                       qdr_receive_t           on_message,
                                       void                   *context)
{
    qdr_subscription_t *sub = NEW(qdr_subscription_t);
    sub->core               = core;
    sub->addr               = 0;
    sub->on_message         = on_message;
    sub->on_message_context = context;
    sub->in_core            = in_core;

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
    core->addr_hash       = qd_hash(12, 32, 0);
    core->addr_lr_al_hash = qd_hash(12, 32, 0);


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
        core->rnode_conns_by_mask_bit   = NEW_PTR_ARRAY(qdr_connection_t, qd_bitmask_width());
        core->data_links_by_mask_bit    = NEW_ARRAY(qdr_priority_sheaf_t, qd_bitmask_width());
        for (int idx = 0; idx < qd_bitmask_width(); idx++) {
            core->routers_by_mask_bit[idx]   = 0;
            core->control_links_by_mask_bit[idx] = 0;
            core->data_links_by_mask_bit[idx].count = 0;
            core->rnode_conns_by_mask_bit[idx] = 0;
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
        // Bump the address's ref_count for the time that it is associated with an existing remote router node.
        //
        addr->ref_count++;

        //
        // Create a router-node record to represent the remote router.
        //
        qdr_node_t *rnode = new_qdr_node_t();
        ZERO(rnode);
        rnode->owning_addr       = addr;
        rnode->mask_bit          = router_maskbit;
        rnode->conn_mask_bit     = -1;
        rnode->valid_origins     = qd_bitmask(0);

        qd_iterator_reset_view(iter, ITER_VIEW_ALL);
        int addr_len = qd_iterator_length(iter);

        rnode->wire_address_ma = (char*) malloc(addr_len + 4);
        qd_iterator_ncopy(iter, (unsigned char*) rnode->wire_address_ma, addr_len);
        strcpy(rnode->wire_address_ma + addr_len, ".ma");

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
    if (!!discard)
        return;

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
    oaddr->ref_count--;
    qdr_check_addr_CT(core, oaddr);
}


static void qdr_set_link_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (!!discard)
        return;

    int router_maskbit = action->args.route_table.router_maskbit;
    int conn_maskbit   = action->args.route_table.link_maskbit;  // "link" identifies a connection, not an amqp link

    if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_link: Router maskbit out of range: %d", router_maskbit);
        return;
    }

    if (conn_maskbit >= qd_bitmask_width() || conn_maskbit < 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_link: Link maskbit out of range: %d", conn_maskbit);
        return;
    }

    if (core->control_links_by_mask_bit[conn_maskbit] == 0) {
        qd_log(core->log, QD_LOG_CRITICAL, "set_link: Invalid link reference: %d", conn_maskbit);
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
    rnode->conn_mask_bit = conn_maskbit;
    qdr_addr_start_inlinks_CT(core, rnode->owning_addr);
}


static void qdr_remove_link_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (!!discard)
        return;

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
    rnode->conn_mask_bit = -1;
}


static void qdr_set_next_hop_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (!!discard)
        return;

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
    if (!!discard)
        return;

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
    if (!!discard)
        return;

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


static void qdr_flush_destinations_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (!!discard)
        return;

    int router_maskbit = action->args.route_table.router_maskbit;

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "flush_destinations: Router maskbit out of range: %d", router_maskbit);
            break;
        }

        qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
        if (rnode == 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "flush_destinations: Router not found");
            break;
        }

        //
        // Raise the event to be picked up by core modules.
        //
        qdrc_event_router_raise(core, QDRC_EVENT_ROUTER_MOBILE_FLUSH, rnode);
    } while (false);
}


static void qdr_mobile_seq_advanced_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (!!discard)
        return;

    int router_maskbit = action->args.route_table.router_maskbit;

    do {
        if (router_maskbit >= qd_bitmask_width() || router_maskbit < 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "seq_advanced: Router maskbit out of range: %d", router_maskbit);
            break;
        }

        qdr_node_t *rnode = core->routers_by_mask_bit[router_maskbit];
        if (rnode == 0) {
            qd_log(core->log, QD_LOG_CRITICAL, "seq_advanced: Router not found");
            break;
        }

        //
        // Raise the event to be picked up by core modules.
        //
        qdrc_event_router_raise(core, QDRC_EVENT_ROUTER_MOBILE_SEQ_ADVANCED, rnode);
    } while (false);
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
    }

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

static void qdr_do_set_mobile_seq(qdr_core_t *core, qdr_general_work_t *work)
{
    core->rt_set_mobile_seq(core->rt_context, work->maskbit, work->mobile_seq);
}


static void qdr_do_set_my_mobile_seq(qdr_core_t *core, qdr_general_work_t *work)
{
    core->rt_set_my_mobile_seq(core->rt_context, work->mobile_seq);
}


static void qdr_do_link_lost(qdr_core_t *core, qdr_general_work_t *work)
{
    core->rt_link_lost(core->rt_context, work->maskbit);
}


void qdr_post_set_mobile_seq_CT(qdr_core_t *core, int router_maskbit, uint64_t mobile_seq)
{
    qdr_general_work_t *work = qdr_general_work(qdr_do_set_mobile_seq);
    work->mobile_seq = mobile_seq;
    work->maskbit    = router_maskbit;
    qdr_post_general_work_CT(core, work);
}


void qdr_post_set_my_mobile_seq_CT(qdr_core_t *core, uint64_t mobile_seq)
{
    qdr_general_work_t *work = qdr_general_work(qdr_do_set_my_mobile_seq);
    work->mobile_seq = mobile_seq;
    qdr_post_general_work_CT(core, work);
}


void qdr_post_link_lost_CT(qdr_core_t *core, int link_maskbit)
{
    qdr_general_work_t *work = qdr_general_work(qdr_do_link_lost);
    work->maskbit = link_maskbit;
    qdr_post_general_work_CT(core, work);
}


