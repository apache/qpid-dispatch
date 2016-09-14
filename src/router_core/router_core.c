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

ALLOC_DEFINE(qdr_address_t);
ALLOC_DEFINE(qdr_address_config_t);
ALLOC_DEFINE(qdr_node_t);
ALLOC_DEFINE(qdr_delivery_t);
ALLOC_DEFINE(qdr_delivery_ref_t);
ALLOC_DEFINE(qdr_link_t);
ALLOC_DEFINE(qdr_router_ref_t);
ALLOC_DEFINE(qdr_link_ref_t);
ALLOC_DEFINE(qdr_general_work_t);
ALLOC_DEFINE(qdr_connection_ref_t);

static void qdr_general_handler(void *context);

qdr_core_t *qdr_core(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id)
{
    qdr_core_t *core = NEW(qdr_core_t);
    ZERO(core);

    core->qd          = qd;
    core->router_mode = mode;
    core->router_area = area;
    core->router_id   = id;

    //
    // Set up the logging sources for the router core
    //
    core->log       = qd_log_source("ROUTER_CORE");
    core->agent_log = qd_log_source("AGENT");

    //
    // Set up the threading support
    //
    core->action_cond = sys_cond();
    core->action_lock = sys_mutex();
    core->running     = true;
    DEQ_INIT(core->action_list);

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
    sys_cond_signal(core->action_cond);
    sys_thread_join(core->thread);

    //
    // Free the core resources
    //
    qdr_core_unsubscribe(core->agent_subscription_mobile);
    qdr_core_unsubscribe(core->agent_subscription_local);
    sys_thread_free(core->thread);
    sys_cond_free(core->action_cond);
    sys_mutex_free(core->action_lock);
    sys_mutex_free(core->work_lock);
    sys_mutex_free(core->id_lock);
    qd_timer_free(core->work_timer);
    free(core);
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

    field->iterator = qd_field_iterator_buffer(DEQ_HEAD(field->buffers), 0, ilength);

    return field;
}


qdr_field_t *qdr_field_from_iter(qd_field_iterator_t *iter)
{
    if (!iter)
        return 0;

    qdr_field_t *field = new_qdr_field_t();
    qd_buffer_t *buf;
    int          remaining;
    int          length;

    ZERO(field);
    qd_field_iterator_reset(iter);
    remaining = qd_field_iterator_remaining(iter);
    length    = remaining;
    while (remaining) {
        buf = qd_buffer();
        size_t cap    = qd_buffer_capacity(buf);
        int    copied = qd_field_iterator_ncopy(iter, qd_buffer_cursor(buf), cap);
        qd_buffer_insert(buf, copied);
        DEQ_INSERT_TAIL(field->buffers, buf);
        remaining = qd_field_iterator_remaining(iter);
    }

    field->iterator = qd_field_iterator_buffer(DEQ_HEAD(field->buffers), 0, length);

    return field;
}

qd_field_iterator_t *qdr_field_iterator(qdr_field_t *field)
{
    if (!field)
        return 0;

    return field->iterator;
}


void qdr_field_free(qdr_field_t *field)
{
    if (field) {
        qd_field_iterator_free(field->iterator);
        qd_buffer_list_free_buffers(&field->buffers);
        free_qdr_field_t(field);
    }
}


char *qdr_field_copy(qdr_field_t *field)
{
    if (!field || !field->iterator)
        return 0;

    return (char*) qd_field_iterator_copy(field->iterator);
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
    sys_cond_signal(core->action_cond);
    sys_mutex_unlock(core->action_lock);
}


qdr_address_t *qdr_address_CT(qdr_core_t *core, qd_address_treatment_t treatment)
{
    qdr_address_t *addr = new_qdr_address_t();
    ZERO(addr);
    addr->treatment = treatment;
    addr->forwarder = qdr_forwarder_CT(core, treatment);
    addr->rnodes    = qd_bitmask(0);
    return addr;
}


qdr_address_t *qdr_add_local_address_CT(qdr_core_t *core, char aclass, const char *address, qd_address_treatment_t treatment)
{
    char                 addr_string[1000];
    qdr_address_t       *addr = 0;
    qd_field_iterator_t *iter = 0;

    snprintf(addr_string, sizeof(addr_string), "%c%s", aclass, address);
    iter = qd_address_iterator_string(addr_string, ITER_VIEW_ALL);

    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    if (!addr) {
        addr = qdr_address_CT(core, treatment);
        qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_ITEM_INIT(addr);
        DEQ_INSERT_TAIL(core->addrs, addr);
        addr->block_deletion = true;
        addr->local = (aclass == 'L');
    }
    qd_field_iterator_free(iter);
    return addr;
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


void qdr_del_link_ref(qdr_link_ref_list_t *ref_list, qdr_link_t *link, int cls)
{
    if (link->ref[cls]) {
        DEQ_REMOVE(*ref_list, link->ref[cls]);
        free_qdr_link_ref_t(link->ref[cls]);
        link->ref[cls] = 0;
    }
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


void qdr_add_delivery_ref(qdr_delivery_ref_list_t *list, qdr_delivery_t *dlv)
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

