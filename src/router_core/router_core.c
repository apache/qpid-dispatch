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
#include "exchange_bindings.h"
#include "core_events.h"
#include "delivery.h"
#include <stdio.h>
#include <strings.h>

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

static void qdr_general_handler(void *context);

uint64_t next_power_of_two(uint64_t v)
{
    assert(v > 0);
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

inline void qdr_action_q_init(qdr_action_q_t *actions, uint64_t capacity)
{
    capacity = next_power_of_two(capacity);
    actions->c_seq = 0;
    actions->p_seq = 0;
    actions->capacity = capacity;
    ALLOC_CACHE_ALIGNED(capacity * sizeof(qdr_action_t), actions->action);
}

inline void qdr_action_q_offer(qdr_action_q_t *actions, qdr_action_t *action)
{
    uint64_t size = (actions->p_seq + 1) - actions->c_seq;
    if (size > actions->capacity) {
        //increase capacity
        uint64_t old_capacity = actions->capacity;
        qdr_action_t *old_actions = actions->action;
        actions->capacity = old_capacity * 2;
        //can't use memmove due to wrapping, will use (at worst) 2 memcpy
        ALLOC_CACHE_ALIGNED(actions->capacity * sizeof(qdr_action_t), actions->action);
        const uint64_t old_c_mask = (old_capacity - 1);
        const uint64_t start = actions->c_seq & old_c_mask;
        const uint64_t end = actions->p_seq & old_c_mask;
        const bool contiguous = end > start;
        size_t remaining = contiguous ? (end - start) : (old_capacity - start);
        memcpy(actions->action, old_actions + start, remaining * sizeof(qdr_action_t));
        if (!contiguous) {
            memcpy(actions->action + remaining, old_actions, end * sizeof(qdr_action_t));
        }
        free(old_actions);
        actions->c_seq = 0;
        actions->p_seq = size - 1;
    }
    const uint64_t index = actions->p_seq & (actions->capacity - 1);
    actions->action[index] = *action;
    actions->p_seq++;
}

inline bool qdr_action_q_poll(qdr_action_q_t *actions, qdr_action_t *action)
{
    if (actions->p_seq == actions->c_seq) {
        return false;
    }
    uint64_t index = actions->c_seq & (actions->capacity - 1);
    *action = actions->action[index];
    actions->c_seq++;
    return true;
}

inline bool qdr_action_q_is_empty(qdr_action_q_t *actions)
{
    return actions->p_seq == actions->c_seq;
}

inline size_t qdr_action_q_batch_poll(qdr_action_q_t *actions, qdr_action_t *action_vec, size_t limit)
{
    const uint64_t size = actions->p_seq - actions->c_seq;
    if (size == 0) {
        return 0;
    }
    limit = size < limit ? size : limit;
    const uint64_t c_mask = (actions->capacity - 1);
    const uint64_t start = actions->c_seq & c_mask;
    const uint64_t end = (actions->c_seq + limit) & c_mask;
    const bool contiguous = end > start;
    size_t remaining = contiguous ? (end - start) : (actions->capacity - start);
    memcpy(action_vec, actions->action + start, remaining * sizeof(qdr_action_t));
    if (!contiguous) {
        memcpy(action_vec + remaining, actions->action, end * sizeof(qdr_action_t));
    }
    actions->c_seq += limit;
    return limit;
}

qdr_core_t *qdr_core(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id)
{
    qdr_core_t *core = NEW(qdr_core_t);
    ZERO(core);

    core->qd          = qd;
    core->router_mode = mode;
    core->router_area = area;
    core->router_id   = id;

    DEQ_INIT(core->exchanges);

    //
    // Set up the logging sources for the router core
    //
    core->log       = qd->router->log_source;
    core->agent_log = qd_log_source("AGENT");

    //
    // Set up the threading support
    //
    core->sleeping = false;
    core->action_cond = sys_cond();
    core->action_lock = sys_mutex();
    core->running     = true;
    qdr_action_q_init(&core->action_list, 1024);

    core->work_lock = sys_mutex();
    DEQ_INIT(core->work_list);
    core->work_timer = qd_timer(core->qd, qdr_general_handler, core);

    //
    // Set up the unique identifier generator
    //
    core->next_identifier = 1;
    core->id_lock = sys_mutex();

    //
    // Launch the core thread
    //
    core->thread = sys_thread(router_core_thread, core);

    //
    // Perform outside-of-thread setup for the management agent
    //
    core->agent_subscription_mobile = qdr_core_subscribe(core, "$management", 'M', '0',
                                                         QD_TREATMENT_ANYCAST_CLOSEST,
                                                         qdr_management_agent_on_message, core);
    core->agent_subscription_local = qdr_core_subscribe(core, "$management", 'L', '0',
                                                        QD_TREATMENT_ANYCAST_CLOSEST,
                                                        qdr_management_agent_on_message, core);

    return core;
}


void qdr_core_free(qdr_core_t *core)
{
    //
    // Stop and join the thread
    //
    core->running = false;
    sys_mutex_lock(core->action_lock);
    if (core->sleeping) {
        sys_cond_signal(core->action_cond);
    }
    sys_mutex_unlock(core->action_lock);
    sys_thread_join(core->thread);

    // Drain the general work lists
    qdr_general_handler(core);

    //
    // Free the core resources
    //
    sys_thread_free(core->thread);
    sys_cond_free(core->action_cond);
    sys_mutex_free(core->action_lock);
    sys_mutex_free(core->work_lock);
    sys_mutex_free(core->id_lock);
    qd_timer_free(core->work_timer);

    //we can't call qdr_core_unsubscribe on the subscriptions because the action processing thread has
    //already been shut down. But, all the action would have done at this point is free the subscriptions
    //so we just do that directly.
    free(core->agent_subscription_mobile);
    free(core->agent_subscription_local);

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
        free_qdr_link_t(link);
        link = DEQ_HEAD(core->open_links);
    }

    qdr_connection_t *conn = DEQ_HEAD(core->open_connections);
    while (conn) {
        DEQ_REMOVE_HEAD(core->open_connections);

        if (conn->conn_id) {
            qdr_del_connection_ref(&conn->conn_id->connection_refs, conn);
            qdr_route_check_id_for_deletion_CT(core, conn->conn_id);
        }

        qdr_connection_work_t *work = DEQ_HEAD(conn->work_list);
        while (work) {
            DEQ_REMOVE_HEAD(conn->work_list);
            qdr_connection_work_free_CT(work);
            work = DEQ_HEAD(conn->work_list);
        }

        qdr_connection_free(conn);
        conn = DEQ_HEAD(core->open_connections);
    }

    // at this point all the conn identifiers have been freed
    qd_hash_free(core->conn_id_hash);

    qdr_modules_finalize(core);

    if (core->query_lock)                sys_mutex_free(core->query_lock);
    if (core->routers_by_mask_bit)       free(core->routers_by_mask_bit);
    if (core->control_links_by_mask_bit) free(core->control_links_by_mask_bit);
    if (core->data_links_by_mask_bit)    free(core->data_links_by_mask_bit);
    if (core->neighbor_free_mask)        qd_bitmask_free(core->neighbor_free_mask);

    free(core->action_list.action);

    free(core);
}

void qdr_router_node_free(qdr_core_t *core, qdr_node_t *rnode)
{
    qd_bitmask_free(rnode->valid_origins);
    DEQ_REMOVE(core->routers, rnode);
    core->routers_by_mask_bit[rnode->mask_bit] = 0;
    core->cost_epoch++;
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
    qdr_action_q_offer(&core->action_list, action);
    if (core->sleeping) {
        sys_cond_signal(core->action_cond);
    }
    sys_mutex_unlock(core->action_lock);
    free_qdr_action_t(action);
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
            addr->block_deletion = true;
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
    qdr_core_timer_free_CT(core, al->retry_timer);
    free_qdr_auto_link_t(al);
}

static void free_address_config(qdr_address_config_t *addr)
{
    free(addr->name);
    free(addr->pattern);
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
        if (QDR_IS_LINK_ROUTE(a_str[0])) {
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

    if (link->link_direction == QD_OUTGOING) {
        qdr_add_link_ref(&addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        if (DEQ_SIZE(addr->rlinks) == 1) {
            if (key && (*key == QD_ITER_HASH_PREFIX_EDGE_SUMMARY || *key == QD_ITER_HASH_PREFIX_MOBILE))
                qdr_post_mobile_added_CT(core, key, addr->treatment);
            qdr_addr_start_inlinks_CT(core, addr);
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_BECAME_LOCAL_DEST, addr);
        } else if (DEQ_SIZE(addr->rlinks) == 2 && qd_bitmask_cardinality(addr->rnodes) == 0)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_TWO_DEST, addr);
    } else {  // link->link_direction == QD_INCOMING
        qdr_add_link_ref(&addr->inlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        if (DEQ_SIZE(addr->inlinks) == 1) {
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_BECAME_SOURCE, addr);
            if (!!addr->fallback && !link->fallback)
                qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_BECAME_SOURCE, addr->fallback);
        } else if (DEQ_SIZE(addr->inlinks) == 2) {
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_TWO_SOURCE, addr);
            if (!!addr->fallback && !link->fallback)
                qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_TWO_SOURCE, addr->fallback);
        }
    }
}


void qdr_core_unbind_address_link_CT(qdr_core_t *core, qdr_address_t *addr, qdr_link_t *link)
{
    link->owning_addr = 0;

    if (link->link_direction == QD_OUTGOING) {
        qdr_del_link_ref(&addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        if (DEQ_SIZE(addr->rlinks) == 0) {
            const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
            if (key && (*key == QD_ITER_HASH_PREFIX_MOBILE || *key == QD_ITER_HASH_PREFIX_EDGE_SUMMARY))
                qdr_post_mobile_removed_CT(core, key);
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST, addr);
        } else if (DEQ_SIZE(addr->rlinks) == 1 && qd_bitmask_cardinality(addr->rnodes) == 0)
            qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_ONE_LOCAL_DEST, addr);
    } else {
        bool removed = qdr_del_link_ref(&addr->inlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        if (removed) {
            if (DEQ_SIZE(addr->inlinks) == 0) {
                qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_NO_LONGER_SOURCE, addr);
                if (!!addr->fallback && !link->fallback)
                    qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_NO_LONGER_SOURCE, addr->fallback);
            } else if (DEQ_SIZE(addr->inlinks) == 1) {
                qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_ONE_SOURCE, addr);
                if (!!addr->fallback && !link->fallback)
                    qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_ONE_SOURCE, addr->fallback);
            }
        }
    }
}


void qdr_core_bind_address_conn_CT(qdr_core_t *core, qdr_address_t *addr, qdr_connection_t *conn)
{
    qdr_add_connection_ref(&addr->conns, conn);
    if (DEQ_SIZE(addr->conns) == 1) {
        const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
        qdr_post_mobile_added_CT(core, key, addr->treatment);
        qdrc_event_addr_raise(core, QDRC_EVENT_ADDR_BECAME_LOCAL_DEST, addr);
    }
}


void qdr_core_unbind_address_conn_CT(qdr_core_t *core, qdr_address_t *addr, qdr_connection_t *conn)
{
    qdr_del_connection_ref(&addr->conns, conn);
    if (DEQ_IS_EMPTY(addr->conns)) {
        const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
        qdr_post_mobile_removed_CT(core, key);
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

