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
#include "agent_address.h"
#include "agent_waypoint.h"
#include "agent_link.h"
#include <stdio.h>


static const char *qdr_address_columns[] =
    {"name",
     "identity",
     "type",
     "key",
     "inProcess",
     "subscriberCount",
     "remoteCount",
     "hostRouters",
     "deliveriesIngress",
     "deliveriesEgress",
     "deliveriesTransit",
     "deliveriesToContainer",
     "deliveriesFromContainer",
     0};


static const char *qdr_link_columns[] =
    {"linkType",
     "name",
     "linkDir",
     "msgFifoDepth",
     "owningAddr",
     "remoteContainer",
     "linkName",
     "eventFifoDepth",
     "type",
     "identity",
     0};


#define QDR_LINK_COLUMN_COUNT     10

static void qdr_manage_read_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_manage_create_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_manage_delete_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_manage_update_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

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


void qdr_agent_enqueue_response_CT(qdr_core_t *core, qdr_query_t *query)
{
    sys_mutex_lock(core->query_lock);
    DEQ_INSERT_TAIL(core->outgoing_query_list, query);
    bool notify = DEQ_SIZE(core->outgoing_query_list) == 1;
    sys_mutex_unlock(core->query_lock);

    if (notify)
        qd_timer_schedule(core->agent_timer, 0);
}

qdr_query_t *qdr_query(qdr_core_t              *core,
                       void                    *context,
                       qd_router_entity_type_t  type,
                       qd_parsed_field_t       *attribute_names,
                       qd_composed_field_t     *body)
{
    qdr_query_t *query = new_qdr_query_t();

    DEQ_ITEM_INIT(query);
    query->core        = core;
    query->entity_type = type;
    query->context     = context;
    query->body        = body;
    query->next_key    = 0;
    query->next_offset = 0;
    query->more        = false;
    query->status      = 0;

    return query;
}

static void qdrh_query_get_first_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdrh_query_get_next_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_agent_emit_columns(qdr_query_t *query, const char *qdr_columns[], int column_count);
static void qdr_agent_set_columns(qdr_query_t *query, qd_parsed_field_t *attribute_names, const char *qdr_columns[], int column_count);
//==================================================================================
// Interface Functions
//==================================================================================

void qdr_manage_create(qdr_core_t              *core,
                       void                    *context,
                       qd_router_entity_type_t  type,
                       qd_field_iterator_t     *name,
                       qd_parsed_field_t       *in_body,
                       qd_composed_field_t     *out_body)
{
    qdr_action_t *action = qdr_action(qdr_manage_create_CT);

    // Create a query object here
    action->args.agent.query = qdr_query(core, context, type, 0, out_body);
    action->args.agent.name = name;
    action->args.agent.in_body = in_body;

    qdr_action_enqueue(core, action);
}


void qdr_manage_delete(qdr_core_t *core, void  *context,
                       qd_router_entity_type_t  type,
                       qd_field_iterator_t     *name,
                       qd_field_iterator_t     *identity)
{
    qdr_action_t *action = qdr_action(qdr_manage_delete_CT);

    // Create a query object here
    action->args.agent.query = qdr_query(core, context, type, 0, 0);
    action->args.agent.name = name;
    action->args.agent.identity = identity;

    qdr_action_enqueue(core, action);
}


void qdr_manage_read(qdr_core_t *core, void  *context,
                     qd_router_entity_type_t  entity_type,
                     qd_field_iterator_t     *name,
                     qd_field_iterator_t     *identity,
                     qd_composed_field_t     *body)
{
    qdr_action_t *action = qdr_action(qdr_manage_read_CT);

    // Create a query object here
    action->args.agent.query = qdr_query(core, context, entity_type, 0, body);
    action->args.agent.identity  = identity;
    action->args.agent.name = name;

    qdr_action_enqueue(core, action);
}


void qdr_manage_update(qdr_core_t              *core,
                       void                    *context,
                       qd_router_entity_type_t  type,
                       qd_field_iterator_t     *name,
                       qd_field_iterator_t     *identity,
                       qd_parsed_field_t       *in_body,
                       qd_composed_field_t     *out_body)
{
    qdr_action_t *action = qdr_action(qdr_manage_update_CT);

    // Create a query object here
    action->args.agent.query = qdr_query(core, context, type, 0, out_body);
    action->args.agent.name = name;
    action->args.agent.identity = identity;
    action->args.agent.in_body = in_body;

    qdr_action_enqueue(core, action);
}


qdr_query_t *qdr_manage_query(qdr_core_t              *core,
                              void                    *context,
                              qd_router_entity_type_t  type,
                              qd_parsed_field_t       *attribute_names,
                              qd_composed_field_t     *body)
{

    qdr_query_t* query = qdr_query(core, context, type, attribute_names, body);

    switch (query->entity_type) {
    case QD_ROUTER_CONNECTION: break;
    case QD_ROUTER_LINK:       qdr_agent_set_columns(query, attribute_names, qdr_link_columns, QDR_LINK_COLUMN_COUNT);break;
    case QD_ROUTER_ADDRESS:    qdr_agent_set_columns(query, attribute_names, qdr_address_columns, QDR_ADDRESS_COLUMN_COUNT);break;
    case QD_ROUTER_WAYPOINT:   break;
    case QD_ROUTER_EXCHANGE:   break;
    case QD_ROUTER_BINDING:    break;
    }

    return query;
}


void qdr_query_add_attribute_names(qdr_query_t *query)
{
    switch (query->entity_type) {
    case QD_ROUTER_CONNECTION: break;
    case QD_ROUTER_LINK:       qdr_agent_emit_columns(query, qdr_link_columns, QDR_LINK_COLUMN_COUNT);break;
    case QD_ROUTER_ADDRESS:    qdr_agent_emit_columns(query, qdr_address_columns, QDR_ADDRESS_COLUMN_COUNT); break;
    case QD_ROUTER_WAYPOINT:   break;
    case QD_ROUTER_EXCHANGE:   break;
    case QD_ROUTER_BINDING:    break;
    }
}

void qdr_query_get_first(qdr_query_t *query, int offset)
{
    qdr_action_t *action = qdr_action(qdrh_query_get_first_CT);
    action->args.agent.query  = query;
    action->args.agent.offset = offset;
    qdr_action_enqueue(query->core, action);
}


void qdr_query_get_next(qdr_query_t *query)
{
    qdr_action_t *action = qdr_action(qdrh_query_get_next_CT);
    action->args.agent.query = query;
    qdr_action_enqueue(query->core, action);
}


void qdr_query_free(qdr_query_t *query)
{
}

static void qdr_agent_emit_columns(qdr_query_t *query, const char *qdr_columns[], int column_count)
{
    qd_compose_start_list(query->body);
    int i = 0;
    while (query->columns[i] >= 0) {
        assert(query->columns[i] < column_count);
        qd_compose_insert_string(query->body, qdr_columns[query->columns[i]]);
        i++;
    }
    qd_compose_end_list(query->body);
}

static void qdr_agent_set_columns(qdr_query_t *query,
                           qd_parsed_field_t *attribute_names,
                           const char *qdr_columns[],
                           int column_count)
{
    if (!attribute_names ||
        (qd_parse_tag(attribute_names) != QD_AMQP_LIST8 &&
         qd_parse_tag(attribute_names) != QD_AMQP_LIST32) ||
        qd_parse_sub_count(attribute_names) == 0) {
        //
        // Either the attribute_names field is absent, it's not a list, or it's an empty list.
        // In this case, we will include all available attributes.
        //
        int i;
        for (i = 0; i < column_count; i++)
            query->columns[i] = i;
        query->columns[i] = -1;
        return;
    }

    //
    // We have a valid, non-empty attribute list.  Set the columns appropriately.
    //
    uint32_t count = qd_parse_sub_count(attribute_names);
    uint32_t idx;

    for (idx = 0; idx < count; idx++) {
        qd_parsed_field_t *name = qd_parse_sub_value(attribute_names, idx);
        if (!name || (qd_parse_tag(name) != QD_AMQP_STR8_UTF8 && qd_parse_tag(name) != QD_AMQP_STR32_UTF8))
            query->columns[idx] = QDR_AGENT_COLUMN_NULL;
        else {
            int j = 0;
            while (qdr_columns[j]) {
                qd_field_iterator_t *iter = qd_parse_raw(name);
                if (qd_field_iterator_equal(iter, (const unsigned char*) qdr_columns[j])) {
                    query->columns[idx] = j;
                    break;
                }
            }
        }
    }
}


void qdr_manage_handler(qdr_core_t *core, qdr_manage_response_t response_handler)
{
    core->agent_response_handler = response_handler;
}


//==================================================================================
// In-Thread Functions
//==================================================================================

void qdr_agent_setup_CT(qdr_core_t *core)
{
    DEQ_INIT(core->outgoing_query_list);
    core->query_lock  = sys_mutex();
    core->agent_timer = qd_timer(core->qd, qdr_agent_response_handler, core);
}


static void qdr_manage_read_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qd_field_iterator_t     *identity   = action->args.agent.identity;
    qd_field_iterator_t     *name       = action->args.agent.name;
    qdr_query_t             *query      = action->args.agent.query;

    switch (query->entity_type) {
        case QD_ROUTER_CONNECTION: break;
        case QD_ROUTER_LINK:       break;
        case QD_ROUTER_ADDRESS:    qdra_address_get_CT(core, name, identity, query, qdr_address_columns); break;
        case QD_ROUTER_WAYPOINT:   break;
        case QD_ROUTER_EXCHANGE:   break;
        case QD_ROUTER_BINDING:    break;
   }
}

static void qdr_manage_update_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qd_field_iterator_t     *identity   = action->args.agent.identity;
    qd_field_iterator_t     *name       = action->args.agent.name;
    qdr_query_t             *query      = action->args.agent.query;
    qd_parsed_field_t       *in_body    = action->args.agent.in_body;

    switch (query->entity_type) {
        case QD_ROUTER_CONNECTION: break;
        case QD_ROUTER_LINK:       break;
        case QD_ROUTER_ADDRESS:    break;
        case QD_ROUTER_WAYPOINT:   qdra_waypoint_update_CT(core, name, identity, query, in_body); break;
        case QD_ROUTER_EXCHANGE:   break;
        case QD_ROUTER_BINDING:    break;
   }
}


static void qdr_manage_create_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qd_field_iterator_t     *name       = action->args.agent.name;
    qdr_query_t             *query      = action->args.agent.query;
    qd_parsed_field_t       *in_body    = action->args.agent.in_body;
    switch (query->entity_type) {
        case QD_ROUTER_CONNECTION: break;
        case QD_ROUTER_LINK:       break;
        case QD_ROUTER_ADDRESS:    break;
        case QD_ROUTER_WAYPOINT:   qdra_waypoint_create_CT(core, name, query, in_body); break;
        case QD_ROUTER_EXCHANGE:   break;
        case QD_ROUTER_BINDING:    break;
   }
}


static void qdr_manage_delete_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qd_field_iterator_t     *name       = action->args.agent.name;
    qd_field_iterator_t     *identity   = action->args.agent.identity;
    qdr_query_t             *query      = action->args.agent.query;

    switch (query->entity_type) {
        case QD_ROUTER_CONNECTION: break;
        case QD_ROUTER_LINK:       break;
        case QD_ROUTER_ADDRESS:    qdra_address_delete_CT(core, name, identity, query); break;
        case QD_ROUTER_WAYPOINT:   qdra_waypoint_delete_CT(core, name, identity, query); break;
        case QD_ROUTER_EXCHANGE:   break;
        case QD_ROUTER_BINDING:    break;
   }
}




static void qdrh_query_get_first_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_query_t *query  = action->args.agent.query;
    int          offset = action->args.agent.offset;

    if (!discard) {
        switch (query->entity_type) {
            case QD_ROUTER_CONNECTION: break;
            case QD_ROUTER_LINK:       qdra_link_get_first_CT(core, query, offset); break;
            case QD_ROUTER_ADDRESS:    qdra_address_get_first_CT(core, query, offset); break;
            case QD_ROUTER_WAYPOINT:   break;
            case QD_ROUTER_EXCHANGE:   break;
            case QD_ROUTER_BINDING:    break;
        }
    }
}


static void qdrh_query_get_next_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_query_t *query  = action->args.agent.query;

    if (!discard) {
        switch (query->entity_type) {
            case QD_ROUTER_CONNECTION: break;
            case QD_ROUTER_LINK:       qdra_link_get_next_CT(core, query); break;
            case QD_ROUTER_ADDRESS:    qdra_address_get_next_CT(core, query); break;
            case QD_ROUTER_WAYPOINT:   break;
            case QD_ROUTER_EXCHANGE:   break;
            case QD_ROUTER_BINDING:    break;
        }
    }
}



