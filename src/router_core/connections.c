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
#include <qpid/dispatch/amqp.h>

static void qdr_connection_opened_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_connection_closed_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_first_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_second_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_detach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

ALLOC_DEFINE(qdr_connection_t);
ALLOC_DEFINE(qdr_connection_work_t);

//==================================================================================
// Internal Functions
//==================================================================================

qdr_terminus_t *qdr_terminus_router_control(void)
{
    qdr_terminus_t *term = qdr_terminus(0);
    qdr_terminus_add_capability(term, QD_CAPABILITY_ROUTER_CONTROL);
    return term;
}


qdr_terminus_t *qdr_terminus_router_data(void)
{
    qdr_terminus_t *term = qdr_terminus(0);
    qdr_terminus_add_capability(term, QD_CAPABILITY_ROUTER_DATA);
    return term;
}


//==================================================================================
// Interface Functions
//==================================================================================

qdr_connection_t *qdr_connection_opened(qdr_core_t *core, bool incoming, qdr_connection_role_t role, const char *label)
{
    qdr_action_t     *action = qdr_action(qdr_connection_opened_CT);
    qdr_connection_t *conn   = new_qdr_connection_t();

    ZERO(conn);
    conn->core         = core;
    conn->user_context = 0;
    conn->incoming     = incoming;
    conn->role         = role;
    conn->label        = label;
    conn->mask_bit     = -1;
    DEQ_INIT(conn->links);
    DEQ_INIT(conn->work_list);
    conn->work_lock    = sys_mutex();

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


void *qdr_connection_get_context(const qdr_connection_t *conn)
{
    return conn ? conn->user_context : 0;
}


void qdr_connection_process(qdr_connection_t *conn)
{
    qdr_connection_work_list_t  work_list;
    qdr_core_t                 *core = conn->core;

    sys_mutex_lock(conn->work_lock);
    DEQ_MOVE(conn->work_list, work_list);
    sys_mutex_unlock(conn->work_lock);

    qdr_connection_work_t *work = DEQ_HEAD(work_list);
    while (work) {
        DEQ_REMOVE_HEAD(work_list);

        switch (work->work_type) {
        case QDR_CONNECTION_WORK_FIRST_ATTACH :
            core->first_attach_handler(core->user_context, conn, work->link, work->source, work->target);
            break;

        case QDR_CONNECTION_WORK_SECOND_ATTACH :
            core->second_attach_handler(core->user_context, work->link, work->source, work->target);
            break;

        case QDR_CONNECTION_WORK_DETACH :
            core->detach_handler(core->user_context, work->link, work->condition);
            break;
        }

        free_qdr_connection_work_t(work);
        work = DEQ_HEAD(work_list);
    }
}


void qdr_link_set_context(qdr_link_t *link, void *context)
{
    if (link)
        link->user_context = context;
}


void *qdr_link_get_context(const qdr_link_t *link)
{
    return link ? link->user_context : 0;
}


qd_link_type_t qdr_link_type(const qdr_link_t *link)
{
    return link->link_type;
}


qd_direction_t qdr_link_direction(const qdr_link_t *link)
{
    return link->link_direction;
}


qdr_link_t *qdr_link_first_attach(qdr_connection_t *conn, qd_direction_t dir, qdr_terminus_t *source, qdr_terminus_t *target)
{
    qdr_action_t *action = qdr_action(qdr_link_first_attach_CT);
    qdr_link_t   *link   = new_qdr_link_t();

    ZERO(link);
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


void qdr_link_second_attach(qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target)
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


void qdr_connection_handlers(qdr_core_t                *core,
                             void                      *context,
                             qdr_connection_activate_t  activate,
                             qdr_link_first_attach_t    first_attach,
                             qdr_link_second_attach_t   second_attach,
                             qdr_link_detach_t          detach)
{
    core->user_context          = context;
    core->activate_handler      = activate;
    core->first_attach_handler  = first_attach;
    core->second_attach_handler = second_attach;
    core->detach_handler        = detach;
}


//==================================================================================
// In-Thread Functions
//==================================================================================

static void qdr_connection_enqueue_work_CT(qdr_core_t            *core,
                                           qdr_connection_t      *conn,
                                           qdr_connection_work_t *work)
{
    sys_mutex_lock(conn->work_lock);
    DEQ_INSERT_TAIL(conn->work_list, work);
    bool notify = DEQ_SIZE(conn->work_list) == 1;
    sys_mutex_unlock(conn->work_lock);

    if (notify)
        core->activate_handler(core->user_context, conn);
}


static qdr_link_t *qdr_create_link_CT(qdr_core_t       *core,
                                      qdr_connection_t *conn,
                                      qd_link_type_t    link_type,
                                      qd_direction_t    dir,
                                      qdr_terminus_t   *source,
                                      qdr_terminus_t   *target)
{
    //
    // Create a new link, initiated by the router core.  This will involve issuing a first-attach outbound.
    //
    qdr_link_t *link = new_qdr_link_t();
    ZERO(link);

    link->core           = core;
    link->user_context   = 0;
    link->conn           = conn;
    link->link_type      = link_type;
    link->link_direction = dir;

    qdr_connection_work_t *work = new_qdr_connection_work_t();
    ZERO(work);
    work->work_type = QDR_CONNECTION_WORK_FIRST_ATTACH;
    work->link      = link;
    work->source    = source;
    work->target    = target;

    qdr_connection_enqueue_work_CT(core, conn, work);
    return link;
}


static void qdr_connection_opened_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_connection_t *conn = action->args.connection.conn;
    DEQ_ITEM_INIT(conn);
    DEQ_INSERT_TAIL(core->open_connections, conn);

    if (conn->role == QDR_ROLE_NORMAL) {
        //
        // No action needed for NORMAL connections
        //
        return;
    }

    if (conn->role == QDR_ROLE_INTER_ROUTER) {
        //
        // Assign a unique mask-bit to this connection as a reference to be used by
        // the router module
        //
        if (qd_bitmask_first_set(core->neighbor_free_mask, &conn->mask_bit))
            qd_bitmask_clear_bit(core->neighbor_free_mask, conn->mask_bit);
        else {
            qd_log(core->log, QD_LOG_CRITICAL, "Exceeded maximum inter-router connection count");
            return;
        }

        if (!conn->incoming) {
            //
            // The connector-side of inter-router connections is responsible for setting up the
            // inter-router links:  Two (in and out) for control, two for routed-message transfer.
            //
            (void) qdr_create_link_CT(core, conn, QD_LINK_CONTROL, QD_INCOMING, qdr_terminus_router_control(), 0);
            (void) qdr_create_link_CT(core, conn, QD_LINK_CONTROL, QD_OUTGOING, 0, qdr_terminus_router_control());
            (void) qdr_create_link_CT(core, conn, QD_LINK_ROUTER,  QD_INCOMING, qdr_terminus_router_data(), 0);
            (void) qdr_create_link_CT(core, conn, QD_LINK_ROUTER,  QD_OUTGOING, 0, qdr_terminus_router_data());
        }
    }

    //
    // If the role is ON_DEMAND:
    //    Activate waypoints associated with this connection
    //    Activate link-route destinations associated with this connection
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

    //
    // Discard items on the work list
    //

    DEQ_REMOVE(core->open_connections, conn);
    sys_mutex_free(conn->work_lock);
    free_qdr_connection_t(conn);
}


static void qdr_link_first_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    //qdr_connection_t  *conn   = action->args.connection.conn;
    //qdr_link_t        *link   = action->args.connection.link;
    //qd_direction_t     dir    = action->args.connection.dir;
    //qdr_terminus_t    *source = action->args.connection.source;
    //qdr_terminus_t    *target = action->args.connection.target;

    //
    // Cases to be handled:
    //
    // dir = Incoming or Outgoing:
    //    Link is an router-control link
    //       If this isn't an inter-router connection, close the link
    //       Note the control link on the connection
    //       Issue a second attach back to the originating node
    //    Link is addressed (i.e. has a target/source address)
    //       If this is a link-routed address, Issue a first attach to the next hop
    //       If not link-routed, issue a second attach back to the originating node
    //
    // dir = Incoming:
    //    Link is addressed (i.e. has a target address) and not link-routed
    //       Lookup/Create address in the address table and associate the link to the address
    //       Issue a second attach back to the originating node
    //    Link is anonymous
    //       Issue a second attach back to the originating node
    //    Issue credit for the inbound fifo
    //
    // dir = Outgoing:
    //    Link is a router-control link
    //       Associate the link with the router-hello address
    //       Associate the link with the link-mask-bit being used by the router
    //    Link is addressed (i.e. has a non-dynamic source address)
    //       If the address is appropriate for distribution, add it to the address table as a local destination
    //       If this is the first local dest for this address, notify the router (mobile_added)
    //       Issue a second attach back to the originating node
    //
}


static void qdr_link_second_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    //qdr_link_t     *link   = action->args.connection.link;
    //qdr_terminus_t *source = action->args.connection.source;
    //qdr_terminus_t *target = action->args.connection.target;

    //
    // Cases to be handled:
    //
    // Link is a router-control link:
    //    Note the control link on the connection
    //    Associate the link with the router-hello address
    //    Associate the link with the link-mask-bit being used by the router
    // Link is link-routed:
    //    Propagate the second attach back toward the originating node
    // Link is Incoming:
    //    Issue credit for the inbound fifo
    //
}


static void qdr_link_detach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    //qdr_link_t     *link      = action->args.connection.link;
    //pn_condition_t *condition = action->args.connection.condition;

    //
    // Cases to be handled:
    //
    // Link is link-routed:
    //    Propagate the detach along the link-chain
    // Link is half-detached and not link-routed:
    //    Issue a detach back to the originating node
    // Link is fully detached:
    //    Free the qdr_link object
    //    Remove any address linkages associated with this link
    //       If the last dest for a local address is lost, notify the router (mobile_removed)
    // Link is a router-control link:
    //    Issue a link-lost indication to the router
    //
}


