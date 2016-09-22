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

/**
 * Creates a thread that is dedicated to managing and using the routing table.
 * The purpose of moving this function into one thread is to remove the widespread
 * lock contention that happens with synchrounous multi-threaded routing.
 *
 * This module owns, manages, and uses the router-link list and the address hash table
 */

ALLOC_DEFINE(qdr_action_t);


static void qdr_activate_connections_CT(qdr_core_t *core)
{
    qdr_connection_t *conn = DEQ_HEAD(core->connections_to_activate);
    while (conn) {
        DEQ_REMOVE_HEAD_N(ACTIVATE, core->connections_to_activate);
        conn->in_activate_list = false;
        core->activate_handler(core->user_context, conn, DEQ_IS_EMPTY(core->connections_to_activate));
        conn = DEQ_HEAD(core->connections_to_activate);
    }
}


void *router_core_thread(void *arg)
{
    qdr_core_t        *core = (qdr_core_t*) arg;
    qdr_action_list_t  action_list;
    qdr_action_t      *action;

    qdr_forwarder_setup_CT(core);
    qdr_route_table_setup_CT(core);
    qdr_agent_setup_CT(core);

    qd_log(core->log, QD_LOG_INFO, "Router Core thread running. %s/%s", core->router_area, core->router_id);
    while (core->running) {
        //
        // Use the lock only to protect the condition variable and the action list
        //
        sys_mutex_lock(core->action_lock);

        //
        // Block on the condition variable when there is no action to do
        //
        while (core->running && DEQ_IS_EMPTY(core->action_list))
            sys_cond_wait(core->action_cond, core->action_lock);

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
    }

    qd_log(core->log, QD_LOG_INFO, "Router Core thread exited");
    return 0;
}
