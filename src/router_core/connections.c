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

#include <qpid/dispatch/router_core.h>
#include "route_control.h"
#include <qpid/dispatch/amqp.h>
#include <stdio.h>
#include <strings.h>
#include "router_core_private.h"

static void qdr_connection_opened_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_connection_closed_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_inbound_first_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_inbound_second_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_inbound_detach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_delete_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

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

qdr_connection_t *qdr_connection_opened(qdr_core_t            *core,
                                        bool                   incoming,
                                        qdr_connection_role_t  role,
                                        int                    cost,
                                        uint64_t               management_id,
                                        const char            *label,
                                        const char            *remote_container_id,
                                        bool                   strip_annotations_in,
                                        bool                   strip_annotations_out,
                                        int                    link_capacity,
                                        const char            *vhost,
                                        qdr_connection_info_t *connection_info,
                                        qdr_connection_bind_context_t context_binder,
                                        void                  *bind_token)
{
    qdr_action_t     *action = qdr_action(qdr_connection_opened_CT, "connection_opened");
    qdr_connection_t *conn   = new_qdr_connection_t();

    ZERO(conn);
    conn->identity              = management_id;
    conn->connection_info       = connection_info;
    conn->core                  = core;
    conn->user_context          = 0;
    conn->incoming              = incoming;
    conn->role                  = role;
    conn->inter_router_cost     = cost;
    conn->strip_annotations_in  = strip_annotations_in;
    conn->strip_annotations_out = strip_annotations_out;
    conn->link_capacity         = link_capacity;
    conn->mask_bit              = -1;
    DEQ_INIT(conn->links);
    DEQ_INIT(conn->work_list);
    conn->connection_info->role = conn->role;
    conn->work_lock = sys_mutex();

    if (vhost) {
        conn->tenant_space_len = strlen(vhost) + 1;
        conn->tenant_space = (char*) malloc(conn->tenant_space_len + 1);
        strcpy(conn->tenant_space, vhost);
        strcat(conn->tenant_space, "/");
    }

    if (context_binder) {
        context_binder(conn, bind_token);
    }

    action->args.connection.conn             = conn;
    action->args.connection.connection_label = qdr_field(label);
    action->args.connection.container_id     = qdr_field(remote_container_id);
    qdr_action_enqueue(core, action);

    return conn;
}


void qdr_connection_closed(qdr_connection_t *conn)
{
    qdr_action_t *action = qdr_action(qdr_connection_closed_CT, "connection_closed");
    action->args.connection.conn = conn;
    qdr_action_enqueue(conn->core, action);
}


void qdr_connection_set_context(qdr_connection_t *conn, void *context)
{
    if (conn) {
        conn->user_context = context;
    }
}

qdr_connection_info_t *qdr_connection_info(bool             is_encrypted,
                                           bool             is_authenticated,
                                           bool             opened,
                                           char            *sasl_mechanisms,
                                           qd_direction_t   dir,
                                           const char      *host,
                                           const char      *ssl_proto,
                                           const char      *ssl_cipher,
                                           const char      *user,
                                           const char      *container,
                                           pn_data_t       *connection_properties,
                                           int              ssl_ssf,
                                           bool             ssl)
{
    qdr_connection_info_t *connection_info = new_qdr_connection_info_t();
    ZERO(connection_info);
    connection_info->is_encrypted          = is_encrypted;
    connection_info->is_authenticated      = is_authenticated;
    connection_info->opened                = opened;
    connection_info->container             = container;

    if (sasl_mechanisms)
        connection_info->sasl_mechanisms = strdup(sasl_mechanisms);
    connection_info->dir = dir;
    if (host)
        connection_info->host = strdup(host);
    if (ssl_proto)
        connection_info->ssl_proto = strdup(ssl_proto);
    if (ssl_cipher)
        connection_info->ssl_cipher = strdup(ssl_cipher);
    if (user)
        connection_info->user = strdup(user);

    pn_data_t *qdr_conn_properties = pn_data(0);
    pn_data_copy(qdr_conn_properties, connection_properties);

    connection_info->connection_properties = qdr_conn_properties;
    connection_info->ssl_ssf = ssl_ssf;
    connection_info->ssl     = ssl;

    return connection_info;
}


static void qdr_connection_info_free(qdr_connection_info_t *ci)
{
    free(ci->sasl_mechanisms);
    free(ci->host);
    free(ci->ssl_proto);
    free(ci->ssl_cipher);
    free(ci->user);
    pn_data_free(ci->connection_properties);
    free_qdr_connection_info_t(ci);
}


void *qdr_connection_get_context(const qdr_connection_t *conn)
{
    return conn ? conn->user_context : NULL;
}


const char *qdr_connection_get_tenant_space(const qdr_connection_t *conn, int *len)
{
    *len = conn ? conn->tenant_space_len : 0;
    return conn ? conn->tenant_space : 0;
}


int qdr_connection_process(qdr_connection_t *conn)
{
    qdr_connection_work_list_t  work_list;
    qdr_core_t                 *core = conn->core;

    qdr_link_ref_t *ref;
    qdr_link_t     *link;
    bool            free_link;

    int event_count = 0;

    sys_mutex_lock(conn->work_lock);
    DEQ_MOVE(conn->work_list, work_list);
    sys_mutex_unlock(conn->work_lock);

    event_count += DEQ_SIZE(work_list);
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
        }

        qdr_terminus_free(work->source);
        qdr_terminus_free(work->target);
        free_qdr_connection_work_t(work);

        work = DEQ_HEAD(work_list);
    }

    do {
        qdr_link_work_t *link_work;
        free_link = false;

        sys_mutex_lock(conn->work_lock);
        ref = DEQ_HEAD(conn->links_with_work);
        if (ref) {
            link = ref->link;
            qdr_del_link_ref(&conn->links_with_work, ref->link, QDR_LINK_LIST_CLASS_WORK);

            link_work = DEQ_HEAD(link->work_list);
            if (link_work) {
                DEQ_REMOVE_HEAD(link->work_list);
                link_work->processing = true;
            }
        } else
            link = 0;
        sys_mutex_unlock(conn->work_lock);

        if (link) {

            //
            // Handle disposition/settlement updates
            //
            qdr_delivery_ref_list_t updated_deliveries;
            sys_mutex_lock(conn->work_lock);
            DEQ_MOVE(link->updated_deliveries, updated_deliveries);
            sys_mutex_unlock(conn->work_lock);

            qdr_delivery_ref_t *dref = DEQ_HEAD(updated_deliveries);
            while (dref) {
                core->delivery_update_handler(core->user_context, dref->dlv, dref->dlv->disposition, dref->dlv->settled);
                qdr_delivery_decref(core, dref->dlv, "qdr_connection_process - remove from updated list");
                qdr_del_delivery_ref(&updated_deliveries, dref);
                dref = DEQ_HEAD(updated_deliveries);
                event_count++;
            }

            while (link_work) {
                switch (link_work->work_type) {
                case QDR_LINK_WORK_DELIVERY :
                    {
                        int count = core->push_handler(core->user_context, link, link_work->value);
                        assert(count <= link_work->value);
                        link_work->value -= count;
                        break;
                    }

                case QDR_LINK_WORK_FLOW :
                    if (link_work->value > 0)
                        core->flow_handler(core->user_context, link, link_work->value);
                    if      (link_work->drain_action == QDR_LINK_WORK_DRAIN_ACTION_SET)
                        core->drain_handler(core->user_context, link, true);
                    else if (link_work->drain_action == QDR_LINK_WORK_DRAIN_ACTION_CLEAR)
                        core->drain_handler(core->user_context, link, false);
                    else if (link_work->drain_action == QDR_LINK_WORK_DRAIN_ACTION_DRAINED)
                        core->drained_handler(core->user_context, link);
                    break;

                case QDR_LINK_WORK_FIRST_DETACH :
                    core->detach_handler(core->user_context, link, link_work->error, true, link_work->close_link);
                    break;

                case QDR_LINK_WORK_SECOND_DETACH :
                    core->detach_handler(core->user_context, link, link_work->error, false, link_work->close_link);
                    free_link = true;
                    break;
                }

                sys_mutex_lock(conn->work_lock);
                if (link_work->work_type == QDR_LINK_WORK_DELIVERY && link_work->value > 0) {
                    DEQ_INSERT_HEAD(link->work_list, link_work);
                    link_work->processing = false;
                    link_work = 0; // Halt work processing
                } else {
                    qdr_error_free(link_work->error);
                    free_qdr_link_work_t(link_work);
                    link_work = DEQ_HEAD(link->work_list);
                    if (link_work) {
                        DEQ_REMOVE_HEAD(link->work_list);
                        link_work->processing = true;
                    }
                }
                sys_mutex_unlock(conn->work_lock);
                event_count++;
            }

            if (free_link)
                qdr_link_delete(link);
        }
    } while (free_link || link);

    return event_count;
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


int qdr_link_phase(const qdr_link_t *link)
{
    return link && link->auto_link ? link->auto_link->phase : 0;
}


bool qdr_link_is_anonymous(const qdr_link_t *link)
{
    return link->owning_addr == 0;
}


bool qdr_link_is_routed(const qdr_link_t *link)
{
    return link->connected_link != 0;
}


bool qdr_link_strip_annotations_in(const qdr_link_t *link)
{
    return link->strip_annotations_in;
}


bool qdr_link_strip_annotations_out(const qdr_link_t *link)
{
    return link->strip_annotations_out;
}


void qdr_link_stalled_outbound(qdr_link_t *link)
{
    link->stalled_outbound = true;
}


const char *qdr_link_name(const qdr_link_t *link)
{
    return link->name;
}


qdr_link_t *qdr_link_first_attach(qdr_connection_t *conn,
                                  qd_direction_t    dir,
                                  qdr_terminus_t   *source,
                                  qdr_terminus_t   *target,
                                  const char       *name,
                                  const char       *terminus_addr)
{
    qdr_action_t   *action         = qdr_action(qdr_link_inbound_first_attach_CT, "link_first_attach");
    qdr_link_t     *link           = new_qdr_link_t();
    qdr_terminus_t *local_terminus = dir == QD_OUTGOING ? source : target;

    ZERO(link);
    link->core = conn->core;
    link->identity = qdr_identifier(conn->core);
    link->conn = conn;
    link->name = (char*) malloc(strlen(name) + 1);

    if (terminus_addr) {
         char *term_addr = malloc((strlen(terminus_addr) + 3) * sizeof(char));
         term_addr[0] = '\0';
         strcat(term_addr, "M0");
         strcat(term_addr, terminus_addr);
         link->terminus_addr = term_addr;
    }

    strcpy(link->name, name);
    link->link_direction = dir;
    link->capacity       = conn->link_capacity;
    link->admin_enabled  = true;
    link->oper_status    = QDR_LINK_OPER_DOWN;

    link->strip_annotations_in  = conn->strip_annotations_in;
    link->strip_annotations_out = conn->strip_annotations_out;

    if      (qdr_terminus_has_capability(local_terminus, QD_CAPABILITY_ROUTER_CONTROL))
        link->link_type = QD_LINK_CONTROL;
    else if (qdr_terminus_has_capability(local_terminus, QD_CAPABILITY_ROUTER_DATA))
        link->link_type = QD_LINK_ROUTER;

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
    qdr_action_t *action = qdr_action(qdr_link_inbound_second_attach_CT, "link_second_attach");

    action->args.connection.link   = link;
    action->args.connection.source = source;
    action->args.connection.target = target;
    qdr_action_enqueue(link->core, action);
}


void qdr_link_detach(qdr_link_t *link, qd_detach_type_t dt, qdr_error_t *error)
{
    qdr_action_t *action = qdr_action(qdr_link_inbound_detach_CT, "link_detach");

    action->args.connection.conn   = link->conn;
    action->args.connection.link   = link;
    action->args.connection.error  = error;
    action->args.connection.dt     = dt;
    qdr_action_enqueue(link->core, action);
}


void qdr_link_delete(qdr_link_t *link)
{
    qdr_action_t *action = qdr_action(qdr_link_delete_CT, "link_delete");

    action->args.connection.link = link;
    qdr_action_enqueue(link->core, action);
}


void qdr_connection_handlers(qdr_core_t                *core,
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
                             qdr_delivery_update_t      delivery_update)
{
    core->user_context            = context;
    core->first_attach_handler    = first_attach;
    core->second_attach_handler   = second_attach;
    core->detach_handler          = detach;
    core->flow_handler            = flow;
    core->offer_handler           = offer;
    core->drained_handler         = drained;
    core->drain_handler           = drain;
    core->push_handler            = push;
    core->deliver_handler         = deliver;
    core->delivery_update_handler = delivery_update;
}


//==================================================================================
// In-Thread Functions
//==================================================================================

void qdr_connection_activate_CT(qdr_core_t *core, qdr_connection_t *conn)
{
    if (!conn->in_activate_list) {
        DEQ_INSERT_TAIL_N(ACTIVATE, core->connections_to_activate, conn);
        conn->in_activate_list = true;
    }
}


void qdr_connection_enqueue_work_CT(qdr_core_t            *core,
                                    qdr_connection_t      *conn,
                                    qdr_connection_work_t *work)
{
    sys_mutex_lock(conn->work_lock);
    DEQ_INSERT_TAIL(conn->work_list, work);
    bool notify = DEQ_SIZE(conn->work_list) == 1;
    sys_mutex_unlock(conn->work_lock);

    if (notify)
        qdr_connection_activate_CT(core, conn);
}


void qdr_link_enqueue_work_CT(qdr_core_t      *core,
                              qdr_link_t      *link,
                              qdr_link_work_t *work)
{
    qdr_connection_t *conn = link->conn;

    sys_mutex_lock(conn->work_lock);
    DEQ_INSERT_TAIL(link->work_list, work);
    qdr_add_link_ref(&conn->links_with_work, link, QDR_LINK_LIST_CLASS_WORK);
    sys_mutex_unlock(conn->work_lock);

    qdr_connection_activate_CT(core, conn);
}


#define QDR_DISCRIMINATOR_SIZE 16
static void qdr_generate_discriminator(char *string)
{
    static const char *table = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+_";
    long int rnd1 = random();
    long int rnd2 = random();
    long int rnd3 = random();
    int      idx;
    int      cursor = 0;

    for (idx = 0; idx < 5; idx++) {
        string[cursor++] = table[(rnd1 >> (idx * 6)) & 63];
        string[cursor++] = table[(rnd2 >> (idx * 6)) & 63];
        string[cursor++] = table[(rnd3 >> (idx * 6)) & 63];
    }
    string[cursor] = '\0';
}


/**
 * Generate a temporary routable address for a destination connected to this
 * router node.
 */
static void qdr_generate_temp_addr(qdr_core_t *core, char *buffer, size_t length)
{
    char discriminator[QDR_DISCRIMINATOR_SIZE];
    qdr_generate_discriminator(discriminator);
    snprintf(buffer, length, "amqp:/_topo/%s/%s/temp.%s", core->router_area, core->router_id, discriminator);
}


/**
 * Generate a temporary mobile address for a producer connected to this
 * router node.
 */
static void qdr_generate_mobile_addr(qdr_core_t *core, char *buffer, size_t length)
{
    char discriminator[QDR_DISCRIMINATOR_SIZE];
    qdr_generate_discriminator(discriminator);
    snprintf(buffer, length, "amqp:/_$temp.%s", discriminator);
}


/**
 * Generate a link name
 */
static void qdr_generate_link_name(const char *label, char *buffer, size_t length)
{
    char discriminator[QDR_DISCRIMINATOR_SIZE];
    qdr_generate_discriminator(discriminator);
    snprintf(buffer, length, "%s.%s", label, discriminator);
}


static void qdr_link_cleanup_deliveries_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link)
{
    //
    // Clean up the lists of deliveries on this link
    //
    qdr_delivery_ref_list_t updated_deliveries;
    qdr_delivery_list_t     undelivered;
    qdr_delivery_list_t     unsettled;
    qdr_delivery_list_t     settled;

    sys_mutex_lock(conn->work_lock);
    DEQ_MOVE(link->updated_deliveries, updated_deliveries);

    DEQ_MOVE(link->undelivered, undelivered);
    qdr_delivery_t *d = DEQ_HEAD(undelivered);
    while (d) {
        assert(d->where == QDR_DELIVERY_IN_UNDELIVERED);
        d->where = QDR_DELIVERY_NOWHERE;
        d = DEQ_NEXT(d);
    }

    DEQ_MOVE(link->unsettled, unsettled);
    d = DEQ_HEAD(unsettled);
    while (d) {
        assert(d->where == QDR_DELIVERY_IN_UNSETTLED);
        d->where = QDR_DELIVERY_NOWHERE;
        d = DEQ_NEXT(d);
    }

    DEQ_MOVE(link->settled, settled);
    d = DEQ_HEAD(settled);
    while (d) {
        assert(d->where == QDR_DELIVERY_IN_SETTLED);
        d->where = QDR_DELIVERY_NOWHERE;
        d = DEQ_NEXT(d);
    }
    sys_mutex_unlock(conn->work_lock);

    //
    // Free all the 'updated' references
    //
    qdr_delivery_ref_t *ref = DEQ_HEAD(updated_deliveries);
    while (ref) {
        //
        // Now our reference
        //
        qdr_delivery_decref_CT(core, ref->dlv, "qdr_link_cleanup_deliveries_CT - remove from updated list");
        qdr_del_delivery_ref(&updated_deliveries, ref);
        ref = DEQ_HEAD(updated_deliveries);
    }

    //
    // Free the undelivered deliveries.  If this is an incoming link, the
    // undelivereds can simply be destroyed.  If it's an outgoing link, the
    // undelivereds' peer deliveries need to be released.
    //
    qdr_delivery_t *dlv = DEQ_HEAD(undelivered);
    qdr_delivery_t *peer;
    while (dlv) {
        DEQ_REMOVE_HEAD(undelivered);
        peer = qdr_delivery_first_peer_CT(dlv);
        while (peer) {
            qdr_delivery_release_CT(core, peer);
            qdr_delivery_unlink_peers_CT(core, dlv, peer);
            peer = qdr_delivery_next_peer_CT(dlv);
        }

        //
        // Now the undelivered-list reference
        //
        qdr_delivery_decref_CT(core, dlv, "qdr_link_cleanup_deliveries_CT - remove from undelivered list");

        dlv = DEQ_HEAD(undelivered);
    }

    //
    // Free the unsettled deliveries.
    //
    dlv = DEQ_HEAD(unsettled);
    while (dlv) {
        DEQ_REMOVE_HEAD(unsettled);

        if (dlv->tracking_addr) {
            dlv->tracking_addr->outstanding_deliveries[dlv->tracking_addr_bit]--;
            dlv->tracking_addr->tracked_deliveries--;

            if (dlv->tracking_addr->tracked_deliveries == 0)
                qdr_check_addr_CT(core, dlv->tracking_addr, false);

            dlv->tracking_addr = 0;
        }

        if (!qdr_delivery_receive_complete(dlv)) {
            qdr_delivery_set_aborted(dlv, true);
            qdr_deliver_continue_peers_CT(core, dlv);
        }

        peer = qdr_delivery_first_peer_CT(dlv);
        while (peer) {
            if (link->link_direction == QD_OUTGOING)
                qdr_delivery_failed_CT(core, peer);
            qdr_delivery_unlink_peers_CT(core, dlv, peer);
            peer = qdr_delivery_next_peer_CT(dlv);
        }

        //
        // Now the unsettled-list reference
        //
        qdr_delivery_decref_CT(core, dlv, "qdr_link_cleanup_deliveries_CT - remove from unsettled list");

        dlv = DEQ_HEAD(unsettled);
    }

    //Free/unlink/decref the settled deliveries.
    dlv = DEQ_HEAD(settled);
    while (dlv) {
        DEQ_REMOVE_HEAD(settled);

        if (!qdr_delivery_receive_complete(dlv)) {
            qdr_delivery_set_aborted(dlv, true);
            qdr_deliver_continue_peers_CT(core, dlv);
        }

        peer = qdr_delivery_first_peer_CT(dlv);
        qdr_delivery_t *next_peer = 0;
        while (peer) {
            next_peer = qdr_delivery_next_peer_CT(dlv);
            qdr_delivery_unlink_peers_CT(core, dlv, peer);
            peer = next_peer;
        }

        // This decref is for the removing the delivery from the settled list
        qdr_delivery_decref_CT(core, dlv, "qdr_link_cleanup_deliveries_CT - remove from settled list");
        dlv = DEQ_HEAD(settled);
    }
}


static void qdr_link_abort_undelivered_CT(qdr_core_t *core, qdr_link_t *link)
{
    assert(link->link_direction == QD_OUTGOING);

    qdr_connection_t *conn = link->conn;

    sys_mutex_lock(conn->work_lock);
    qdr_delivery_t *dlv = DEQ_HEAD(link->undelivered);
    while (dlv) {
        if (!qdr_delivery_receive_complete(dlv))
            qdr_delivery_set_aborted(dlv, true);
        dlv = DEQ_NEXT(dlv);
    }
    sys_mutex_unlock(conn->work_lock);
}


static void qdr_link_cleanup_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link)
{
    //
    // Remove the link from the master list of links
    //
    DEQ_REMOVE(core->open_links, link);

    //
    // If the link has a connected peer, unlink the peer
    //
    if (link->connected_link) {
        link->connected_link->connected_link = 0;
        link->connected_link = 0;
    }

    //
    // If this link is involved in inter-router communication, remove its reference
    // from the core mask-bit tables
    //
    if (qd_bitmask_valid_bit_value(conn->mask_bit)) {
        if (link->link_type == QD_LINK_CONTROL)
            core->control_links_by_mask_bit[conn->mask_bit] = 0;
        if (link->link_type == QD_LINK_ROUTER)
            core->data_links_by_mask_bit[conn->mask_bit] = 0;
    }

    //
    // Clean up the work list
    //
    qdr_link_work_list_t work_list;

    sys_mutex_lock(conn->work_lock);
    DEQ_MOVE(link->work_list, work_list);
    sys_mutex_unlock(conn->work_lock);

    //
    // Free the work list
    //
    qdr_link_work_t *link_work = DEQ_HEAD(work_list);
    while (link_work) {
        DEQ_REMOVE_HEAD(work_list);
        qdr_error_free(link_work->error);
        free_qdr_link_work_t(link_work);
        link_work = DEQ_HEAD(work_list);
    }

    //
    // Clean up any remaining deliveries
    //
    qdr_link_cleanup_deliveries_CT(core, conn, link);

    //
    // Remove the reference to this link in the connection's reference lists
    //
    qdr_del_link_ref(&conn->links, link, QDR_LINK_LIST_CLASS_CONNECTION);
    sys_mutex_lock(conn->work_lock);
    qdr_del_link_ref(&conn->links_with_work, link, QDR_LINK_LIST_CLASS_WORK);
    sys_mutex_unlock(conn->work_lock);

    //
    // Free the link's name and terminus_addr
    //
    free(link->name);
    free(link->terminus_addr);
    link->name = 0;
}


qdr_link_t *qdr_create_link_CT(qdr_core_t       *core,
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
    link->identity       = qdr_identifier(core);
    link->user_context   = 0;
    link->conn           = conn;
    link->link_type      = link_type;
    link->link_direction = dir;
    link->capacity       = conn->link_capacity;
    link->name           = (char*) malloc(QDR_DISCRIMINATOR_SIZE + 8);
    link->terminus_addr  = 0;
    qdr_generate_link_name("qdlink", link->name, QDR_DISCRIMINATOR_SIZE + 8);
    link->admin_enabled  = true;
    link->oper_status    = QDR_LINK_OPER_DOWN;

    link->strip_annotations_in  = conn->strip_annotations_in;
    link->strip_annotations_out = conn->strip_annotations_out;

    DEQ_INSERT_TAIL(core->open_links, link);
    qdr_add_link_ref(&conn->links, link, QDR_LINK_LIST_CLASS_CONNECTION);

    qdr_connection_work_t *work = new_qdr_connection_work_t();
    ZERO(work);
    work->work_type = QDR_CONNECTION_WORK_FIRST_ATTACH;
    work->link      = link;
    work->source    = source;
    work->target    = target;

    qdr_connection_enqueue_work_CT(core, conn, work);
    return link;
}


void qdr_link_outbound_detach_CT(qdr_core_t *core, qdr_link_t *link, qdr_error_t *error, qdr_condition_t condition, bool close)
{
    qdr_link_work_t *work = new_qdr_link_work_t();
    ZERO(work);
    work->work_type  = ++link->detach_count == 1 ? QDR_LINK_WORK_FIRST_DETACH : QDR_LINK_WORK_SECOND_DETACH;
    work->close_link = close;

    if (error)
        work->error = error;
    else {
        switch (condition) {
        case QDR_CONDITION_NO_ROUTE_TO_DESTINATION:
            work->error = qdr_error("qd:no-route-to-dest", "No route to the destination node");
            break;

        case QDR_CONDITION_ROUTED_LINK_LOST:
            work->error = qdr_error("qd:routed-link-lost", "Connectivity to the peer container was lost");
            break;

        case QDR_CONDITION_FORBIDDEN:
            work->error = qdr_error("qd:forbidden", "Connectivity to the node is forbidden");
            break;

        case QDR_CONDITION_WRONG_ROLE:
            work->error = qdr_error("qd:connection-role", "Link attach forbidden on inter-router connection");
            break;

        case QDR_CONDITION_COORDINATOR_PRECONDITION_FAILED:
            work->error = qdr_error(QD_AMQP_COND_PRECONDITION_FAILED, "The router can't coordinate transactions by itself, a "
                                                            "linkRoute to a coordinator must be configured to use transactions.");
            break;

        case QDR_CONDITION_NONE:
            work->error = 0;
            break;
        }
    }

    qdr_link_enqueue_work_CT(core, link, work);
}


static void qdr_link_outbound_second_attach_CT(qdr_core_t *core, qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target)
{
    qdr_connection_work_t *work = new_qdr_connection_work_t();
    ZERO(work);
    work->work_type = QDR_CONNECTION_WORK_SECOND_ATTACH;
    work->link      = link;
    work->source    = source;
    work->target    = target;

    link->oper_status = QDR_LINK_OPER_UP;

    qdr_connection_enqueue_work_CT(core, link->conn, work);
}


qd_address_treatment_t qdr_treatment_for_address_CT(qdr_core_t *core, qdr_connection_t *conn, qd_iterator_t *iter, int *in_phase, int *out_phase)
{
    qdr_address_config_t *addr = 0;
    qd_iterator_view_t old_view = qd_iterator_get_view(iter);

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_WITH_SPACE);
    if (conn && conn->tenant_space)
        qd_iterator_annotate_space(iter, conn->tenant_space, conn->tenant_space_len);
    qd_parse_tree_retrieve_match(core->addr_parse_tree, iter, (void **) &addr);
    qd_iterator_annotate_prefix(iter, '\0');
    qd_iterator_reset_view(iter, old_view);

    if (in_phase)  *in_phase  = addr ? addr->in_phase  : 0;
    if (out_phase) *out_phase = addr ? addr->out_phase : 0;


    return addr ? addr->treatment : core->qd->default_treatment;
}


qd_address_treatment_t qdr_treatment_for_address_hash_CT(qdr_core_t *core, qd_iterator_t *iter)
{
#define HASH_STORAGE_SIZE 1000
    char  storage[HASH_STORAGE_SIZE + 1];
    char *copy    = storage;
    bool  on_heap = false;
    int   length  = qd_iterator_length(iter);
    qd_address_treatment_t trt = core->qd->default_treatment;

    if (length > HASH_STORAGE_SIZE) {
        copy    = (char*) malloc(length + 1);
        on_heap = true;
    }

    qd_iterator_strncpy(iter, copy, length + 1);

    if (QDR_IS_LINK_ROUTE(copy[0]))
        //
        // Handle the link-route address case
        // TODO - put link-routes into the config table with a different prefix from 'Z'
        //
        trt = QD_TREATMENT_LINK_BALANCED;

    else if (copy[0] == 'M') {
        //
        // Handle the mobile address case
        //
        qd_iterator_t *config_iter = qd_iterator_string(&copy[2], ITER_VIEW_ADDRESS_WITH_SPACE);
        qdr_address_config_t *addr = 0;
        qd_parse_tree_retrieve_match(core->addr_parse_tree, config_iter, (void **) &addr);
        if (addr)
            trt = addr->treatment;
        qd_iterator_free(config_iter);
    }

    if (on_heap)
        free(copy);

    return trt;
}


/**
 * Check an address to see if it no longer has any associated destinations.
 * Depending on its policy, the address may be eligible for being closed out
 * (i.e. Logging its terminal statistics and freeing its resources).
 */
void qdr_check_addr_CT(qdr_core_t *core, qdr_address_t *addr, bool was_local)
{
    if (addr == 0)
        return;

    //
    // If we have just removed a local linkage and it was the last local linkage,
    // we need to notify the router module that there is no longer a local
    // presence of this address.
    //
    if (was_local && DEQ_SIZE(addr->rlinks) == 0) {
        const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
        if (key && *key == 'M')
            qdr_post_mobile_removed_CT(core, key);
    }

    //
    // If the address has no in-process consumer or destinations, it should be
    // deleted.
    //
    if (DEQ_SIZE(addr->subscriptions) == 0 && DEQ_SIZE(addr->rlinks) == 0 && DEQ_SIZE(addr->inlinks) == 0 &&
        qd_bitmask_cardinality(addr->rnodes) == 0 && addr->ref_count == 0 && !addr->block_deletion &&
        addr->tracked_deliveries == 0) {
        qdr_core_remove_address(core, addr);
    }
}


/**
 * qdr_lookup_terminus_address_CT
 *
 * Lookup a terminus address in the route table and possibly create a new address
 * if no match is found.
 *
 * @param core Pointer to the core object
 * @param dir Direction of the link for the terminus
 * @param conn The connection over which the terminus was attached
 * @param terminus The terminus containing the addressing information to be looked up
 * @param create_if_not_found Iff true, return a pointer to a newly created address record
 * @param accept_dynamic Iff true, honor the dynamic flag by creating a dynamic address
 * @param [out] link_route True iff the lookup indicates that an attach should be routed
 * @return Pointer to an address record or 0 if none is found
 */
static qdr_address_t *qdr_lookup_terminus_address_CT(qdr_core_t       *core,
                                                     qd_direction_t    dir,
                                                     qdr_connection_t *conn,
                                                     qdr_terminus_t   *terminus,
                                                     bool              create_if_not_found,
                                                     bool              accept_dynamic,
                                                     bool             *link_route,
                                                     bool             *unavailable)
{
    qdr_address_t *addr = 0;

    //
    // Unless expressly stated, link routing is not indicated for this terminus.
    //
    *link_route = false;
    *unavailable = false;

    if (qdr_terminus_is_dynamic(terminus)) {
        //
        // The terminus is dynamic.  Check to see if there is an address provided
        // in the dynamic node properties.  If so, look that address up as a link-routed
        // destination.
        //
        qd_iterator_t *dnp_address = qdr_terminus_dnp_address(terminus);
        if (dnp_address) {
            qd_iterator_reset_view(dnp_address, ITER_VIEW_ADDRESS_WITH_SPACE);
            if (conn->tenant_space)
                qd_iterator_annotate_space(dnp_address, conn->tenant_space, conn->tenant_space_len);
            qd_parse_tree_retrieve_match(core->link_route_tree[dir], dnp_address, (void**) &addr);

            if (addr && conn->tenant_space) {
                //
                // If this link is in a tenant space, translate the dnp address to
                // the fully-qualified view
                //
                qdr_terminus_set_dnp_address_iterator(terminus, dnp_address);
            }

            qd_iterator_free(dnp_address);
            *link_route = true;
            return addr;
        }

        //
        // The dynamic terminus has no address in the dynamic-node-propteries.  If we are
        // permitted to generate dynamic addresses, create a new address that is local to
        // this router and insert it into the address table with a hash index.
        //
        if (!accept_dynamic)
            return 0;

        char temp_addr[200];
        bool generating = true;
        while (generating) {
            //
            // The address-generation process is performed in a loop in case the generated
            // address collides with a previously generated address (this should be _highly_
            // unlikely).
            //
            if (dir == QD_OUTGOING)
                qdr_generate_temp_addr(core, temp_addr, 200);
            else
                qdr_generate_mobile_addr(core, temp_addr, 200);

            qd_iterator_t *temp_iter = qd_iterator_string(temp_addr, ITER_VIEW_ADDRESS_HASH);
            qd_hash_retrieve(core->addr_hash, temp_iter, (void**) &addr);
            if (!addr) {
                addr = qdr_address_CT(core, QD_TREATMENT_ANYCAST_BALANCED);
                qd_hash_insert(core->addr_hash, temp_iter, addr, &addr->hash_handle);
                DEQ_INSERT_TAIL(core->addrs, addr);
                qdr_terminus_set_address(terminus, temp_addr);
                generating = false;
            }
            qd_iterator_free(temp_iter);
        }
        return addr;
    }

    //
    // If the terminus is anonymous, there is no address to look up.
    //
    if (qdr_terminus_is_anonymous(terminus))
        return 0;

    //
    // The terminus has a non-dynamic address that we need to look up.  First, look for
    // a link-route destination for the address.
    //
    qd_iterator_t *iter = qdr_terminus_get_address(terminus);
    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_WITH_SPACE);
    if (conn->tenant_space)
        qd_iterator_annotate_space(iter, conn->tenant_space, conn->tenant_space_len);
    qd_parse_tree_retrieve_match(core->link_route_tree[dir], iter, (void**) &addr);
    if (addr) {
        *link_route = true;

        //
        // If this link is in a tenant space, translate the terminus address to
        // the fully-qualified view
        //
        if (conn->tenant_space) {
            qdr_terminus_set_address_iterator(terminus, iter);
        }
        return addr;
    }

    //
    // There was no match for a link-route destination, look for a message-route address.
    //
    int in_phase;
    int out_phase;
    int addr_phase;
    qd_address_treatment_t treat = qdr_treatment_for_address_CT(core, conn, iter, &in_phase, &out_phase);

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, '\0'); // Cancel previous override
    addr_phase = dir == QD_INCOMING ? in_phase : out_phase;
    qd_iterator_annotate_phase(iter, (char) addr_phase + '0');

    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);

    if (addr && addr->treatment == QD_TREATMENT_UNAVAILABLE)
        *unavailable = true;

    if (!addr && create_if_not_found) {
        addr = qdr_address_CT(core, treat);
        if (addr) {
            qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
            DEQ_INSERT_TAIL(core->addrs, addr);
        }

        if (!addr && treat == QD_TREATMENT_UNAVAILABLE)
            *unavailable = true;
    }

    if (qdr_terminus_is_coordinator(terminus))
        *unavailable = false;

    return addr;
}


static void qdr_connection_opened_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (!discard) {
        qdr_connection_t *conn = action->args.connection.conn;
        DEQ_ITEM_INIT(conn);
        DEQ_INSERT_TAIL(core->open_connections, conn);

        if (conn->role == QDR_ROLE_NORMAL) {
            //
            // No action needed for NORMAL connections
            //
            qdr_field_free(action->args.connection.connection_label);
            qdr_field_free(action->args.connection.container_id);
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
                conn->role = QDR_ROLE_NORMAL;
                qdr_field_free(action->args.connection.connection_label);
                qdr_field_free(action->args.connection.container_id);
                return;
            }

            if (!conn->incoming) {
                //
                // The connector-side of inter-router connections is responsible for setting up the
                // inter-router links:  Two (in and out) for control, two for routed-message transfer.
                //
                (void) qdr_create_link_CT(core, conn, QD_LINK_CONTROL, QD_INCOMING, qdr_terminus_router_control(), qdr_terminus_router_control());
                (void) qdr_create_link_CT(core, conn, QD_LINK_CONTROL, QD_OUTGOING, qdr_terminus_router_control(), qdr_terminus_router_control());
                (void) qdr_create_link_CT(core, conn, QD_LINK_ROUTER,  QD_INCOMING, qdr_terminus_router_data(), qdr_terminus_router_data());
                (void) qdr_create_link_CT(core, conn, QD_LINK_ROUTER,  QD_OUTGOING, qdr_terminus_router_data(), qdr_terminus_router_data());
            }
        }

        if (conn->role == QDR_ROLE_ROUTE_CONTAINER) {
            //
            // Notify the route-control module that a route-container connection has opened.
            // There may be routes that need to be activated due to the opening of this connection.
            //

            //
            // If there's a connection label, use it as the identifier.  Otherwise, use the remote
            // container id.
            //
            qdr_field_t *cid = action->args.connection.connection_label ?
                action->args.connection.connection_label : action->args.connection.container_id;
            if (cid)
                qdr_route_connection_opened_CT(core, conn, action->args.connection.container_id, action->args.connection.connection_label);
        }
    }

    qdr_field_free(action->args.connection.connection_label);
    qdr_field_free(action->args.connection.container_id);
}

void qdr_connection_free(qdr_connection_t *conn)
{
    sys_mutex_free(conn->work_lock);
    free(conn->tenant_space);
    qdr_connection_info_free(conn->connection_info);
    free_qdr_connection_t(conn);
}


static void qdr_connection_closed_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_connection_t *conn = action->args.connection.conn;

    //
    // Deactivate routes associated with this connection
    //
    qdr_route_connection_closed_CT(core, conn);

    //
    // Give back the router mask-bit.
    //
    if (conn->role == QDR_ROLE_INTER_ROUTER)
        qd_bitmask_set_bit(core->neighbor_free_mask, conn->mask_bit);

    //
    // Remove the references in the links_with_work list
    //
    qdr_link_ref_t *link_ref = DEQ_HEAD(conn->links_with_work);
    while (link_ref) {
        qdr_del_link_ref(&conn->links_with_work, link_ref->link, QDR_LINK_LIST_CLASS_WORK);
        link_ref = DEQ_HEAD(conn->links_with_work);
    }

    //
    // TODO - Clean up links associated with this connection
    //        This involves the links and the dispositions of deliveries stored
    //        with the links.
    //
    link_ref = DEQ_HEAD(conn->links);
    while (link_ref) {
        qdr_link_t *link = link_ref->link;

        //
        // Clean up the link and all its associated state.
        //
        qdr_link_cleanup_CT(core, conn, link); // link_cleanup disconnects and frees the ref.
        free_qdr_link_t(link);
        link_ref = DEQ_HEAD(conn->links);
    }

    //
    // Discard items on the work list
    //
    qdr_connection_work_t *work = DEQ_HEAD(conn->work_list);
    while (work) {
        DEQ_REMOVE_HEAD(conn->work_list);
        qdr_terminus_free(work->source);
        qdr_terminus_free(work->target);
        free_qdr_connection_work_t(work);
        work = DEQ_HEAD(conn->work_list);
    }

    //
    // If this connection is on the activation list, remove it from the list
    //
    if (conn->in_activate_list) {
        conn->in_activate_list = false;
        DEQ_REMOVE_N(ACTIVATE, core->connections_to_activate, conn);
    }

    DEQ_REMOVE(core->open_connections, conn);
    qdr_connection_free(conn);
}


static void qdr_link_inbound_first_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_connection_t  *conn   = action->args.connection.conn;
    qdr_link_t        *link   = action->args.connection.link;
    qd_direction_t     dir    = action->args.connection.dir;
    qdr_terminus_t    *source = action->args.connection.source;
    qdr_terminus_t    *target = action->args.connection.target;
    bool               success;

    //
    // Put the link into the proper lists for tracking.
    //
    DEQ_INSERT_TAIL(core->open_links, link);
    qdr_add_link_ref(&conn->links, link, QDR_LINK_LIST_CLASS_CONNECTION);

    //
    // Reject any attaches of inter-router links that arrive on connections that are not inter-router.
    //
    if (((link->link_type == QD_LINK_CONTROL || link->link_type == QD_LINK_ROUTER) && conn->role != QDR_ROLE_INTER_ROUTER)) {
        link->link_type = QD_LINK_ENDPOINT; // Demote the link type to endpoint if this is not an inter-router connection
        qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_FORBIDDEN, true);
        qdr_terminus_free(source);
        qdr_terminus_free(target);
        return;
    }

    //
    // Reject ENDPOINT attaches if this is an inter-router connection _and_ there is no
    // CONTROL link on the connection.  This will prevent endpoints from using inter-router
    // listeners for normal traffic but will not prevent routed-links from being established.
    //
    if (conn->role == QDR_ROLE_INTER_ROUTER && link->link_type == QD_LINK_ENDPOINT &&
        core->control_links_by_mask_bit[conn->mask_bit] == 0) {
        qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_WRONG_ROLE, true);
        qdr_terminus_free(source);
        qdr_terminus_free(target);
        return;
    }

    if (dir == QD_INCOMING) {
        //
        // Handle incoming link cases
        //
        switch (link->link_type) {
        case QD_LINK_ENDPOINT: {
            if (qdr_terminus_is_anonymous(target)) {
                link->owning_addr = 0;
                qdr_link_outbound_second_attach_CT(core, link, source, target);
                qdr_link_issue_credit_CT(core, link, link->capacity, false);

            } else {
                //
                // This link has a target address
                //
                bool  link_route;
                bool  unavailable;
                qdr_address_t *addr = qdr_lookup_terminus_address_CT(core, dir, conn, target, true, true, &link_route, &unavailable);
                if (unavailable) {
                    qdr_link_outbound_detach_CT(core, link, qdr_error(QD_AMQP_COND_NOT_FOUND, "Node not found"), 0, true);
                    qdr_terminus_free(source);
                    qdr_terminus_free(target);
                }
                else if (!addr) {
                    //
                    // No route to this destination, reject the link
                    //
                    qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
                    qdr_terminus_free(source);
                    qdr_terminus_free(target);
                }

                else if (link_route) {
                    //
                    // This is a link-routed destination, forward the attach to the next hop
                    //
                    success = qdr_forward_attach_CT(core, addr, link, source, target);
                    if (!success) {
                        qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
                        qdr_terminus_free(source);
                        qdr_terminus_free(target);
                    }

                }

                else if (qdr_terminus_is_coordinator(target)) {
                    //
                    // This target terminus is a coordinator.
                    // If we got here, it means that the coordinator link attach could not be link routed to a broker (or to the next router).
                    // The router should reject this link because the router cannot coordinate transactions itself.
                    //
                    // The attach response should have a null target to indicate refusal and the immediately coming detach.
                    qdr_link_outbound_second_attach_CT(core, link, source, 0);
                    // Now, send back a detach with the error amqp:precondition-failed
                    qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_COORDINATOR_PRECONDITION_FAILED, true);
                }
                else
                    {
                    //
                    // Associate the link with the address.  With this association, it will be unnecessary
                    // to do an address lookup for deliveries that arrive on this link.
                    //
                    link->owning_addr = addr;
                    qdr_add_link_ref(&addr->inlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
                    qdr_link_outbound_second_attach_CT(core, link, source, target);

                    //
                    // Issue the initial credit only if there are destinations for the address or if the address treatment is multicast.
                    //
                    if (DEQ_SIZE(addr->subscriptions)
                            || DEQ_SIZE(addr->rlinks)
                            || qd_bitmask_cardinality(addr->rnodes)
                            || qdr_is_addr_treatment_multicast(addr)) {
                        qdr_link_issue_credit_CT(core, link, link->capacity, false);
                    }
                }
            }
            break;
        }

        case QD_LINK_CONTROL:
            qdr_link_outbound_second_attach_CT(core, link, source, target);
            qdr_link_issue_credit_CT(core, link, link->capacity, false);
            break;

        case QD_LINK_ROUTER:
            qdr_link_outbound_second_attach_CT(core, link, source, target);
            qdr_link_issue_credit_CT(core, link, link->capacity, false);
            break;
        }
    } else {
        //
        // Handle outgoing link cases
        //
        switch (link->link_type) {
        case QD_LINK_ENDPOINT: {
            bool  link_route;
            bool  unavailable;
            qdr_address_t *addr = qdr_lookup_terminus_address_CT(core, dir, conn, source, true, true, &link_route, &unavailable);
            if (unavailable) {
                qdr_link_outbound_detach_CT(core, link, qdr_error(QD_AMQP_COND_NOT_FOUND, "Node not found"), 0, true);
                qdr_terminus_free(source);
                qdr_terminus_free(target);
            }
            else if (!addr) {
                //
                // No route to this destination, reject the link
                //
                qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
                qdr_terminus_free(source);
                qdr_terminus_free(target);
            }

            else if (link_route) {
                //
                // This is a link-routed destination, forward the attach to the next hop
                //
                bool success = qdr_forward_attach_CT(core, addr, link, source, target);
                if (!success) {
                    qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
                    qdr_terminus_free(source);
                    qdr_terminus_free(target);
                }
            }

            else {
                //
                // Associate the link with the address.
                //
                link->owning_addr = addr;
                qdr_add_link_ref(&addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
                if (DEQ_SIZE(addr->rlinks) == 1) {
                    const char *key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
                    if (key && *key == 'M')
                        qdr_post_mobile_added_CT(core, key);
                    qdr_addr_start_inlinks_CT(core, addr);
                }
                qdr_link_outbound_second_attach_CT(core, link, source, target);
            }
            break;
        }

        case QD_LINK_CONTROL:
            link->owning_addr = core->hello_addr;
            qdr_add_link_ref(&core->hello_addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
            core->control_links_by_mask_bit[conn->mask_bit] = link;
            qdr_link_outbound_second_attach_CT(core, link, source, target);
            break;

        case QD_LINK_ROUTER:
            core->data_links_by_mask_bit[conn->mask_bit] = link;
            qdr_link_outbound_second_attach_CT(core, link, source, target);
            break;
        }
    }
}


static void qdr_link_inbound_second_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_link_t       *link   = action->args.connection.link;
    qdr_connection_t *conn   = link->conn;
    qdr_terminus_t   *source = action->args.connection.source;
    qdr_terminus_t   *target = action->args.connection.target;

    link->oper_status = QDR_LINK_OPER_UP;

    //
    // Handle attach-routed links
    //
    if (link->connected_link) {
        qdr_link_outbound_second_attach_CT(core, link->connected_link, source, target);
        return;
    }

    if (link->link_direction == QD_INCOMING) {
        //
        // Handle incoming link cases
        //
        switch (link->link_type) {
        case QD_LINK_ENDPOINT:
            if (link->auto_link) {
                //
                // This second-attach is the completion of an auto-link.  If the attach
                // has a valid source, transition the auto-link to the "active" state.
                //
                if (qdr_terminus_get_address(source)) {
                    link->auto_link->state = QDR_AUTO_LINK_STATE_ACTIVE;
                    qdr_add_link_ref(&link->auto_link->addr->inlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
                    link->owning_addr = link->auto_link->addr;
                }
            }

            //
            // Issue credit if this is an anonymous link or if its address has at least one reachable destination.
            //
            qdr_address_t *addr = link->owning_addr;
            if (!addr || (DEQ_SIZE(addr->subscriptions) || DEQ_SIZE(addr->rlinks) || qd_bitmask_cardinality(addr->rnodes)))
                qdr_link_issue_credit_CT(core, link, link->capacity, false);
            break;

        case QD_LINK_CONTROL:
        case QD_LINK_ROUTER:
            qdr_link_issue_credit_CT(core, link, link->capacity, false);
            break;
        }
    } else {
        //
        // Handle outgoing link cases
        //
        switch (link->link_type) {
        case QD_LINK_ENDPOINT:
            if (link->auto_link) {
                //
                // This second-attach is the completion of an auto-link.  If the attach
                // has a valid target, transition the auto-link to the "active" state.
                //
                if (qdr_terminus_get_address(target)) {
                    link->auto_link->state = QDR_AUTO_LINK_STATE_ACTIVE;
                    qdr_add_link_ref(&link->auto_link->addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
                    link->owning_addr = link->auto_link->addr;
                    if (DEQ_SIZE(link->auto_link->addr->rlinks) == 1) {
                        const char *key = (const char*) qd_hash_key_by_handle(link->auto_link->addr->hash_handle);
                        if (key && *key == 'M')
                            qdr_post_mobile_added_CT(core, key);
                        qdr_addr_start_inlinks_CT(core, link->auto_link->addr);
                    }
                }
            }
            break;

        case QD_LINK_CONTROL:
            link->owning_addr = core->hello_addr;
            qdr_add_link_ref(&core->hello_addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
            core->control_links_by_mask_bit[conn->mask_bit] = link;
            break;

        case QD_LINK_ROUTER:
            core->data_links_by_mask_bit[conn->mask_bit] = link;
            break;
        }
    }

    qdr_terminus_free(source);
    qdr_terminus_free(target);
}


static void qdr_link_inbound_detach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_connection_t *conn      = action->args.connection.conn;
    qdr_link_t       *link      = action->args.connection.link;
    qdr_error_t      *error     = action->args.connection.error;
    qd_detach_type_t  dt        = action->args.connection.dt;
    qdr_address_t    *addr      = link->owning_addr;
    bool              was_local = false;

    //
    // Bump the detach count to track half and full detaches
    //
    link->detach_count++;

    //
    // For routed links, propagate the detach
    //
    if (link->connected_link) {
        //
        // If the connected link is outgoing and there is a delivery on the connected link's undelivered
        // list that is not receive-complete, we must flag that delivery as aborted or it will forever
        // block the propagation of the detach.
        //
        if (link->connected_link->link_direction == QD_OUTGOING)
            qdr_link_abort_undelivered_CT(core, link->connected_link);

        if (dt != QD_LOST)
            qdr_link_outbound_detach_CT(core, link->connected_link, error, QDR_CONDITION_NONE, dt == QD_CLOSED);
        else {
            qdr_link_outbound_detach_CT(core, link->connected_link, 0, QDR_CONDITION_ROUTED_LINK_LOST, false);
            qdr_error_free(error);
        }

        //
        // If the link is completely detached, release its resources
        //
        if (link->detach_count == 2) {
            qdr_link_cleanup_CT(core, conn, link);
            free_qdr_link_t(link);
        }

        return;
    }

    //
    // For auto links, switch the auto link to failed state and record the error
    //
    if (link->auto_link) {
        link->auto_link->link  = 0;
        link->auto_link->state = QDR_AUTO_LINK_STATE_FAILED;
        free(link->auto_link->last_error);
        link->auto_link->last_error = qdr_error_description(error);
    }

    link->owning_addr = 0;

    if (link->link_direction == QD_INCOMING) {
        //
        // Handle incoming link cases
        //
        switch (link->link_type) {
        case QD_LINK_ENDPOINT:
            if (addr) {
                //
                // Drain the undelivered list to ensure deliveries don't get dropped by a detach.
                //
                qdr_drain_inbound_undelivered_CT(core, link, addr);

                //
                // Remove this link from the linked address.
                //
                qdr_del_link_ref(&addr->inlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
            }
            break;

        case QD_LINK_CONTROL:
            break;

        case QD_LINK_ROUTER:
            break;
        }
    } else {
        //
        // Handle outgoing link cases
        //
        switch (link->link_type) {
        case QD_LINK_ENDPOINT:
            if (addr) {
                qdr_del_link_ref(&addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
                was_local = true;
            }
            break;

        case QD_LINK_CONTROL:
            if (conn->role == QDR_ROLE_INTER_ROUTER) {
                qdr_del_link_ref(&core->hello_addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
                core->control_links_by_mask_bit[conn->mask_bit] = 0;
                qdr_post_link_lost_CT(core, conn->mask_bit);
            }
            break;

        case QD_LINK_ROUTER:
            if (conn->role == QDR_ROLE_INTER_ROUTER)
                core->data_links_by_mask_bit[conn->mask_bit] = 0;
            break;
        }
    }

    if (link->detach_count == 1) {
        //
        // Handle the disposition of any deliveries that remain on the link
        //
        qdr_link_cleanup_deliveries_CT(core, conn, link);
        
        //
        // If the detach occurred via protocol, send a detach back.
        //
        if (dt != QD_LOST)
            qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NONE, dt == QD_CLOSED);
    } else {
        qdr_link_cleanup_CT(core, conn, link);
        free_qdr_link_t(link);
    }

    //
    // If there was an address associated with this link, check to see if any address-related
    // cleanup has to be done.
    //
    if (addr)
        qdr_check_addr_CT(core, addr, was_local);

    if (error)
        qdr_error_free(error);
}


static void qdr_link_delete_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_link_t *link = action->args.connection.link;

    if (link && link->conn) {
        qdr_link_cleanup_CT(core, link->conn, link);
        free_qdr_link_t(link);
    }
}

