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

#include "module.h"
#include "router_core_private.h"

#include "qpid/dispatch/protocol_adaptor.h"

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


typedef struct qdrc_adaptor_t {
    DEQ_LINKS(struct qdrc_adaptor_t);
    const char          *name;
    qdr_adaptor_init_t   on_init;
    qdr_adaptor_final_t  on_final;
    void                *context;
} qdrc_adaptor_t;

DEQ_DECLARE(qdrc_adaptor_t, qdrc_adaptor_list_t);
static qdrc_adaptor_list_t registered_adaptors = {0,0};

void qdr_register_adaptor(const char *name, qdr_adaptor_init_t on_init, qdr_adaptor_final_t on_final)
{
    qdrc_adaptor_t *adaptor = NEW(qdrc_adaptor_t);
    ZERO(adaptor);
    adaptor->name     = name;
    adaptor->on_init  = on_init;
    adaptor->on_final = on_final;
    DEQ_INSERT_TAIL(registered_adaptors, adaptor);
}


static void qdr_activate_connections_CT(qdr_core_t *core)
{
    qdr_connection_t *conn = DEQ_HEAD(core->connections_to_activate);
    while (conn) {
        DEQ_REMOVE_HEAD_N(ACTIVATE, core->connections_to_activate);
        conn->in_activate_list = false;
        conn->protocol_adaptor->activate_handler(conn->protocol_adaptor->user_context, conn);
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


void qdr_adaptors_init(qdr_core_t *core)
{
    //
    // Initialize registered adaptors
    //
    qdrc_adaptor_t *adaptor = DEQ_HEAD(registered_adaptors);
    while (adaptor) {
        adaptor->on_init(core, &adaptor->context);
        adaptor = DEQ_NEXT(adaptor);
    }
}


void qdr_adaptors_finalize(qdr_core_t *core)
{
    //
    // Finalize registered adaptors
    //
    qdrc_adaptor_t *adaptor = DEQ_TAIL(registered_adaptors);
    while (adaptor) {
        adaptor->on_final(adaptor->context);
        adaptor = DEQ_PREV(adaptor);
    }

    // release the default AMQP adaptor (it is not a module)
    assert(DEQ_SIZE(core->protocol_adaptors) == 1);
    qdr_protocol_adaptor_free(core, DEQ_HEAD(core->protocol_adaptors));
}


void *router_core_thread(void *arg)
{
    qdr_core_t        *core = (qdr_core_t*) arg;
    qdr_action_list_t  action_list = DEQ_EMPTY;
    qdr_action_t      *bg_action = 0;

    qd_log(core->log, QD_LOG_INFO, "Router Core thread running. %s/%s", core->router_area, core->router_id);
    while (core->running) {
        //
        // Use the lock only to protect the condition variable and the action lists
        //
        sys_mutex_lock(core->action_lock);

        for (;;) {
            if (!DEQ_IS_EMPTY(core->action_list)) {
                DEQ_MOVE(core->action_list, action_list);
                break;
            }

            // no pending actions so process one background action if present
            //
            bg_action = DEQ_HEAD(core->action_list_background);
            if (bg_action) {
                DEQ_REMOVE_HEAD(core->action_list_background);
                break;
            }

            if (!core->running)
                break;

            //
            // Block on the condition variable when there is no action to do
            //
            core->sleeping = true;
            sys_cond_wait(core->action_cond, core->action_lock);
            core->sleeping = false;
        }

        sys_mutex_unlock(core->action_lock);

        // bg_action is set only when there are no other actions pending
        //
        if (bg_action) {
            if (bg_action->label)
                qd_log(core->log, QD_LOG_TRACE, "Core background action '%s'%s", bg_action->label, core->running ? "" : " (discard)");
            bg_action->action_handler(core, bg_action, !core->running);
            free_qdr_action_t(bg_action);
            bg_action = 0;
            continue;
        }

        //
        // Process and free all of the action items in the list
        //
        qdr_action_t *action = DEQ_HEAD(action_list);
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
