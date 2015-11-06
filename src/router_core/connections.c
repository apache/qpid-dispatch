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

static void qdr_connection_opened_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_connection_closed_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_first_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_second_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_detach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

ALLOC_DEFINE(qdr_connection_t);

//==================================================================================
// Internal Functions
//==================================================================================


//==================================================================================
// Interface Functions
//==================================================================================

qdr_connection_t *qdr_connection_opened(qdr_core_t *core, const char *label)
{
    qdr_action_t     *action = qdr_action(qdr_connection_opened_CT);
    qdr_connection_t *conn   = new_qdr_connection_t();

    conn->core         = core;
    conn->user_context = 0;
    conn->label        = label;

    action->args.connection.conn = conn;
    qdr_action_enqueue(core, action);

    return conn;
}


void qdr_connection_closed(qdr_connection_t *conn)
{
    qdr_action_t *action = qdr_action(qdr_connection_closed_CT);
    action->args.connection.conn = conn;
    qdr_action_enqueue(conn->core, action);
}


void qdr_connection_set_context(qdr_connection_t *conn, void *context)
{
    if (conn)
        conn->user_context = context;
}


void *qdr_connection_get_context(qdr_connection_t *conn)
{
    return conn ? conn->user_context : 0;
}


qdr_link_t *qdr_link_first_attach(qdr_connection_t *conn, qd_direction_t dir, pn_terminus_t *source, pn_terminus_t *target)
{
    qdr_action_t *action = qdr_action(qdr_link_first_attach_CT);
    qdr_link_t   *link   = new_qdr_link_t();

    link->core = conn->core;
    link->conn = conn;

    action->args.connection.conn   = conn;
    action->args.connection.link   = link;
    action->args.connection.dir    = dir;
    action->args.connection.source = source;
    action->args.connection.target = target;
    qdr_action_enqueue(conn->core, action);

    return link;
}


void qdr_link_second_attach(qdr_link_t *link, pn_terminus_t *source, pn_terminus_t *target)
{
    qdr_action_t *action = qdr_action(qdr_link_second_attach_CT);

    action->args.connection.link   = link;
    action->args.connection.source = source;
    action->args.connection.target = target;
    qdr_action_enqueue(link->core, action);
}


void qdr_link_detach(qdr_link_t *link, pn_condition_t *condition)
{
    qdr_action_t *action = qdr_action(qdr_link_detach_CT);

    action->args.connection.link      = link;
    action->args.connection.condition = condition;
    qdr_action_enqueue(link->core, action);
}


//==================================================================================
// In-Thread Functions
//==================================================================================

static void qdr_connection_opened_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_connection_t *conn = action->args.connection.conn;
    DEQ_ITEM_INIT(conn);
    DEQ_INSERT_TAIL(core->open_connections, conn);

    //
    // TODO - Look for waypoints that need to be activated now that their connection
    //        is open.
    //

    //
    // TODO - Look for link-route destinations to be activated now that their connection
    //        is open.
    //
}


static void qdr_connection_closed_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_connection_t *conn = action->args.connection.conn;

    //
    // TODO - Deactivate waypoints and link-route destinations for this connection
    //

    //
    // TODO - Clean up links associated with this connection
    //        This involves the links and the dispositions of deliveries stored
    //        with the links.
    //

    DEQ_REMOVE(core->open_connections, conn);
    free_qdr_connection_t(conn);
}


static void qdr_link_first_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
}


static void qdr_link_second_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
}


static void qdr_link_detach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
}


