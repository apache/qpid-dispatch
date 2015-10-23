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

#include <qpid/dispatch/amqp.h>
#include "router_core_private.h"
#include <stdio.h>

static void qdrh_manage_get_first(qdr_core_t *core, qdr_action_t *action, bool discard);

//==================================================================================
// Interface Functions
//==================================================================================

void qdr_manage_create(qdr_core_t *core, void *context, qd_router_entity_type_t type, qd_parsed_field_t *attributes)
{
}


void qdr_manage_delete(qdr_core_t *core, void *context, qd_router_entity_type_t type, qd_parsed_field_t *attributes)
{
}


void qdr_manage_read(qdr_core_t *core, void *context, qd_router_entity_type_t type, qd_parsed_field_t *attributes)
{
}


qdr_query_t *qdr_manage_get_first(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                                  int offset, qd_composed_field_t *body)
{
    qdr_action_t *action = qdr_action(qdrh_manage_get_first);
    qdr_query_t  *query  = new_qdr_query_t();

    query->entity_type = type;
    query->context     = context;
    query->body        = body;
    query->next_key    = 0;
    query->more        = false;
    query->status      = 0;

    action->args.agent.query  = query;
    action->args.agent.offset = offset;

    qdr_action_enqueue(core, action);

    return query;
}


void qdr_manage_get_next(qdr_query_t *query)
{
}


void qdr_query_cancel(qdr_query_t *query)
{
}


void qdr_manage_handler(qdr_core_t *core, qdr_manage_response_t response_handler)
{
    core->agent_response_handler = response_handler;
}


//==================================================================================
// Internal Functions
//==================================================================================

static void qdr_agent_response_handler(void *context)
{
    qdr_core_t  *core = (qdr_core_t*) context;
    qdr_query_t *query;
    bool         done = false;

    while (!done) {
        sys_mutex_lock(core->query_lock);
        query = DEQ_HEAD(core->outgoing_query_list);
        if (query)
            DEQ_REMOVE_HEAD(core->outgoing_query_list);
        done = DEQ_SIZE(core->outgoing_query_list) == 0;
        sys_mutex_unlock(core->query_lock);

        if (query) {
            core->agent_response_handler(query->context, query->status, query->more);
            if (!query->more) {
                if (query->next_key)
                    qdr_field_free(query->next_key);
                free_qdr_query_t(query);
            }
        }
    }
}


static void qdr_agent_enqueue_response(qdr_core_t *core, qdr_query_t *query)
{
    sys_mutex_lock(core->query_lock);
    DEQ_INSERT_TAIL(core->outgoing_query_list, query);
    bool notify = DEQ_SIZE(core->outgoing_query_list) == 1;
    sys_mutex_unlock(core->query_lock);

    if (notify)
        qd_timer_schedule(core->agent_timer, 0);
}


static void qdr_manage_get_first_address(qdr_core_t *core, qdr_query_t *query, int offset)
{
    if (offset >= DEQ_SIZE(core->addrs)) {
        query->more        = false;
        query->status      = &QD_AMQP_OK;
        qdr_agent_enqueue_response(core, query);
        return;
    }
}


//==================================================================================
// In-Thread Functions
//==================================================================================

void qdr_agent_setup(qdr_core_t *core)
{
    DEQ_INIT(core->outgoing_query_list);
    core->query_lock  = sys_mutex();
    core->agent_timer = qd_timer(core->qd, qdr_agent_response_handler, core);
}


static void qdrh_manage_get_first(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_query_t *query  = action->args.agent.query;
    int          offset = action->args.agent.offset;

    if (!discard)
        switch (query->entity_type) {
        case QD_ROUTER_CONNECTION :
            break;

        case QD_ROUTER_LINK :
            break;

        case QD_ROUTER_ADDRESS :
            qdr_manage_get_first_address(core, query, offset);
            break;

        case QD_ROUTER_WAYPOINT :
            break;

        case QD_ROUTER_EXCHANGE :
            break;

        case QD_ROUTER_BINDING :
            break;
        }
}



