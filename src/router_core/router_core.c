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

#include "core_events.h"
#include "delivery.h"
#include "exchange_bindings.h"
#include "route_control.h"
#include "router_core_private.h"

#include <stdio.h>
#include <strings.h>

ALLOC_DECLARE(qdr_link_work_t);

ALLOC_DEFINE(qdr_address_t);
ALLOC_DEFINE(qdr_address_config_t);
ALLOC_DEFINE(qdr_node_t);
ALLOC_DEFINE(qdr_delivery_ref_t);
ALLOC_DEFINE(qdr_link_t);
ALLOC_DEFINE(qdr_router_ref_t);
ALLOC_DEFINE(qdr_link_ref_t);
ALLOC_DEFINE(qdr_delivery_cleanup_t);
ALLOC_DEFINE(qdr_general_work_t);
ALLOC_DEFINE(qdr_link_work_t);
ALLOC_DEFINE(qdr_connection_ref_t);
ALLOC_DEFINE(qdr_connection_info_t);
ALLOC_DEFINE(qdr_subscription_ref_t);

const uint64_t QD_DELIVERY_MOVED_TO_NEW_LINK = 999999999;

static void qdr_general_handler(void *context);

static void qdr_core_setup_init(qdr_core_t *core)
{
    //
    // DISPATCH-1867: These functions used to be called inside the router_core_thread() function in router_core_thread.c
    // which meant they were executed asynchronously by the core thread which meant qd_router_setup_late() could
    // return before these functions executed in the core thread. But we need the adaptors and modules to be initialized *before* qd_router_setup_late() completes.
    //
    qdr_forwarder_setup_CT(core);
    qdr_route_table_setup_CT(core);

    //
    // Initialize the core modules
    //
    qdr_modules_init(core);

    //
    // Initialize all registered adaptors.
    //
    qdr_adaptors_init(core);
}

qdr_core_t *qdr_core(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id)
{
    qdr_core_t *core = NEW(qdr_core_t);
    ZERO(core);

    core->qd                  = qd;
    core->router_mode         = mode;
    core->router_area         = area;
    core->router_id           = id;
    core->worker_thread_count = qd->thread_count;
    sys_atomic_init(&core->uptime_ticks, 0);

    DEQ_INIT(core->exchanges);

    //
    // Set up the logging sources for the router core. The core
    // module logs to the ROUTER_CORE module. There is no need to free the core->log as all log sources are.
    // freed by qd_dispatch_free()
    //
    core->log = qd_log_source("ROUTER_CORE");

    //
    // Set up the threading support
    //
    core->action_cond = sys_cond();
    core->action_lock = sys_mutex();
    core->running     = true;
    DEQ_INIT(core->action_list);
    DEQ_INIT(core->action_list_background);

    core->work_lock = sys_mutex();
    DEQ_INIT(core->work_list);
    core->work_timer = qd_timer(core->qd, qdr_general_handler, core);

    //
    // Set up the unique identifier generator
    //
    core->next_identifier = 1;
    core->id_lock = sys_mutex();

    //
    // Initialize the management agent
    //
    core->mgmt_agent = qdr_agent(core);

    //
    // Setup and initialize modules, adaptors, address table etc. so we can have everything initialized and ready to
    // go when the core thread starts handling actions.
    //
    qdr_core_setup_init(core);

    //
    // Launch the core thread
    //
    core->thread = sys_thread(router_core_thread, core);

    //
    // Setup the agents subscriptions to $management
    //
    qdr_agent_setup_subscriptions(core->mgmt_agent, core);

    return core;
}


void qdr_core_free(qdr_core_t *core)
{
    //
    // Stop and join the thread
    //
    core->running = false;

    sys_cond_signal(core->action_cond);
    sys_thread_join(core->thread);

    // have adaptors clean up all core resources
    qdr_adaptors_finalize(core);

    //
    // The char* core->router_id and core->router_area are owned by qd->router_id and qd->router_area respectively
    // We will set them to zero here just in case anybody tries to use these fields.
    //
    core->router_id = 0;
    core->router_area = 0;

    //
    // Free the core resources
    //
    for (int i = 0; i <= QD_TREATMENT_LINK_BALANCED; ++i) {
        if (core->forwarders[i]) {
            free(core->forwarders[i]);
        }
    }

    qdr_link_route_t *link_route = 0;
    while ( (link_route = DEQ_HEAD(core->link_routes))) {
        DEQ_REMOVE_HEAD(core->link_routes);
        qdr_core_delete_link_route(core, link_route);
    }

    //
    // The connection based link routes need to be removed before we call
    // qdr_core_remove_address(addr) on all core->addrs
    //
    qdr_connection_t *conn = DEQ_HEAD(core->open_connections);
    while (conn) {
        while ((link_route = DEQ_HEAD(conn->conn_link_routes))) {
            DEQ_REMOVE_HEAD(conn->conn_link_routes);
            qdr_core_delete_link_route(core, link_route);
        }
        conn = DEQ_NEXT(conn);
    }

    qdr_auto_link_t *auto_link = 0;
    while ( (auto_link = DEQ_HEAD(core->auto_links))) {
        DEQ_REMOVE_HEAD(core->auto_links);
        qdr_core_delete_auto_link(core, auto_link);
    }

    qdr_exchange_free_all(core);

    qdr_address_t *addr = 0;
    while ( (addr = DEQ_HEAD(core->addrs)) ) {
        qdr_core_remove_address(core, addr);
    }
    qdr_address_config_t *addr_config = 0;
    while ( (addr_config = DEQ_HEAD(core->addr_config))) {
        qdr_core_remove_address_config(core, addr_config);
    }

    qd_hash_free(core->addr_hash);
    qd_hash_free(core->addr_lr_al_hash);

    qd_parse_tree_free(core->addr_parse_tree);
    qd_parse_tree_free(core->link_route_tree[QD_INCOMING]);
    qd_parse_tree_free(core->link_route_tree[QD_OUTGOING]);

    qdr_node_t *rnode = 0;
    while ( (rnode = DEQ_HEAD(core->routers)) ) {
        qdr_router_node_free(core, rnode);
    }

    qdr_link_t *link = DEQ_HEAD(core->open_links);
    while (link) {
        DEQ_REMOVE_HEAD(core->open_links);
        if (link->in_streaming_pool) {
            DEQ_REMOVE_N(STREAMING_POOL, link->conn->streaming_link_pool, link);
            link->in_streaming_pool = false;
        }

        qdr_link_cleanup_deliveries_CT(core, link->conn, link, true);

        if (link->core_endpoint)
            qdrc_endpoint_do_cleanup_CT(core, link->core_endpoint);

        qdr_del_link_ref(&link->conn->links, link, QDR_LINK_LIST_CLASS_CONNECTION);
        qdr_del_link_ref(&link->conn->links_with_work[link->priority], link, QDR_LINK_LIST_CLASS_WORK);
        free(link->name);
        free(link->disambiguated_name);
        free(link->terminus_addr);
        free(link->ingress_histogram);
        free(link->insert_prefix);
        free(link->strip_prefix);
        link->name = 0;

        //
        // If there are still any work items remaining in the link->work_list
        // remove them and free the associated link_work->error
        //
        sys_mutex_lock(link->conn->work_lock);
        qdr_link_work_t *link_work = DEQ_HEAD(link->work_list);
        while (link_work) {
            DEQ_REMOVE_HEAD(link->work_list);
            qdr_link_work_release(link_work);
            link_work = DEQ_HEAD(link->work_list);
        }
        sys_mutex_unlock(link->conn->work_lock);
        if (link->user_context) {
            qdr_link_set_context(link, 0);
        }
        free_qdr_link_t(link);
        link = DEQ_HEAD(core->open_links);
    }

    // finalize modules while we can still submit new actions
    // this must happen after qdrc_endpoint_do_cleanup_CT calls
    qdr_modules_finalize(core);

    // discard any left over actions

    qdr_action_list_t  action_list;
    DEQ_MOVE(core->action_list, action_list);
    DEQ_APPEND(action_list, core->action_list_background);
    qdr_action_t *action = DEQ_HEAD(action_list);
    while (action) {
        DEQ_REMOVE_HEAD(action_list);
        action->action_handler(core, action, true);
        free_qdr_action_t(action);
        action = DEQ_HEAD(action_list);
    }

    // Drain the general work lists
    qdr_general_handler(core);

    sys_thread_free(core->thread);
    sys_cond_free(core->action_cond);
    sys_mutex_free(core->action_lock);
    sys_mutex_free(core->work_lock);
    sys_mutex_free(core->id_lock);
    qd_timer_free(core->work_timer);

    //
    // Clean up any qdr_delivery_cleanup_t's that are still left in the core->delivery_cleanup_list
    //
    qdr_delivery_cleanup_t *cleanup = DEQ_HEAD(core->delivery_cleanup_list);
    while (cleanup) {
        DEQ_REMOVE_HEAD(core->delivery_cleanup_list);
        if (cleanup->msg)
            qd_message_free(cleanup->msg);
        if (cleanup->iter)
            qd_iterator_free(cleanup->iter);
        free_qdr_delivery_cleanup_t(cleanup);
        cleanup = DEQ_HEAD(core->delivery_cleanup_list);
    }

    conn = DEQ_HEAD(core->open_connections);
    while (conn) {
        DEQ_REMOVE_HEAD(core->open_connections);

        if (conn->conn_id) {
            qdr_del_connection_ref(&conn->conn_id->connection_refs, conn);
            qdr_route_check_id_for_deletion_CT(core, conn->conn_id);
        }

        if (conn->alt_conn_id) {
            qdr_del_connection_ref(&conn->alt_conn_id->connection_refs, conn);
            qdr_route_check_id_for_deletion_CT(core, conn->alt_conn_id);
        }

        qdr_connection_work_t *work = DEQ_HEAD(conn->work_list);
        while (work) {
            DEQ_REMOVE_HEAD(conn->work_list);
            qdr_connection_work_free_CT(work);
            work = DEQ_HEAD(conn->work_list);
        }

        if (conn->has_streaming_links) {
            assert(DEQ_IS_EMPTY(conn->streaming_link_pool));  // all links have been released
            qdr_del_connection_ref(&core->streaming_connections, conn);
        }

        qdr_connection_free(conn);
        conn = DEQ_HEAD(core->open_connections);
    }
    assert(DEQ_SIZE(core->streaming_connections) == 0);

    // at this point all the conn identifiers have been freed
    qd_hash_free(core->conn_id_hash);

    qdr_agent_free(core->mgmt_agent);

    if (core->routers_by_mask_bit)       free(core->routers_by_mask_bit);
    if (core->control_links_by_mask_bit) free(core->control_links_by_mask_bit);
    if (core->data_links_by_mask_bit)    free(core->data_links_by_mask_bit);
    if (core->neighbor_free_mask)        qd_bitmask_free(core->neighbor_free_mask);
    if (core->rnode_conns_by_mask_bit)   free(core->rnode_conns_by_mask_bit);

    free(core);
}

void qdr_router_node_free(qdr_core_t *core, qdr_node_t *rnode)
{
    qd_bitmask_free(rnode->valid_origins);
    DEQ_REMOVE(core->routers, rnode);
    core->routers_by_mask_bit[rnode->mask_bit] = 0;
    core->cost_epoch++;
    free(rnode->wire_address_ma);
    free_qdr_node_t(rnode);
}

ALLOC_DECLARE(qdr_field_t);
ALLOC_DEFINE(qdr_field_t);

qdr_field_t *qdr_field(const char *text)
{
    size_t length  = text ? strlen(text) : 0;
    size_t ilength = length;

    if (length == 0)
        return 0;

    qdr_field_t *field = new_qdr_field_t();
    qd_buffer_t *buf;

    ZERO(field);
    while (length > 0) {
        buf = qd_buffer();
        size_t cap  = qd_buffer_capacity(buf);
        size_t copy = length > cap ? cap : length;
        memcpy(qd_buffer_cursor(buf), text, copy);
        qd_buffer_insert(buf, copy);
        length -= copy;
        text   += copy;
        DEQ_INSERT_TAIL(field->buffers, buf);
    }

    field->iterator = qd_iterator_buffer(DEQ_HEAD(field->buffers), 0, ilength, ITER_VIEW_ALL);

    return field;
}


qdr_field_t *qdr_field_from_iter(qd_iterator_t *iter)
{
    if (!iter)
        return 0;

    qdr_field_t *field = new_qdr_field_t();
    qd_buffer_t *buf;
    int          remaining;
    int          length;

    ZERO(field);
    qd_iterator_reset(iter);
    remaining = qd_iterator_remaining(iter);
    length    = remaining;
    while (remaining) {
        buf = qd_buffer();
        size_t cap    = qd_buffer_capacity(buf);
        int    copied = qd_iterator_ncopy(iter, qd_buffer_cursor(buf), cap);
        qd_buffer_insert(buf, copied);
        DEQ_INSERT_TAIL(field->buffers, buf);
        remaining = qd_iterator_remaining(iter);
    }

    field->iterator = qd_iterator_buffer(DEQ_HEAD(field->buffers), 0, length, ITER_VIEW_ALL);

    return field;
}

qd_iterator_t *qdr_field_iterator(qdr_field_t *field)
{
    if (!field)
        return 0;

    return field->iterator;
}


void qdr_field_free(qdr_field_t *field)
{
    if (field) {
        qd_iterator_free(field->iterator);
        qd_buffer_list_free_buffers(&field->buffers);
        free_qdr_field_t(field);
    }
}


char *qdr_field_copy(qdr_field_t *field)
{
    if (!field || !field->iterator)
        return 0;

    return (char*) qd_iterator_copy(field->iterator);
}


qdr_action_t *qdr_action(qdr_action_handler_t action_handler, const char *label)
{
    qdr_action_t *action = new_qdr_action_t();
    ZERO(action);
    action->action_handler = action_handler;
    action->label          = label;
    return action;
}


void qdr_action_enqueue(qdr_core_t *core, qdr_action_t *action)
{
    sys_mutex_lock(core->action_lock);
    DEQ_INSERT_TAIL(core->action_list, action);
    const bool need_wake = core->sleeping;
    sys_mutex_unlock(core->action_lock);
    if (need_wake)
        sys_cond_signal(core->action_cond);
}


void qdr_action_background_enqueue(qdr_core_t *core, qdr_action_t *action)
{
    sys_mutex_lock(core->action_lock);
    DEQ_INSERT_TAIL(core->action_list_background, action);
    const bool need_wake = core->sleeping;
    sys_mutex_unlock(core->action_lock);
    if (need_wake)
        sys_cond_signal(core->action_cond);
}


qdr_address_t *qdr_address_CT(qdr_core_t *core, qd_address_treatment_t treatment, qdr_address_config_t *config)
{
    if (treatment == QD_TREATMENT_UNAVAILABLE)
        return 0;

    qdr_address_t *addr = new_qdr_address_t();
    ZERO(addr);
    addr->config     = config;
    addr->treatment  = treatment;
    addr->forwarder  = qdr_forwarder_CT(core, treatment);
    addr->rnodes     = qd_bitmask(0);
    addr->add_prefix = 0;
    addr->del_prefix = 0;
    addr->priority   = -1;

    if (config)
        config->ref_count++;

    return addr;
}


qdr_address_t *qdr_add_local_address_CT(qdr_core_t *core, char aclass, const char *address, qd_address_treatment_t treatment)
{
    char           addr_string[1000];
    qdr_address_t *addr = 0;
    qd_iterator_t *iter = 0;

    snprintf(addr_string, sizeof(addr_string), "%c%s", aclass, address);
    iter = qd_iterator_string(addr_string, ITER_VIEW_ALL);

    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    if (!addr) {
        addr = qdr_address_CT(core, treatment, 0);
        if (addr) {
            qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_INSERT_TAIL(core->addrs, addr);
            addr->ref_count++;
            addr->local = (aclass == 'L');
        }
    }
    qd_iterator_free(iter);
    return addr;
}


qdr_address_t *qdr_add_mobile_address_CT(qdr_core_t *core, const char *prefix, const char *address, qd_address_treatment_t treatment, bool edge)
{
    char           addr_string_stack[1000];
    char          *addr_string = addr_string_stack;
    bool           allocated   = false;
    qdr_address_t *addr = 0;
    qd_iterator_t *iter = 0;

    size_t len = strlen(prefix) + strlen(address) + 3;
    if (len > sizeof(addr_string_stack)) {
        allocated = true;
        addr_string = (char*) malloc(len);
    }

    snprintf(addr_string, len, "%s%s%s", edge ? "H" : "M0", prefix, address);
    iter = qd_iterator_string(addr_string, ITER_VIEW_ALL);

    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    if (!addr) {
        addr = qdr_address_CT(core, treatment, 0);
        if (addr) {
            qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_INSERT_TAIL(core->addrs, addr);
        }
    }

    qd_iterator_free(iter);
    if (allocated)
        free(addr_string);
    return addr;
}


bool qdr_address_is_mobile_CT(qdr_address_t *addr)
{
    if (!addr)
        return false;

    const char *addr_str = (const char *)qd_hash_key_by_handle(addr->hash_handle);

    if (addr_str && addr_str[0] == QD_ITER_HASH_PREFIX_MOBILE)
        return true;

    return false;
}

bool qdr_is_addr_treatment_multicast(qdr_address_t *addr)
{
    if (addr) {
        if (addr->treatment == QD_TREATMENT_MULTICAST_FLOOD || addr->treatment == QD_TREATMENT_MULTICAST_ONCE)
            return true;
    }
    return false;
}

void qdr_core_delete_link_route(qdr_core_t *core, qdr_link_route_t *lr)
{
    if (lr->conn_id) {
        DEQ_REMOVE_N(REF, lr->conn_id->link_route_refs, lr);
        qdr_route_check_id_for_deletion_CT(core, lr->conn_id);
    }

    if (lr->addr) {
        if (--lr->addr->ref_count == 0) {
            qdr_check_addr_CT(core, lr->addr);
        }
    }

    free(lr->add_prefix);
    free(lr->del_prefix);
    free(lr->name);
    free(lr->pattern);
    qd_hash_handle_free(lr->hash_handle);
    free_qdr_link_route_t(lr);
}

void qdr_core_delete_auto_link(qdr_core_t *core, qdr_auto_link_t *al)
{
    if (al->conn_id) {
        DEQ_REMOVE_N(REF, al->conn_id->auto_link_refs, al);
        qdr_route_check_id_for_deletion_CT(core, al->conn_id);
    }

    qdr_address_t *addr = al->addr;
    if (addr && --addr->ref_count == 0)
        qdr_check_addr_CT(core, addr);

    free(al->name);
    free(al->external_addr);
    qd_hash_handle_free(al->hash_handle);
    qdr_core_timer_free_CT(core, al->retry_timer);
    free_qdr_auto_link_t(al);
}

static void free_address_config(qdr_address_config_t *addr)
{
    free(addr->name);
    free(addr->pattern);
    qd_hash_handle_free(addr->hash_handle);
    free_qdr_address_config_t(addr);
}

void qdr_core_remove_address(qdr_core_t *core, qdr_address_t *addr)
{
    qdr_address_config_t *config = addr->config;
    if (config && --config->ref_count == 0)
        free_address_config(config);

    // Remove the address from the list, hash index, and parse tree
    DEQ_REMOVE(core->addrs, addr);
    if (addr->hash_handle) {
        const char *a_str = (const char *)qd_hash_key_by_handle(addr->hash_handle);
        if (a_str && QDR_IS_LINK_ROUTE(a_str[0])) {
            qd_iterator_t *iter = qd_iterator_string(a_str, ITER_VIEW_ALL);
            qdr_link_route_unmap_pattern_CT(core, iter);
            qd_iterator_free(iter);
        }
        qd_hash_remove_by_handle(core->addr_hash, addr->hash_handle);
        qd_hash_handle_free(addr->hash_handle);
    }

    // Free resources associated with this address

    DEQ_APPEND(addr->rlinks, addr->inlinks);
    qdr_link_ref_t *lref = DEQ_HEAD(addr->rlinks);
    while (lref) {
        qdr_link_t *link = lref->link;
        assert(link->owning_addr == addr);
        link->owning_addr = 0;
        qdr_del_link_ref(&addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        lref = DEQ_HEAD(addr->rlinks);
    }

    qd_bitmask_free(addr->rnodes);
    if (addr->treatment == QD_TREATMENT_ANYCAST_CLOSEST) {
        qd_bitmask_free(addr->closest_remotes);
    }
    else if (addr->treatment == QD_TREATMENT_ANYCAST_BALANCED) {
        free(addr->outstanding_deliveries);
    }

    qdr_connection_ref_t *cr = DEQ_HEAD(addr->conns);
    while (cr) {
        qdr_del_connection_ref(&addr->conns, cr->conn);
        cr = DEQ_HEAD(addr->conns);
    }

    //
    // If there are any fallback-related linkages, disconnect them.
    //
    if (!!addr->fallback)
        addr->fallback->fallback_for = 0;
    if (!!addr->fallback_for)
        addr->fallback_for->fallback = 0;

    free(addr->add_prefix);
    free(addr->del_prefix);
    free_qdr_address_t(addr);
}


void qdr_core_bind_address_link_CT(qdr_core_t *core, qdr_address_t *addr, qdr_link_t *link)
{
    const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
    link->owning_addr = addr;
    if (key && (*key == QD_ITER_HASH_PREFIX_MOBILE))
        link->phase = (int) (key[1] - '0');

    //
    // If this link is configured as no-route, don't create any functional linkage between the
    // link and the address beyond the owning_addr.
    //
    if (link->no_route) {
        link->owning_addr->ref_count++;
        // The no_route link's address has been bound. Set no_route_bound to true.
        link->no_route_bound = true;
        return;
    }

    if (link->link_direction == QD_OUTGOING) {
        qdr_add_link_ref(&addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        if (DEQ_SIZE(addr->rlinks) == 1) {
            qdr_addr_start_inlinks_CT(core, addr);
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_BECAME_LOCAL_DEST, addr);
        } else if (DEQ_SIZE(addr->rlinks) == 2 && qd_bitmask_cardinality(addr->rnodes) == 0)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_TWO_DEST, addr);
    } else {  // link->link_direction == QD_INCOMING
        qdr_add_link_ref(&addr->inlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        qdr_address_t *fallback_for = addr->fallback_for;

        if (DEQ_SIZE(addr->inlinks) + (!!fallback_for ? DEQ_SIZE(fallback_for->inlinks) : 0) == 1)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_BECAME_SOURCE, addr);
        else if (DEQ_SIZE(addr->inlinks) + (!!fallback_for ? DEQ_SIZE(fallback_for->inlinks) : 0) == 2)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_TWO_SOURCE, addr);

        qdr_address_t *fallback = addr->fallback;
        if (!!fallback) {
            size_t combined_in = DEQ_SIZE(addr->inlinks) + DEQ_SIZE(fallback->inlinks);
            if (combined_in == 1)
                qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_BECAME_SOURCE, fallback);
            else if (combined_in == 2)
                qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_TWO_SOURCE, fallback);
        }
    }
}


void qdr_core_unbind_address_link_CT(qdr_core_t *core, qdr_address_t *addr, qdr_link_t *link)
{
    link->owning_addr = 0;

    //
    // If the link is configured as no_route, there will be no further link/address
    // linkage to disconnect.
    //
    if (link->no_route)
        return;

    if (link->link_direction == QD_OUTGOING) {
        qdr_del_link_ref(&addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        if (DEQ_SIZE(addr->rlinks) == 0) {
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST, addr);
        } else if (DEQ_SIZE(addr->rlinks) == 1 && qd_bitmask_cardinality(addr->rnodes) == 0)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_ONE_LOCAL_DEST, addr);
    } else {  // link->link_direction == QD_INCOMING
        bool removed = qdr_del_link_ref(&addr->inlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        if (removed) {
            qdr_address_t *fallback_for = addr->fallback_for;
            if (DEQ_SIZE(addr->inlinks) + (!!fallback_for ? DEQ_SIZE(fallback_for->inlinks) : 0) == 0)
                qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_NO_LONGER_SOURCE, addr);
            else if (DEQ_SIZE(addr->inlinks) + (!!fallback_for ? DEQ_SIZE(fallback_for->inlinks) : 0) == 1)
                qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_ONE_SOURCE, addr);

            qdr_address_t *fallback = addr->fallback;
            if (!!fallback) {
                size_t combined_in = DEQ_SIZE(addr->inlinks) + DEQ_SIZE(fallback->inlinks);
                if (combined_in == 0)
                    qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_NO_LONGER_SOURCE, fallback);
                else if (combined_in == 1)
                    ; // Don't do anything in this case.  Raising the event will cause the fallback uplink to be dropped
            }
        }
    }
}


void qdr_core_bind_address_conn_CT(qdr_core_t *core, qdr_address_t *addr, qdr_connection_t *conn)
{
    qdr_add_connection_ref(&addr->conns, conn);
    if (DEQ_SIZE(addr->conns) == 1) {
        qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_BECAME_LOCAL_DEST, addr);
    }
}


void qdr_core_unbind_address_conn_CT(qdr_core_t *core, qdr_address_t *addr, qdr_connection_t *conn)
{
    qdr_del_connection_ref(&addr->conns, conn);
    if (DEQ_IS_EMPTY(addr->conns)) {
        qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST, addr);
    }
}


/**
 * Search for, and possibly create, the fallback address based on the
 * fallback flag in the address's configuration.  This will be used in
 * the forwarding paths to handle undeliverable messages with fallback destinations.
 */
void qdr_setup_fallback_address_CT(qdr_core_t *core, qdr_address_t *addr)
{
#define QDR_SETUP_FALLBACK_BUFFER_SIZE 256
    char  buffer[QDR_SETUP_FALLBACK_BUFFER_SIZE];
    char *alt_text       = buffer;
    bool  buffer_on_heap = false;

    char   *address_text = (char*) qd_hash_key_by_handle(addr->hash_handle);
    size_t  alt_length   = strlen(address_text) + 1;

    //
    // If this is a fallback address for a primary address that hasn't been seen
    // yet, simply exit without doing anything.
    //
    if (address_text[1] == QD_ITER_HASH_PHASE_FALLBACK)
        return;

    if (alt_length > QDR_SETUP_FALLBACK_BUFFER_SIZE) {
        alt_text       = (char*) malloc(alt_length);
        buffer_on_heap = true;
    }

    strcpy(alt_text, address_text);
    alt_text[1] = QD_ITER_HASH_PHASE_FALLBACK;

    qd_iterator_t *alt_iter = qd_iterator_string(alt_text, ITER_VIEW_ALL);
    qdr_address_t *alt_addr = 0;

    qd_hash_retrieve(core->addr_hash, alt_iter, (void**) &alt_addr);
    if (!alt_addr) {
        alt_addr = qdr_address_CT(core, QD_TREATMENT_ANYCAST_BALANCED, 0);
        qd_hash_insert(core->addr_hash, alt_iter, alt_addr, &alt_addr->hash_handle);
        DEQ_INSERT_TAIL(core->addrs, alt_addr);
    }

    assert(alt_addr != addr);
    assert(alt_addr->fallback_for == 0);
    addr->fallback         = alt_addr;
    alt_addr->fallback_for = addr;

    qd_iterator_free(alt_iter);
    if (buffer_on_heap)
        free(alt_text);
}


void qdr_core_remove_address_config(qdr_core_t *core, qdr_address_config_t *addr)
{
    qd_iterator_t *pattern = qd_iterator_string(addr->pattern, ITER_VIEW_ALL);

    // Remove the address from the list and the parse tree
    if (addr->hash_handle) {
        qd_hash_remove_by_handle(core->addr_lr_al_hash, addr->hash_handle);
        qd_hash_handle_free(addr->hash_handle);
        addr->hash_handle = 0;
    }
    DEQ_REMOVE(core->addr_config, addr);
    qd_parse_tree_remove_pattern(core->addr_parse_tree, pattern);
    addr->ref_count--;

    if (addr->ref_count == 0)
        free_address_config(addr);
    qd_iterator_free(pattern);
}


void qdr_add_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link, int cls)
{
    if (link->ref[cls] != 0)
        return;

    qdr_link_ref_t *ref = new_qdr_link_ref_t();
    DEQ_ITEM_INIT(ref);
    ref->link = link;
    link->ref[cls] = ref;
    DEQ_INSERT_TAIL(*ref_list, ref);
}


bool qdr_del_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link, int cls)
{
    if (link->ref[cls]) {
        DEQ_REMOVE(*ref_list, link->ref[cls]);
        free_qdr_link_ref_t(link->ref[cls]);
        link->ref[cls] = 0;
        return true;
    }
    return false;
}


void move_link_ref(qdr_link_t *link, int from_cls, int to_cls)
{
    assert(link->ref[to_cls] == 0);
    link->ref[to_cls] = link->ref[from_cls];
    link->ref[from_cls] = 0;
}


void qdr_add_connection_ref(qdr_connection_ref_list_t *ref_list, qdr_connection_t *conn)
{
    qdr_connection_ref_t *ref = new_qdr_connection_ref_t();
    DEQ_ITEM_INIT(ref);
    ref->conn = conn;
    DEQ_INSERT_TAIL(*ref_list, ref);
}


void qdr_del_connection_ref(qdr_connection_ref_list_t *ref_list, qdr_connection_t *conn)
{
    qdr_connection_ref_t *ref = DEQ_HEAD(*ref_list);
    while (ref) {
        if (ref->conn == conn) {
            DEQ_REMOVE(*ref_list, ref);
            free_qdr_connection_ref_t(ref);
            break;
        }
        ref = DEQ_NEXT(ref);
    }
}


void qdr_add_delivery_ref_CT(qdr_delivery_ref_list_t *list, qdr_delivery_t *dlv)
{
    qdr_delivery_ref_t *ref = new_qdr_delivery_ref_t();
    DEQ_ITEM_INIT(ref);
    ref->dlv = dlv;
    DEQ_INSERT_TAIL(*list, ref);
}


void qdr_del_delivery_ref(qdr_delivery_ref_list_t *list, qdr_delivery_ref_t *ref)
{
    DEQ_REMOVE(*list, ref);
    free_qdr_delivery_ref_t(ref);
}


void qdr_add_subscription_ref_CT(qdr_subscription_ref_list_t *list, qdr_subscription_t *sub)
{
    qdr_subscription_ref_t *ref = new_qdr_subscription_ref_t();
    DEQ_ITEM_INIT(ref);
    ref->sub = sub;
    DEQ_INSERT_TAIL(*list, ref);
}


void qdr_del_subscription_ref_CT(qdr_subscription_ref_list_t *list, qdr_subscription_ref_t *ref)
{
    DEQ_REMOVE(*list, ref);
    free_qdr_subscription_ref_t(ref);
}


static void qdr_general_handler(void *context)
{
    qdr_core_t              *core = (qdr_core_t*) context;
    qdr_general_work_list_t  work_list;
    qdr_general_work_t      *work;

    sys_mutex_lock(core->work_lock);
    DEQ_MOVE(core->work_list, work_list);
    sys_mutex_unlock(core->work_lock);

    work = DEQ_HEAD(work_list);
    while (work) {
        DEQ_REMOVE_HEAD(work_list);
        work->handler(core, work);
        free_qdr_general_work_t(work);
        work = DEQ_HEAD(work_list);
    }
}


qdr_general_work_t *qdr_general_work(qdr_general_work_handler_t handler)
{
    qdr_general_work_t *work = new_qdr_general_work_t();
    ZERO(work);
    work->handler = handler;
    return work;
}


void qdr_post_general_work_CT(qdr_core_t *core, qdr_general_work_t *work)
{
    bool notify;

    sys_mutex_lock(core->work_lock);
    DEQ_ITEM_INIT(work);
    DEQ_INSERT_TAIL(core->work_list, work);
    notify = DEQ_SIZE(core->work_list) == 1;
    sys_mutex_unlock(core->work_lock);

    if (notify)
        qd_timer_schedule(core->work_timer, 0);
}


uint64_t qdr_identifier(qdr_core_t* core)
{
    sys_mutex_lock(core->id_lock);
    uint64_t id = core->next_identifier++;
    sys_mutex_unlock(core->id_lock);
    return id;
}

void qdr_reset_sheaf(qdr_core_t *core, uint8_t n)
{
  qdr_priority_sheaf_t *sheaf = core->data_links_by_mask_bit + n;
  sheaf->count = 0;
  memset(sheaf->links, 0, QDR_N_PRIORITIES * sizeof(void *));
}


void qdr_connection_work_free_CT(qdr_connection_work_t *work)
{
    qdr_terminus_free(work->source);
    qdr_terminus_free(work->target);
    free_qdr_connection_work_t(work);
}

static void qdr_post_global_stats_response(qdr_core_t *core, qdr_general_work_t *work)
{
    work->stats_handler(work->context);
}

static void qdr_global_stats_request_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_global_stats_t *stats = action->args.stats_request.stats;
    if (stats) {
        stats->addrs = DEQ_SIZE(core->addrs);
        stats->links = DEQ_SIZE(core->open_links);
        stats->routers = DEQ_SIZE(core->routers);
        stats->connections = DEQ_SIZE(core->open_connections);
        stats->link_routes = DEQ_SIZE(core->link_routes);
        stats->auto_links = DEQ_SIZE(core->auto_links);
        stats->presettled_deliveries = core->presettled_deliveries;
        stats->dropped_presettled_deliveries = core->dropped_presettled_deliveries;
        stats->accepted_deliveries = core->accepted_deliveries;
        stats->rejected_deliveries = core->rejected_deliveries;
        stats->released_deliveries = core->released_deliveries;
        stats->modified_deliveries = core->modified_deliveries;
        stats->deliveries_ingress = core->deliveries_ingress;
        stats->deliveries_egress = core->deliveries_egress;
        stats->deliveries_transit = core->deliveries_transit;
        stats->deliveries_ingress_route_container = core->deliveries_ingress_route_container;
        stats->deliveries_egress_route_container = core->deliveries_egress_route_container;
        stats->deliveries_delayed_1sec = core->deliveries_delayed_1sec;
        stats->deliveries_delayed_10sec = core->deliveries_delayed_10sec;
        stats->deliveries_stuck = core->deliveries_stuck;
        stats->links_blocked = core->links_blocked;
        stats->deliveries_redirected_to_fallback = core->deliveries_redirected;
    }
    qdr_general_work_t *work = qdr_general_work(qdr_post_global_stats_response);
    work->stats_handler = action->args.stats_request.handler;
    work->context = action->args.stats_request.context;
    qdr_post_general_work_CT(core, work);
}


void qdr_request_global_stats(qdr_core_t *core, qdr_global_stats_t *stats, qdr_global_stats_handler_t callback, void *context)
{
    qdr_action_t *action = qdr_action(qdr_global_stats_request_CT, "global_stats_request");
    action->args.stats_request.stats = stats;
    action->args.stats_request.handler = callback;
    action->args.stats_request.context = context;
    qdr_action_enqueue(core, action);
}


qdr_protocol_adaptor_t *qdr_protocol_adaptor(qdr_core_t                *core,
                                             const char                *name,
                                             void                      *context,
                                             qdr_connection_activate_t  activate,
                                             qdr_link_first_attach_t    first_attach,
                                             qdr_link_second_attach_t   second_attach,
                                             qdr_link_detach_t          detach,
                                             qdr_link_flow_t            flow,
                                             qdr_link_offer_t           offer,
                                             qdr_link_drained_t         drained,
                                             qdr_link_drain_t           drain,
                                             qdr_link_push_t            push,
                                             qdr_link_deliver_t         deliver,
                                             qdr_link_get_credit_t      get_credit,
                                             qdr_delivery_update_t      delivery_update,
                                             qdr_connection_close_t     conn_close,
                                             qdr_connection_trace_t     conn_trace)
{
    qdr_protocol_adaptor_t *adaptor = NEW(qdr_protocol_adaptor_t);

    qd_log(core->log, QD_LOG_INFO, "Protocol adaptor registered: %s", name);

    DEQ_ITEM_INIT(adaptor);
    adaptor->name                    = name;
    adaptor->user_context            = context;
    adaptor->activate_handler        = activate;
    adaptor->first_attach_handler    = first_attach;
    adaptor->second_attach_handler   = second_attach;
    adaptor->detach_handler          = detach;
    adaptor->flow_handler            = flow;
    adaptor->offer_handler           = offer;
    adaptor->drained_handler         = drained;
    adaptor->drain_handler           = drain;
    adaptor->push_handler            = push;
    adaptor->deliver_handler         = deliver;
    adaptor->get_credit_handler      = get_credit;
    adaptor->delivery_update_handler = delivery_update;
    adaptor->conn_close_handler      = conn_close;
    adaptor->conn_trace_handler      = conn_trace;

    DEQ_INSERT_TAIL(core->protocol_adaptors, adaptor);
    return adaptor;
}


void qdr_protocol_adaptor_free(qdr_core_t *core, qdr_protocol_adaptor_t *adaptor)
{
    DEQ_REMOVE(core->protocol_adaptors, adaptor);
    free(adaptor);
}


qdr_link_work_t *qdr_link_work(qdr_link_work_type_t type)
{
    qdr_link_work_t *work = new_qdr_link_work_t();
    if (work) {
        ZERO(work);
        work->work_type = type;
        sys_atomic_init(&work->ref_count, 1);
    }
    return work;
}


qdr_link_work_t *qdr_link_work_getref(qdr_link_work_t *work)
{
    if (work) {
        uint32_t old = sys_atomic_inc(&work->ref_count);
        (void)old;  // mask unused var compiler warning
        assert(old != 0);
    }
    return work;
}

void qdr_link_work_release(qdr_link_work_t *work)
{
    if (work) {
        uint32_t old = sys_atomic_dec(&work->ref_count);
        assert(old != 0);
        if (old == 1) {
            qdr_error_free(work->error);
            free_qdr_link_work_t(work);
        }
    }
}


