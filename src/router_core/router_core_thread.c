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
#include "module.h"
#include "dispatch_private.h"

/**
 * Creates a thread that is dedicated to managing and using the routing table.
 * The purpose of moving this function into one thread is to remove the widespread
 * lock contention that happens with synchrounous multi-threaded routing.
 *
 * This module owns, manages, and uses the router-link list and the address hash table
 */

ALLOC_DEFINE(qdr_action_t);


typedef struct qdrc_module_t {
    DEQ_LINKS(struct qdrc_module_t);
    const char          *name;
    qdrc_module_enable_t enable;
    qdrc_module_init_t   on_init;
    qdrc_module_final_t  on_final;
    void                *context;
    bool                 enabled;
} qdrc_module_t;

DEQ_DECLARE(qdrc_module_t, qdrc_module_list_t);
static qdrc_module_list_t registered_modules = {0,0};

void qdr_register_core_module(const char *name, qdrc_module_enable_t enable, qdrc_module_init_t on_init, qdrc_module_final_t on_final)
{
    qdrc_module_t *module = NEW(qdrc_module_t);
    ZERO(module);
    module->name     = name;
    module->enable   = enable;
    module->on_init  = on_init;
    module->on_final = on_final;
    DEQ_INSERT_TAIL(registered_modules, module);
}


static void qdr_activate_connections_CT(qdr_core_t *core)
{
    qdr_connection_t *conn = DEQ_HEAD(core->connections_to_activate);
    while (conn) {
        DEQ_REMOVE_HEAD_N(ACTIVATE, core->connections_to_activate);
        conn->in_activate_list = false;
        sys_mutex_lock(qd_server_get_activation_lock(core->qd->server));
        qd_server_activate((qd_connection_t*) qdr_connection_get_context(conn));
        sys_mutex_unlock(qd_server_get_activation_lock(core->qd->server));
        conn = DEQ_HEAD(core->connections_to_activate);
    }
}


static void qdr_do_message_to_addr_free(qdr_core_t *core, qdr_general_work_t *work)
{
    qdr_delivery_cleanup_t *cleanup = DEQ_HEAD(work->delivery_cleanup_list);

    while (cleanup) {
        DEQ_REMOVE_HEAD(work->delivery_cleanup_list);
        if (cleanup->msg)
            qd_message_free(cleanup->msg);
        if (cleanup->iter)
            qd_iterator_free(cleanup->iter);
        free_qdr_delivery_cleanup_t(cleanup);
        cleanup = DEQ_HEAD(work->delivery_cleanup_list);
    }
}


void qdr_modules_init(qdr_core_t *core)
{
    //
    // Initialize registered modules
    //
    qdrc_module_t *module = DEQ_HEAD(registered_modules);
    while (module) {
        module->enabled = module->enable(core);
        if (module->enabled) {
            module->on_init(core, &module->context);
            qd_log(core->log, QD_LOG_INFO, "Core module enabled: %s", module->name);
        } else
            qd_log(core->log, QD_LOG_INFO, "Core module present but disabled: %s", module->name);

        module = DEQ_NEXT(module);
    }
}


void qdr_modules_finalize(qdr_core_t *core)
{
    //
    // Finalize registered modules
    //
    qdrc_module_t *module = DEQ_TAIL(registered_modules);
    while (module) {
        if (module->enabled) {
            qd_log(core->log, QD_LOG_INFO, "Finalizing core module: %s", module->name);
            module->on_final(module->context);
        }
        module = DEQ_PREV(module);
    }

}


/*
 * router_core_process_background_action_LH
 *
 * Process up to one available background action.
 * Return true iff an action was processed.
 */
static bool router_core_process_background_action_LH(qdr_core_t *core)
{
    qdr_action_t *action = DEQ_HEAD(core->action_list_background);

    if (!!action) {
        DEQ_REMOVE_HEAD(core->action_list_background);
        sys_mutex_unlock(core->action_lock);
        if (action->label)
            qd_log(core->log, QD_LOG_TRACE, "Core background action '%s'%s", action->label, core->running ? "" : " (discard)");
        action->action_handler(core, action, !core->running);
        sys_mutex_lock(core->action_lock);

        free_qdr_action_t(action);
        return true;
    }

    return false;
}


void *router_core_thread(void *arg)
{
    qdr_core_t        *core = (qdr_core_t*) arg;
    qdr_action_list_t  action_list;
    qdr_action_t      *action;

    qdr_forwarder_setup_CT(core);
    qdr_route_table_setup_CT(core);

    qdr_modules_init(core);

    qd_log(core->log, QD_LOG_INFO, "Router Core thread running. %s/%s", core->router_area, core->router_id);
    while (core->running) {
        //
        // Use the lock only to protect the condition variable and the action lists
        //
        sys_mutex_lock(core->action_lock);

        //
        // Block on the condition variable when there is no action to do
        //
        while (core->running && DEQ_IS_EMPTY(core->action_list)) {
            if (!router_core_process_background_action_LH(core))
                sys_cond_wait(core->action_cond, core->action_lock);
        }

        //
        // Move the entire action list to a private list so we can process it without
        // holding the lock
        //
        DEQ_MOVE(core->action_list, action_list);
        sys_mutex_unlock(core->action_lock);

        //
        // Process and free all of the action items in the list
        //
        action = DEQ_HEAD(action_list);
        while (action) {
            DEQ_REMOVE_HEAD(action_list);
            if (action->label)
                qd_log(core->log, QD_LOG_TRACE, "Core action '%s'%s", action->label, core->running ? "" : " (discard)");
            action->action_handler(core, action, !core->running);
            free_qdr_action_t(action);
            action = DEQ_HEAD(action_list);
        }

        //
        // Activate all connections that were flagged for activation during the above processing
        //
        qdr_activate_connections_CT(core);

        //
        // Schedule the cleanup of deliveries freed during this core-thread pass
        //
        if (DEQ_SIZE(core->delivery_cleanup_list) > 0) {
            qdr_general_work_t *work = qdr_general_work(qdr_do_message_to_addr_free);
            DEQ_MOVE(core->delivery_cleanup_list, work->delivery_cleanup_list);
            qdr_post_general_work_CT(core, work);
        }
    }
    qd_log(core->log, QD_LOG_INFO, "Router Core thread exited");
    return 0;
}
