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

#include "core_link_endpoint.h"
#include "delivery.h"
#include "route_control.h"
#include "router_core_private.h"

#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/discriminator.h"
#include "qpid/dispatch/router_core.h"
#include "qpid/dispatch/static_assert.h"

#include <inttypes.h>
#include <stdio.h>
#include <strings.h>

static void qdr_connection_opened_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_connection_closed_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_inbound_first_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_inbound_second_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_inbound_detach_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_detach_sent_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_processing_complete_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_detach_sent(qdr_link_t *link);
static void qdr_link_processing_complete(qdr_core_t *core, qdr_link_t *link);

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

qdr_connection_t *qdr_connection_opened(qdr_core_t                   *core,
                                        qdr_protocol_adaptor_t       *protocol_adaptor,
                                        bool                          incoming,
                                        qdr_connection_role_t         role,
                                        int                           cost,
                                        uint64_t                      management_id,
                                        const char                   *label,
                                        const char                   *remote_container_id,
                                        bool                          strip_annotations_in,
                                        bool                          strip_annotations_out,
                                        int                           link_capacity,
                                        const char                   *vhost,
                                        const qd_policy_spec_t       *policy_spec,
                                        qdr_connection_info_t        *connection_info,
                                        qdr_connection_bind_context_t context_binder,
                                        void                         *bind_token)
{
    qdr_action_t     *action = qdr_action(qdr_connection_opened_CT, "connection_opened");
    qdr_connection_t *conn   = new_qdr_connection_t();

    ZERO(conn);
    conn->protocol_adaptor      = protocol_adaptor;
    conn->identity              = management_id;
    conn->connection_info       = connection_info;
    conn->core                  = core;
    conn->user_context          = 0;
    conn->incoming              = incoming;
    conn->role                  = role;
    conn->inter_router_cost     = cost;
    conn->strip_annotations_in  = strip_annotations_in;
    conn->strip_annotations_out = strip_annotations_out;
    conn->policy_spec           = policy_spec;
    conn->link_capacity         = link_capacity;
    conn->mask_bit              = -1;
    conn->admin_status          = QD_CONN_ADMIN_ENABLED;
    conn->oper_status           = QD_CONN_OPER_UP;
    DEQ_INIT(conn->links);
    DEQ_INIT(conn->work_list);
    DEQ_INIT(conn->streaming_link_pool);
    conn->connection_info->role = conn->role;
    conn->work_lock = sys_mutex();
    conn->conn_uptime = qdr_core_uptime_ticks(core);

    if (vhost) {
        conn->tenant_space_len = strlen(vhost) + 1;
        conn->tenant_space = (char*) malloc(conn->tenant_space_len + 1);
        strcpy(conn->tenant_space, vhost);
        strcat(conn->tenant_space, "/");
    }

    if (context_binder) {
        context_binder(conn, bind_token);
    }

    set_safe_ptr_qdr_connection_t(conn, &action->args.connection.conn);
    action->args.connection.connection_label = qdr_field(label);
    action->args.connection.container_id     = qdr_field(remote_container_id);
    if (qd_log_enabled(qd_log_source("PROTOCOL"), QD_LOG_TRACE)) {
        action->args.connection.enable_protocol_trace = true;
    }
    qdr_action_enqueue(core, action);

    char   props_str[1000];
    size_t props_len = 1000;

    pn_data_format(connection_info->connection_properties, props_str, &props_len);

    qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"] Connection Opened: dir=%s host=%s vhost=%s encrypted=%s"
           " auth=%s user=%s container_id=%s props=%s",
           management_id, incoming ? "in" : "out",
           connection_info->host, vhost ? vhost : "", connection_info->is_encrypted ? connection_info->ssl_proto : "no",
           connection_info->is_authenticated ? connection_info->sasl_mechanisms : "no",
           connection_info->user, connection_info->container, props_str);

    return conn;
}


void qdr_connection_closed(qdr_connection_t *conn)
{
    qdr_action_t *action = qdr_action(qdr_connection_closed_CT, "connection_closed");
    set_safe_ptr_qdr_connection_t(conn, &action->args.connection.conn);
    qdr_action_enqueue(conn->core, action);
}

bool qdr_connection_route_container(qdr_connection_t *conn)
{
    return conn->role == QDR_ROLE_ROUTE_CONTAINER;
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
                                           bool             ssl,
                                           const char      *version,
                                           bool             streaming_links)
{
    qdr_connection_info_t *connection_info = new_qdr_connection_info_t();
    ZERO(connection_info);
    connection_info->is_encrypted          = is_encrypted;
    connection_info->is_authenticated      = is_authenticated;
    connection_info->opened                = opened;

    if (container)
        connection_info->container = strdup(container);
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
    if (version)
        connection_info->version = strdup(version);

    pn_data_t *qdr_conn_properties = pn_data(0);
    if (connection_properties)
        pn_data_copy(qdr_conn_properties, connection_properties);

    connection_info->connection_properties = qdr_conn_properties;
    connection_info->ssl_ssf = ssl_ssf;
    connection_info->ssl     = ssl;
    connection_info->streaming_links = streaming_links;
    return connection_info;
}


static void qdr_connection_info_free(qdr_connection_info_t *ci)
{
    free(ci->container);
    free(ci->sasl_mechanisms);
    free(ci->host);
    free(ci->ssl_proto);
    free(ci->ssl_cipher);
    free(ci->user);
    free(ci->version);
    pn_data_free(ci->connection_properties);
    free_qdr_connection_info_t(ci);
}


qdr_connection_role_t qdr_connection_role(const qdr_connection_t *conn)
{
    return conn->role;
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


void qdr_record_link_credit(qdr_core_t *core, qdr_link_t *link)
{
    //
    // Get Proton's view of this link's available credit.
    //
    if (link && link->conn && link->conn->protocol_adaptor) {
        int pn_credit = link->conn->protocol_adaptor->get_credit_handler(link->conn->protocol_adaptor->user_context, link);

        if (link->credit_reported > 0 && pn_credit == 0) {
            //
            // The link has transitioned from positive credit to zero credit.
            //
            link->zero_credit_time = qdr_core_uptime_ticks(core);
        } else if (link->credit_reported == 0 && pn_credit > 0) {
            //
            // The link has transitioned from zero credit to positive credit.
            // Clear the recorded time.
            //
            link->zero_credit_time = 0;
            if (link->reported_as_blocked) {
                link->reported_as_blocked = false;
                core->links_blocked--;
            }
        }

        link->credit_reported = pn_credit;
    }
}


void qdr_close_connection_CT(qdr_core_t *core, qdr_connection_t  *conn)
{
    conn->closed = true;
    conn->error  = qdr_error(QD_AMQP_COND_CONNECTION_FORCED, "Connection forced-closed by management request");
    conn->admin_status = QD_CONN_ADMIN_DELETED;

    //Activate the connection, so the I/O threads can finish the job.
    qdr_connection_activate_CT(core, conn);
}


static void qdr_core_close_connection_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_connection_t  *conn = safe_deref_qdr_connection_t(action->args.connection.conn);

    if (discard || !conn)
        return;

    qdr_close_connection_CT(core, conn);
}



void qdr_core_close_connection(qdr_connection_t *conn)
{
    qdr_action_t *action = qdr_action(qdr_core_close_connection_CT, "qdr_core_close_connection");
    set_safe_ptr_qdr_connection_t(conn, &action->args.connection.conn);
    qdr_action_enqueue(conn->core, action);
}


int qdr_connection_process(qdr_connection_t *conn)
{
    qdr_connection_work_list_t  work_list;
    qdr_link_ref_list_t         links_with_work[QDR_N_PRIORITIES];
    qdr_core_t                 *core = conn->core;

    qdr_link_ref_t *ref;
    qdr_link_t     *link;
    bool            detach_sent;

    int event_count = 0;

    if (conn->closed) {
        conn->protocol_adaptor->conn_close_handler(conn->protocol_adaptor->user_context, conn, conn->error);
        return 0;
    }

    sys_mutex_lock(conn->work_lock);
    DEQ_MOVE(conn->work_list, work_list);
    for (int priority = 0; priority <= QDR_MAX_PRIORITY; ++ priority) {
        DEQ_MOVE(conn->links_with_work[priority], links_with_work[priority]);

        //
        // Move the references from CLASS_WORK to CLASS_LOCAL so concurrent action in the core
        // thread doesn't assume these links are referenced from the connection's list.
        //
        ref = DEQ_HEAD(links_with_work[priority]);
        while (ref) {
            move_link_ref(ref->link, QDR_LINK_LIST_CLASS_WORK, QDR_LINK_LIST_CLASS_LOCAL);
            ref->link->processing = true;
            ref = DEQ_NEXT(ref);
        }
    }
    sys_mutex_unlock(conn->work_lock);

    event_count += DEQ_SIZE(work_list);
    qdr_connection_work_t *work = DEQ_HEAD(work_list);
    while (work) {
        DEQ_REMOVE_HEAD(work_list);

        switch (work->work_type) {
        case QDR_CONNECTION_WORK_FIRST_ATTACH :
            conn->protocol_adaptor->first_attach_handler(conn->protocol_adaptor->user_context, conn, work->link, work->source, work->target, work->ssn_class);
            break;

        case QDR_CONNECTION_WORK_SECOND_ATTACH :
            conn->protocol_adaptor->second_attach_handler(conn->protocol_adaptor->user_context, work->link, work->source, work->target);
            break;

        case QDR_CONNECTION_WORK_TRACING_ON :
            conn->protocol_adaptor->conn_trace_handler(conn->protocol_adaptor->user_context, conn, true);
            break;

        case QDR_CONNECTION_WORK_TRACING_OFF :
            conn->protocol_adaptor->conn_trace_handler(conn->protocol_adaptor->user_context, conn, false);
            break;

        }

        qdr_connection_work_free_CT(work);
        work = DEQ_HEAD(work_list);
    }

    // Process the links_with_work array from highest to lowest priority.
    for (int priority = QDR_MAX_PRIORITY; priority >= 0; -- priority) {
        ref = DEQ_HEAD(links_with_work[priority]);
        while (ref) {
            qdr_link_work_t *link_work;
            detach_sent = false;
            link = ref->link;

            //
            // The work lock must be used to protect accesses to the link's work_list and
            // link_work->processing.
            //
            sys_mutex_lock(conn->work_lock);
            link_work = DEQ_HEAD(link->work_list);
            if (link_work) {
                // link_work ref transfered to local link_work
                DEQ_REMOVE_HEAD(link->work_list);
                link_work->processing = true;
            }
            sys_mutex_unlock(conn->work_lock);

            //
            // Handle disposition/settlement updates
            //
            qdr_delivery_ref_list_t updated_deliveries;
            sys_mutex_lock(conn->work_lock);
            DEQ_MOVE(link->updated_deliveries, updated_deliveries);
            sys_mutex_unlock(conn->work_lock);

            qdr_delivery_ref_t *dref = DEQ_HEAD(updated_deliveries);
            while (dref) {
                conn->protocol_adaptor->delivery_update_handler(conn->protocol_adaptor->user_context, dref->dlv, dref->dlv->disposition, dref->dlv->settled);
                qdr_delivery_decref(core, dref->dlv, "qdr_connection_process - remove from updated list");
                qdr_del_delivery_ref(&updated_deliveries, dref);
                dref = DEQ_HEAD(updated_deliveries);
                event_count++;
            }

            while (link_work) {
                switch (link_work->work_type) {
                case QDR_LINK_WORK_DELIVERY :
                    {
                        int count = conn->protocol_adaptor->push_handler(conn->protocol_adaptor->user_context, link, link_work->value);
                        assert(count <= link_work->value);
                        link_work->value -= count;
                        break;
                    }

                case QDR_LINK_WORK_FLOW :
                    if (link_work->value > 0)
                        conn->protocol_adaptor->flow_handler(conn->protocol_adaptor->user_context, link, link_work->value);
                    if      (link_work->drain_action == QDR_LINK_WORK_DRAIN_ACTION_SET)
                        conn->protocol_adaptor->drain_handler(conn->protocol_adaptor->user_context, link, true);
                    else if (link_work->drain_action == QDR_LINK_WORK_DRAIN_ACTION_CLEAR)
                        conn->protocol_adaptor->drain_handler(conn->protocol_adaptor->user_context, link, false);
                    else if (link_work->drain_action == QDR_LINK_WORK_DRAIN_ACTION_DRAINED)
                        conn->protocol_adaptor->drained_handler(conn->protocol_adaptor->user_context, link);
                    break;

                case QDR_LINK_WORK_FIRST_DETACH :
                case QDR_LINK_WORK_SECOND_DETACH :
                    conn->protocol_adaptor->detach_handler(conn->protocol_adaptor->user_context, link, link_work->error,
                                                           link_work->work_type == QDR_LINK_WORK_FIRST_DETACH,
                                                           link_work->close_link);
                    detach_sent = true;
                    break;
                }

                sys_mutex_lock(conn->work_lock);
                if (link_work->work_type == QDR_LINK_WORK_DELIVERY && link_work->value > 0 && !link->detach_received) {
                    // link_work ref transfered from link_work to work_list
                    DEQ_INSERT_HEAD(link->work_list, link_work);
                    link_work->processing = false;
                    link_work = 0; // Halt work processing
                } else {
                    qdr_link_work_release(link_work);
                    link_work = DEQ_HEAD(link->work_list);
                    if (link_work) {
                        // link_work ref transfered to local link_work
                        DEQ_REMOVE_HEAD(link->work_list);
                        link_work->processing = true;
                    }
                }
                sys_mutex_unlock(conn->work_lock);
                event_count++;
            }

            if (detach_sent) {
                // let the core thread know so it can clean up
                qdr_link_detach_sent(link);
            } else
                qdr_record_link_credit(core, link);

            ref = DEQ_NEXT(ref);
        }
    }

    sys_mutex_lock(conn->work_lock);
    for (int priority = QDR_MAX_PRIORITY; priority >= 0; -- priority) {
        ref = DEQ_HEAD(links_with_work[priority]);
        while (ref) {
            qdr_link_t *link = ref->link;

            link->processing = false;
            if (link->ready_to_free)
                qdr_link_processing_complete(core, link);

            qdr_del_link_ref(links_with_work + priority, ref->link, QDR_LINK_LIST_CLASS_LOCAL);
            ref = DEQ_HEAD(links_with_work[priority]);
        }
    }
    sys_mutex_unlock(conn->work_lock);

    return event_count;
}


void qdr_link_set_context(qdr_link_t *link, void *context)
{
    if (link) {
        if (context == 0) {
            if (link->user_context) {
                qd_nullify_safe_ptr((qd_alloc_safe_ptr_t *)link->user_context);
                free(link->user_context);
                link->user_context = 0;
            }
        }
        else {
            if (link->user_context) {
                qd_nullify_safe_ptr((qd_alloc_safe_ptr_t *)link->user_context);
                free(link->user_context);
            }

            qd_link_t_sp *safe_ptr = NEW(qd_alloc_safe_ptr_t);
            set_safe_ptr_qd_link_t(context, safe_ptr);
            link->user_context = safe_ptr;
        }
    }
}


void *qdr_link_get_context(const qdr_link_t *link)
{
    if (link) {
        if (link->user_context) {
            qd_link_t_sp *safe_qdl = (qd_link_t_sp*) link->user_context;
            if (safe_qdl)
                return safe_deref_qd_link_t(*safe_qdl);
        }
    }

    return 0;
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
    return link ? link->phase : 0;
}


const char *qdr_link_internal_address(const qdr_link_t *link)
{
    return link && link->auto_link ? link->auto_link->internal_addr : 0;
}


bool qdr_link_is_anonymous(const qdr_link_t *link)
{
    return link->owning_addr == 0;
}


bool qdr_link_is_routed(const qdr_link_t *link)
{
    return link->connected_link != 0 || link->core_endpoint != 0;
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


static void qdr_link_setup_histogram(qdr_connection_t *conn, qd_direction_t dir, qdr_link_t *link)
{
    if (dir == QD_OUTGOING && conn->role != QDR_ROLE_INTER_ROUTER) {
        link->ingress_histogram = NEW_ARRAY(uint64_t, qd_bitmask_width());
        for (int i = 0; i < qd_bitmask_width(); i++)
            link->ingress_histogram[i] = 0;
    }
}


// used by the TSAN suppression file to mask the read/write races
// caused by modifying the deliveries' conn_id and link_id while in flight
void tsan_reset_delivery_ids(qdr_delivery_t *dlv, uint64_t conn_id, uint64_t link_id)
{
    dlv->conn_id = conn_id;
    dlv->link_id = link_id;
}


qdr_link_t *qdr_link_first_attach(qdr_connection_t *conn,
                                  qd_direction_t    dir,
                                  qdr_terminus_t   *source,
                                  qdr_terminus_t   *target,
                                  const char       *name,
                                  const char       *terminus_addr,
                                  bool              no_route,
                                  qdr_delivery_t   *initial_delivery,
                                  uint64_t         *link_id)
{
    qdr_action_t   *action         = qdr_action(qdr_link_inbound_first_attach_CT, "link_first_attach");
    qdr_link_t     *link           = new_qdr_link_t();
    qdr_terminus_t *local_terminus = dir == QD_OUTGOING ? source : target;

    ZERO(link);
    link->core = conn->core;
    link->identity = qdr_identifier(conn->core);
    *link_id = link->identity;
    link->conn = conn;
    link->conn_id = conn->identity;
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
    link->credit_pending = conn->link_capacity;
    link->admin_enabled  = true;
    link->oper_status    = QDR_LINK_OPER_DOWN;
    link->core_ticks     = qdr_core_uptime_ticks(conn->core);
    link->zero_credit_time = link->core_ticks;
    link->terminus_survives_disconnect = qdr_terminus_survives_disconnect(local_terminus);
    link->no_route = no_route;
    link->priority = QDR_DEFAULT_PRIORITY;

    link->strip_annotations_in  = conn->strip_annotations_in;
    link->strip_annotations_out = conn->strip_annotations_out;

    //
    // Adjust the delivery's identity
    //

    if (initial_delivery) {
        tsan_reset_delivery_ids(initial_delivery, link->conn->identity, link->identity);
    }

    if      (qdr_terminus_has_capability(local_terminus, QD_CAPABILITY_ROUTER_CONTROL)) {
        link->link_type = QD_LINK_CONTROL;
        link->priority = QDR_MAX_PRIORITY;
    } else if (qdr_terminus_has_capability(local_terminus, QD_CAPABILITY_ROUTER_DATA))
        link->link_type = QD_LINK_ROUTER;
    else if (qdr_terminus_has_capability(local_terminus, QD_CAPABILITY_EDGE_DOWNLINK)) {
        if (conn->core->router_mode == QD_ROUTER_MODE_INTERIOR &&
            conn->role == QDR_ROLE_EDGE_CONNECTION &&
            dir == QD_OUTGOING)
            link->link_type = QD_LINK_EDGE_DOWNLINK;
    }

    qdr_link_setup_histogram(conn, dir, link);

    set_safe_ptr_qdr_connection_t(conn, &action->args.connection.conn);
    set_safe_ptr_qdr_link_t(link, &action->args.connection.link);
    action->args.connection.dir    = dir;
    action->args.connection.source = source;
    action->args.connection.target = target;
    action->args.connection.initial_delivery = initial_delivery;
    if (!!initial_delivery)
        qdr_delivery_incref(initial_delivery, "qdr_link_first_attach - protect delivery in action list");
    qdr_action_enqueue(conn->core, action);

    return link;
}


void qdr_link_second_attach(qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target)
{
    qdr_action_t *action = qdr_action(qdr_link_inbound_second_attach_CT, "link_second_attach");

    set_safe_ptr_qdr_connection_t(link->conn, &action->args.connection.conn);
    set_safe_ptr_qdr_link_t(link, &action->args.connection.link);

    // ownership of source/target passed to core, core must free them when done
    action->args.connection.source = source;
    action->args.connection.target = target;
    qdr_action_enqueue(link->core, action);
}


void qdr_link_detach(qdr_link_t *link, qd_detach_type_t dt, qdr_error_t *error)
{
    qdr_action_t *action = qdr_action(qdr_link_inbound_detach_CT, "link_detach");

    set_safe_ptr_qdr_connection_t(link->conn, &action->args.connection.conn);
    set_safe_ptr_qdr_link_t(link, &action->args.connection.link);
    action->args.connection.error  = error;
    action->args.connection.dt     = dt;
    qdr_action_enqueue(link->core, action);
}


/* let the core thread know that a dispatch has been sent by the I/O thread
 */
static void qdr_link_detach_sent(qdr_link_t *link)
{
    qdr_action_t *action = qdr_action(qdr_link_detach_sent_CT, "link_detach_sent");

    set_safe_ptr_qdr_link_t(link, &action->args.connection.link);
    qdr_action_enqueue(link->core, action);
}


static void qdr_link_processing_complete(qdr_core_t *core, qdr_link_t *link)
{
    qdr_action_t *action = qdr_action(qdr_link_processing_complete_CT, "link_processing_complete");

    set_safe_ptr_qdr_link_t(link, &action->args.connection.link);
    qdr_action_enqueue(core, action);
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
    // expect: caller transfers refcount:
    assert(sys_atomic_get(&work->ref_count) > 0);
    DEQ_INSERT_TAIL(link->work_list, work);
    qdr_add_link_ref(&conn->links_with_work[link->priority], link, QDR_LINK_LIST_CLASS_WORK);
    sys_mutex_unlock(conn->work_lock);

    qdr_connection_activate_CT(core, conn);
}


/**
 * Generate a link name
 */
static void qdr_generate_link_name(const char *label, char *buffer, size_t length)
{
    char discriminator[QD_DISCRIMINATOR_SIZE];
    qd_generate_discriminator(discriminator);
    snprintf(buffer, length, "%s.%s", label, discriminator);
}


void qdr_link_cleanup_deliveries_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link, bool on_shutdown)
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
        if (d->presettled)
            core->dropped_presettled_deliveries++;
        d->where = QDR_DELIVERY_NOWHERE;
        if (on_shutdown)
            d->tracking_addr = 0;
        qdr_link_work_release(d->link_work);
        d->link_work = 0;
        d = DEQ_NEXT(d);
    }

    DEQ_MOVE(link->unsettled, unsettled);
    d = DEQ_HEAD(unsettled);
    while (d) {
        assert(d->where == QDR_DELIVERY_IN_UNSETTLED);
        d->where = QDR_DELIVERY_NOWHERE;
        qdr_link_work_release(d->link_work);
        d->link_work = 0;
        if (on_shutdown)
            d->tracking_addr = 0;
        d = DEQ_NEXT(d);
    }

    DEQ_MOVE(link->settled, settled);
    d = DEQ_HEAD(settled);
    while (d) {
        assert(d->where == QDR_DELIVERY_IN_SETTLED);
        d->where = QDR_DELIVERY_NOWHERE;
        qdr_link_work_release(d->link_work);
        d->link_work = 0;
        if (on_shutdown)
            d->tracking_addr = 0;
        d = DEQ_NEXT(d);
    }
    sys_mutex_unlock(conn->work_lock);

    //
    // Free all the 'updated' references
    //
    qdr_delivery_ref_t *ref = DEQ_HEAD(updated_deliveries);
    while (ref) {
        //
        // Updates global and link level delivery counters like presettled_deliveries, accepted_deliveries, released_deliveries etc
        //
        qdr_delivery_increment_counters_CT(core, ref->dlv);
        qd_nullify_safe_ptr(&ref->dlv->link_sp);
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

        // expect: an inbound undelivered multicast should
        // have no peers (has not been forwarded yet)
        assert(dlv->multicast
               ? qdr_delivery_peer_count_CT(dlv) == 0
               : true);

        peer = qdr_delivery_first_peer_CT(dlv);
        while (peer) {
            if (peer->multicast) {
                //
                // dlv is outgoing mcast - tell its incoming peer that it has
                // been released and settled.  This will unlink these peers.
                //
                qdr_delivery_mcast_outbound_update_CT(core, peer, dlv, PN_RELEASED, true);
            }
            else {
                qdr_delivery_release_CT(core, peer);
                qdr_delivery_unlink_peers_CT(core, dlv, peer);
            }
            peer = qdr_delivery_next_peer_CT(dlv);
        }

        //
        // Updates global and link level delivery counters like presettled_deliveries, accepted_deliveries, released_deliveries etc
        //
        qdr_delivery_increment_counters_CT(core, dlv);
        qd_nullify_safe_ptr(&dlv->link_sp);

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
                qdr_check_addr_CT(core, dlv->tracking_addr);

            dlv->tracking_addr = 0;
        }

        if (!qdr_delivery_receive_complete(dlv)) {
            qdr_delivery_set_aborted(dlv);
            qdr_delivery_continue_peers_CT(core, dlv, false);
        }

        if (dlv->multicast) {
            //
            // forward settlement
            //
            qdr_delivery_mcast_inbound_update_CT(core, dlv,
                                                 PN_MODIFIED,
                                                 true);  // true == settled
        } else {
            peer = qdr_delivery_first_peer_CT(dlv);
            while (peer) {
                if (peer->multicast) {
                    //
                    // peer is incoming multicast and dlv is one of its corresponding
                    // outgoing deliveries.  This will unlink these peers.
                    //
                    qdr_delivery_mcast_outbound_update_CT(core, peer, dlv, PN_MODIFIED, true);
                } else {
                    if (link->link_direction == QD_OUTGOING)
                        qdr_delivery_failed_CT(core, peer);
                    qdr_delivery_unlink_peers_CT(core, dlv, peer);
                }
                peer = qdr_delivery_next_peer_CT(dlv);
            }
        }

        //
        // Updates global and link level delivery counters like presettled_deliveries, accepted_deliveries, released_deliveries etc
        //
        qdr_delivery_increment_counters_CT(core, dlv);
        qd_nullify_safe_ptr(&dlv->link_sp);

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
            qdr_delivery_set_aborted(dlv);
            qdr_delivery_continue_peers_CT(core, dlv, false);
        }

        peer = qdr_delivery_first_peer_CT(dlv);
        qdr_delivery_t *next_peer = 0;
        while (peer) {
            next_peer = qdr_delivery_next_peer_CT(dlv);
            qdr_delivery_unlink_peers_CT(core, dlv, peer);
            peer = next_peer;
        }

        //
        // Updates global and link level delivery counters like presettled_deliveries, accepted_deliveries, released_deliveries etc
        //
        qdr_delivery_increment_counters_CT(core, dlv);
        qd_nullify_safe_ptr(&dlv->link_sp);

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
            qdr_delivery_set_aborted(dlv);
        dlv = DEQ_NEXT(dlv);
    }
    sys_mutex_unlock(conn->work_lock);
}


static void qdr_link_cleanup_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link, const char *log_text)
{
    //
    // Remove the link from the overall list of links and possibly the streaming
    // link pool
    //
    DEQ_REMOVE(core->open_links, link);

    //
    // If the link has a core_endpoint, allow the core_endpoint module to
    // clean up its state
    //
    if (link->core_endpoint)
        qdrc_endpoint_do_cleanup_CT(core, link->core_endpoint);

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
        if (link->link_type == QD_LINK_ROUTER) {
            if (link == core->data_links_by_mask_bit[conn->mask_bit].links[link->priority])
                core->data_links_by_mask_bit[conn->mask_bit].links[link->priority] = 0;
        }
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
        qdr_link_work_release(link_work);
        link_work = DEQ_HEAD(work_list);
    }

    //
    // Clean up any remaining deliveries
    //
    qdr_link_cleanup_deliveries_CT(core, conn, link, false);

    //
    // Remove all references to this link in the connection's and owning
    // address reference lists
    //
    sys_mutex_lock(conn->work_lock);
    qdr_del_link_ref(&conn->links, link, QDR_LINK_LIST_CLASS_CONNECTION);
    qdr_del_link_ref(&conn->links_with_work[link->priority], link, QDR_LINK_LIST_CLASS_WORK);
    sys_mutex_unlock(conn->work_lock);

    if (link->ref[QDR_LINK_LIST_CLASS_ADDRESS]) {
        assert(link->owning_addr);
        qdr_del_link_ref((link->link_direction == QD_OUTGOING)
                         ? &link->owning_addr->rlinks
                         : &link->owning_addr->inlinks,
                         link,  QDR_LINK_LIST_CLASS_ADDRESS);
    }

    if (link->in_streaming_pool) {
        DEQ_REMOVE_N(STREAMING_POOL, conn->streaming_link_pool, link);
        link->in_streaming_pool = false;
    }

    //
    // Free the link's name and terminus_addr
    //
    free(link->name);
    free(link->disambiguated_name);
    free(link->terminus_addr);
    free(link->ingress_histogram);
    free(link->insert_prefix);
    free(link->strip_prefix);

    //
    // Log the link closure
    //
    qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"][L%"PRIu64"] %s: del=%"PRIu64" presett=%"PRIu64" psdrop=%"PRIu64
           " acc=%"PRIu64" rej=%"PRIu64" rel=%"PRIu64" mod=%"PRIu64" delay1=%"PRIu64" delay10=%"PRIu64" blocked=%s",
           conn->identity, link->identity, log_text, link->total_deliveries, link->presettled_deliveries,
           link->dropped_presettled_deliveries, link->accepted_deliveries, link->rejected_deliveries,
           link->released_deliveries, link->modified_deliveries, link->deliveries_delayed_1sec,
           link->deliveries_delayed_10sec, link->reported_as_blocked ? "yes" : "no");

    if (link->reported_as_blocked)
        core->links_blocked--;
    if (link->user_context) {
        qdr_link_set_context(link, 0);
    }
    free_qdr_link_t(link);
}


static void qdr_link_cleanup_protected_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link, const char *label)
{
    bool do_cleanup = false;

    sys_mutex_lock(conn->work_lock);
    // prevent an I/O thread from processing this link (DISPATCH-1475)
    qdr_del_link_ref(&conn->links_with_work[link->priority], link, QDR_LINK_LIST_CLASS_WORK);
    if (link->processing) {
        // Cannot cleanup link because I/O thread is currently processing it
        // Mark it so the I/O thread will notify the core when processing is complete
        link->ready_to_free = true;
    }
    else
        do_cleanup = true;
    sys_mutex_unlock(conn->work_lock);

    if (do_cleanup)
        qdr_link_cleanup_CT(core, conn, link, label);
}


qdr_link_t *qdr_create_link_CT(qdr_core_t        *core,
                               qdr_connection_t  *conn,
                               qd_link_type_t     link_type,
                               qd_direction_t     dir,
                               qdr_terminus_t    *source,
                               qdr_terminus_t    *target,
                               qd_session_class_t ssn_class,
                               uint8_t            priority)
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
    link->conn_id        = conn->identity;
    link->link_type      = link_type;
    link->link_direction = dir;
    link->capacity       = conn->link_capacity;
    link->credit_pending = conn->link_capacity;
    link->name           = (char*) malloc(QD_DISCRIMINATOR_SIZE + 8);
    link->disambiguated_name = 0;
    link->terminus_addr  = 0;
    qdr_generate_link_name("qdlink", link->name, QD_DISCRIMINATOR_SIZE + 8);
    link->admin_enabled  = true;
    link->oper_status    = QDR_LINK_OPER_DOWN;
    link->insert_prefix  = 0;
    link->strip_prefix   = 0;
    link->attach_count   = 1;
    link->core_ticks     = qdr_core_uptime_ticks(core);
    link->zero_credit_time = link->core_ticks;
    link->priority       = priority;

    link->strip_annotations_in  = conn->strip_annotations_in;
    link->strip_annotations_out = conn->strip_annotations_out;

    qdr_link_setup_histogram(conn, dir, link);

    DEQ_INSERT_TAIL(core->open_links, link);
    qdr_add_link_ref(&conn->links, link, QDR_LINK_LIST_CLASS_CONNECTION);

    qdr_connection_work_t *work = new_qdr_connection_work_t();
    ZERO(work);
    work->work_type = QDR_CONNECTION_WORK_FIRST_ATTACH;
    work->link      = link;
    work->source    = source;
    work->target    = target;
    work->ssn_class = ssn_class;

    char   source_str[1000];
    char   target_str[1000];
    size_t source_len = 1000;
    size_t target_len = 1000;

    source_str[0] = '\0';
    target_str[0] = '\0';

    if (qd_log_enabled(core->log, QD_LOG_INFO)) {
        qdr_terminus_format(source, source_str, &source_len);
        qdr_terminus_format(target, target_str, &target_len);
    }

    qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"][L%"PRIu64"] Link attached: dir=%s source=%s target=%s",
               conn->identity, link->identity, dir == QD_INCOMING ? "in" : "out", source_str, target_str);

    qdr_connection_enqueue_work_CT(core, conn, work);
    return link;
}


void qdr_link_outbound_detach_CT(qdr_core_t *core, qdr_link_t *link, qdr_error_t *error, qdr_condition_t condition, bool close)
{
    //
    // Ensure a pooled link is no longer available for streaming messages
    //
    if (link->streaming) {
        if (link->in_streaming_pool) {
            DEQ_REMOVE_N(STREAMING_POOL, link->conn->streaming_link_pool, link);
            link->in_streaming_pool = false;
        }
    }

    //
    // tell the I/O thread to do the detach
    //

    link->detach_count += 1;
    qdr_link_work_t *work = qdr_link_work(link->detach_count == 1 ? QDR_LINK_WORK_FIRST_DETACH : QDR_LINK_WORK_SECOND_DETACH);
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

        case QDR_CONDITION_INVALID_LINK_EXPIRATION:
            work->error = qdr_error("qd:link-expiration", "Requested link expiration not allowed");
            break;

        case QDR_CONDITION_NONE:
            work->error = 0;
            break;
        }
    }

    qdr_link_enqueue_work_CT(core, link, work);
}


void qdr_link_outbound_second_attach_CT(qdr_core_t *core, qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target)
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


qdr_address_config_t *qdr_config_for_address_CT(qdr_core_t *core, qdr_connection_t *conn, qd_iterator_t *iter)
{
    qdr_address_config_t *addr = 0;
    qd_iterator_view_t old_view = qd_iterator_get_view(iter);

    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_WITH_SPACE);
    if (conn && conn->tenant_space)
        qd_iterator_annotate_space(iter, conn->tenant_space, conn->tenant_space_len);
    qd_parse_tree_retrieve_match(core->addr_parse_tree, iter, (void **) &addr);
    qd_iterator_annotate_prefix(iter, '\0');
    qd_iterator_reset_view(iter, old_view);

    return addr;
}


qd_address_treatment_t qdr_treatment_for_address_hash_CT(qdr_core_t *core, qd_iterator_t *iter, qdr_address_config_t **addr_config)
{
    return qdr_treatment_for_address_hash_with_default_CT(core, iter, core->qd->default_treatment, addr_config);
}

qd_address_treatment_t qdr_treatment_for_address_hash_with_default_CT(qdr_core_t              *core,
                                                                      qd_iterator_t           *iter,
                                                                      qd_address_treatment_t   default_treatment,
                                                                      qdr_address_config_t   **addr_config)
{
#define HASH_STORAGE_SIZE 1000
    char  storage[HASH_STORAGE_SIZE + 1];
    char *copy    = storage;
    bool  on_heap = false;
    int   length  = qd_iterator_length(iter);
    qd_address_treatment_t trt = default_treatment;
    qdr_address_config_t *addr = 0;

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

    else if (copy[0] == QD_ITER_HASH_PREFIX_MOBILE) {
        //
        // Handle the mobile address case
        //
        qd_iterator_t *config_iter = qd_iterator_string(&copy[2], ITER_VIEW_ADDRESS_WITH_SPACE);
        qd_parse_tree_retrieve_match(core->addr_parse_tree, config_iter, (void **) &addr);
        if (addr)
            trt = addr->treatment;
        qd_iterator_free(config_iter);
    }

    if (on_heap)
        free(copy);

    *addr_config = addr;
    return trt;
}


/**
 * Check an address to see if it no longer has any associated destinations.
 * Depending on its policy, the address may be eligible for being closed out
 * (i.e. Logging its terminal statistics and freeing its resources).
 */
void qdr_check_addr_CT(qdr_core_t *core, qdr_address_t *addr)
{
    if (addr == 0)
        return;

    //
    // If the address has no in-process consumer or destinations, it should be
    // deleted.
    //
    if (DEQ_SIZE(addr->subscriptions) == 0
        && DEQ_SIZE(addr->rlinks) == 0
        && DEQ_SIZE(addr->inlinks) == 0
        && qd_bitmask_cardinality(addr->rnodes) == 0
        && addr->ref_count == 0
        && addr->tracked_deliveries == 0
        && addr->core_endpoint == 0
        && addr->fallback_for == 0) {
        qdr_address_t *fallback = addr->fallback;
        qdr_core_remove_address(core, addr);

        //
        // If the address being removed had a fallback address, check to see if that
        // address should now also be removed.
        //
        if (!!fallback)
            qdr_check_addr_CT(core, fallback);
    }
}


static void qdr_connection_opened_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_connection_t *conn = safe_deref_qdr_connection_t(action->args.connection.conn);

    if (!conn || discard) {
        qdr_field_free(action->args.connection.connection_label);
        qdr_field_free(action->args.connection.container_id);

        if (conn)
            qdr_connection_free(conn);
        return;
    }

    do {
        DEQ_ITEM_INIT(conn);
        conn->enable_protocol_trace = action->args.connection.enable_protocol_trace;
        DEQ_INSERT_TAIL(core->open_connections, conn);

        if (conn->role == QDR_ROLE_NORMAL) {
            //
            // No action needed for NORMAL connections
            //
            break;
        }

        if (conn->role == QDR_ROLE_INTER_ROUTER) {
            //
            // Assign a unique mask-bit to this connection as a reference to be used by
            // the router module
            //
            if (qd_bitmask_first_set(core->neighbor_free_mask, &conn->mask_bit)) {
                qd_bitmask_clear_bit(core->neighbor_free_mask, conn->mask_bit);
                assert(core->rnode_conns_by_mask_bit[conn->mask_bit] == 0);
                core->rnode_conns_by_mask_bit[conn->mask_bit] = conn;
            } else {
                qd_log(core->log, QD_LOG_CRITICAL, "Exceeded maximum inter-router connection count");
                conn->role = QDR_ROLE_NORMAL;
                break;
            }

            if (!conn->incoming) {
                //
                // The connector-side of inter-router connections is responsible for setting up the
                // inter-router links:  Two (in and out) for control, 2 * QDR_N_PRIORITIES for
                // routed-message transfer.
                //
                (void) qdr_create_link_CT(core, conn, QD_LINK_CONTROL, QD_INCOMING,
                                          qdr_terminus_router_control(), qdr_terminus_router_control(),
                                          QD_SSN_ROUTER_CONTROL, QDR_MAX_PRIORITY);
                (void) qdr_create_link_CT(core, conn, QD_LINK_CONTROL, QD_OUTGOING,
                                          qdr_terminus_router_control(), qdr_terminus_router_control(),
                                          QD_SSN_ROUTER_CONTROL, QDR_MAX_PRIORITY);
                STATIC_ASSERT((QD_SSN_ROUTER_DATA_PRI_9 - QD_SSN_ROUTER_DATA_PRI_0 + 1) == QDR_N_PRIORITIES, PRIORITY_SESSION_NOT_SAME);

                for (int priority = 0; priority < QDR_N_PRIORITIES; ++ priority) {
                    // a session is reserved for each priority link
                    qd_session_class_t sc = (qd_session_class_t)(QD_SSN_ROUTER_DATA_PRI_0 + priority);
                    (void) qdr_create_link_CT(core, conn, QD_LINK_ROUTER, QD_INCOMING,
                                              qdr_terminus_router_data(), qdr_terminus_router_data(),
                                              sc, priority);
                    (void) qdr_create_link_CT(core, conn, QD_LINK_ROUTER,  QD_OUTGOING,
                                              qdr_terminus_router_data(), qdr_terminus_router_data(),
                                              sc, priority);
                }
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
    } while (false);

    qdrc_event_conn_raise(core, QDRC_EVENT_CONN_OPENED, conn);

    qdr_field_free(action->args.connection.connection_label);
    qdr_field_free(action->args.connection.container_id);
}

void qdr_connection_free(qdr_connection_t *conn)
{
    sys_mutex_free(conn->work_lock);
    free(conn->tenant_space);
    qdr_error_free(conn->error);
    qdr_connection_info_free(conn->connection_info);
    free_qdr_connection_t(conn);
}


// create a new outoing link for streaming messages
qdr_link_t *qdr_connection_new_streaming_link_CT(qdr_core_t *core, qdr_connection_t *conn)
{
    qdr_link_t *out_link = 0;

    switch (conn->role) {
    case QDR_ROLE_INTER_ROUTER:
        out_link = qdr_create_link_CT(core, conn, QD_LINK_ROUTER, QD_OUTGOING,
                                      qdr_terminus_router_data(), qdr_terminus_router_data(),
                                      QD_SSN_LINK_STREAMING, QDR_DEFAULT_PRIORITY);
        break;
    case QDR_ROLE_EDGE_CONNECTION:
        out_link = qdr_create_link_CT(core, conn, QD_LINK_ENDPOINT, QD_OUTGOING,
                                      qdr_terminus(0), qdr_terminus(0),
                                      QD_SSN_LINK_STREAMING, QDR_DEFAULT_PRIORITY);
        break;
    default:
        assert(false);
        break;
    }

    if (out_link) {
        out_link->streaming = true;
        out_link->priority = 4;
        if (!conn->has_streaming_links) {
            qdr_add_connection_ref(&core->streaming_connections, conn);
            conn->has_streaming_links = true;
        }
    }
    return out_link;
}


static void qdr_connection_closed_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_connection_t *conn = safe_deref_qdr_connection_t(action->args.connection.conn);
    if (discard || !conn)
        return;

    //
    // Deactivate routes associated with this connection
    //
    qdr_route_connection_closed_CT(core, conn);

    //
    // Give back the router mask-bit.
    //
    if (conn->role == QDR_ROLE_INTER_ROUTER) {
        assert(qd_bitmask_valid_bit_value(conn->mask_bit));
        qdr_reset_sheaf(core, conn->mask_bit);
        qd_bitmask_set_bit(core->neighbor_free_mask, conn->mask_bit);
        core->rnode_conns_by_mask_bit[conn->mask_bit] = 0;
    }

    //
    // Remove the references in the links_with_work list
    //
    qdr_link_ref_t *link_ref;
    for (int priority = 0; priority < QDR_N_PRIORITIES; ++ priority) {
        link_ref = DEQ_HEAD(conn->links_with_work[priority]);
        while (link_ref) {
            qdr_del_link_ref(&conn->links_with_work[priority], link_ref->link, QDR_LINK_LIST_CLASS_WORK);
            link_ref = DEQ_HEAD(conn->links_with_work[priority]);
        }
    }

    //
    // TODO - Clean up links associated with this connection
    //        This involves the links and the dispositions of deliveries stored
    //        with the links.
    //
    link_ref = DEQ_HEAD(conn->links);
    while (link_ref) {
        qdr_link_t *link = link_ref->link;

        qdr_route_auto_link_closed_CT(core, link);

        //
        // Clean up the link and all its associated state.
        //
        qdr_link_cleanup_CT(core, conn, link, "Link closed due to connection loss"); // link_cleanup disconnects and frees the ref.
        link_ref = DEQ_HEAD(conn->links);
    }

    if (conn->has_streaming_links) {
        assert(DEQ_IS_EMPTY(conn->streaming_link_pool));  // all links have been released
        qdr_del_connection_ref(&core->streaming_connections, conn);
    }

    //
    // Discard items on the work list
    //
    qdr_connection_work_t *work = DEQ_HEAD(conn->work_list);
    while (work) {
        DEQ_REMOVE_HEAD(conn->work_list);
        qdr_connection_work_free_CT(work);
        work = DEQ_HEAD(conn->work_list);
    }

    //
    // If this connection is on the activation list, remove it from the list
    //
    if (conn->in_activate_list) {
        conn->in_activate_list = false;
        DEQ_REMOVE_N(ACTIVATE, core->connections_to_activate, conn);
    }

    qdrc_event_conn_raise(core, QDRC_EVENT_CONN_CLOSED, conn);

    qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"] Connection Closed", conn->identity);

    DEQ_REMOVE(core->open_connections, conn);
    qdr_connection_free(conn);
}


//
// Handle the attachment and detachment of an inter-router control link
//
static void qdr_attach_link_control_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link)
{
    if (conn->role == QDR_ROLE_INTER_ROUTER) {
        link->owning_addr = core->hello_addr;
        qdr_add_link_ref(&core->hello_addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        core->control_links_by_mask_bit[conn->mask_bit] = link;
    }
}


static void qdr_detach_link_control_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link)
{
    if (conn->role == QDR_ROLE_INTER_ROUTER) {
        qdr_del_link_ref(&core->hello_addr->rlinks, link, QDR_LINK_LIST_CLASS_ADDRESS);
        link->owning_addr = 0;
        core->control_links_by_mask_bit[conn->mask_bit] = 0;
        qdr_post_link_lost_CT(core, conn->mask_bit);
    }
}


//
// Handle the attachment and detachment of an inter-router data link
//
static void qdr_attach_link_data_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link)
{
    assert(link->link_type == QD_LINK_ROUTER);
    // The first 2 x QDR_N_PRIORITIES (10) QDR_LINK_ROUTER links to attach over
    // the inter-router connection are the shared priority links.  These links
    // are attached in priority order starting at zero.
    if (link->link_direction == QD_OUTGOING) {
        int next_pri = core->data_links_by_mask_bit[conn->mask_bit].count;
        if (next_pri < QDR_N_PRIORITIES) {
            link->priority = next_pri;
            core->data_links_by_mask_bit[conn->mask_bit].links[next_pri] = link;
            core->data_links_by_mask_bit[conn->mask_bit].count += 1;
        }
    } else {
        if (conn->next_pri < QDR_N_PRIORITIES) {
            link->priority = conn->next_pri++;
        }
    }
}


static void qdr_detach_link_data_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link)
{
    // if this link is in the priority sheaf it needs to be removed
    if (conn->role == QDR_ROLE_INTER_ROUTER)
        if (link == core->data_links_by_mask_bit[conn->mask_bit].links[link->priority])
            core->data_links_by_mask_bit[conn->mask_bit].links[link->priority] = 0;
}


static void qdr_attach_link_downlink_CT(qdr_core_t *core, qdr_connection_t *conn, qdr_link_t *link, qdr_terminus_t *source)
{
    qdr_address_t *addr;
    qd_iterator_t *iter = qd_iterator_dup(qdr_terminus_get_address(source));
    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, QD_ITER_HASH_PREFIX_EDGE_SUMMARY);

    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    if (!addr) {
       addr = qdr_address_CT(core, QD_TREATMENT_ANYCAST_BALANCED, 0);
        qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_INSERT_TAIL(core->addrs, addr);
    }

    qdr_core_bind_address_link_CT(core, addr, link);

    qd_iterator_free(iter);
}


// move dlv to new link.
static void qdr_link_process_initial_delivery_CT(qdr_core_t *core, qdr_link_t *link, qdr_delivery_t *dlv)
{
    //
    // Remove the delivery from its current link if needed
    //
    qdr_link_t *old_link  = safe_deref_qdr_link_t(dlv->link_sp);
    if (!!old_link) {
        sys_mutex_lock(old_link->conn->work_lock);
        switch (dlv->where) {
        case QDR_DELIVERY_NOWHERE:
            break;

        case QDR_DELIVERY_IN_UNDELIVERED:
            DEQ_REMOVE(old_link->undelivered, dlv);
            dlv->where = QDR_DELIVERY_NOWHERE;
            qdr_link_work_release(dlv->link_work);
            dlv->link_work = 0;
            // expect: caller holds reference to dlv (in action)
            assert(sys_atomic_get(&dlv->ref_count) > 1);
            qdr_delivery_decref_CT(core, dlv, "qdr_link_process_initial_delivery_CT - remove from undelivered list");
            break;

        case QDR_DELIVERY_IN_UNSETTLED:
            DEQ_REMOVE(old_link->unsettled, dlv);
            dlv->where = QDR_DELIVERY_NOWHERE;
            assert(sys_atomic_get(&dlv->ref_count) > 1);
            qdr_delivery_decref_CT(core, dlv, "qdr_link_process_initial_delivery_CT - remove from unsettled list");
            break;

        case QDR_DELIVERY_IN_SETTLED:
            DEQ_REMOVE(old_link->settled, dlv);
            dlv->where = QDR_DELIVERY_NOWHERE;
            assert(sys_atomic_get(&dlv->ref_count) > 1);
            qdr_delivery_decref_CT(core, dlv, "qdr_link_process_initial_delivery_CT - remove from settled list");
            break;
        }
        sys_mutex_unlock(old_link->conn->work_lock);
    }

    //
    // Enqueue the delivery onto the new link's undelivered list
    //
    set_safe_ptr_qdr_link_t(link, &dlv->link_sp);
    qdr_forward_deliver_CT(core, link, dlv);
}


static void qdr_link_inbound_first_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_connection_t      *conn = safe_deref_qdr_connection_t(action->args.connection.conn);
    qdr_link_t            *link = safe_deref_qdr_link_t(action->args.connection.link);
    qdr_delivery_t *initial_dlv = action->args.connection.initial_delivery;
    if (discard || !conn || !link) {
        if (initial_dlv)
            qdr_delivery_decref(core, initial_dlv,
                                "qdr_link_inbound_first_attach_CT - discarding action");
        return;
    }

    qd_direction_t  dir         = action->args.connection.dir;
    qdr_terminus_t *source      = action->args.connection.source;
    qdr_terminus_t *target      = action->args.connection.target;

    //
    // Start the attach count.
    //
    link->attach_count = 1;

    //
    // Put the link into the proper lists for tracking.
    //
    DEQ_INSERT_TAIL(core->open_links, link);
    qdr_add_link_ref(&conn->links, link, QDR_LINK_LIST_CLASS_CONNECTION);

    //
    // Mark the link as an edge link if it's inside an edge connection.
    //
    link->edge = (conn->role == QDR_ROLE_EDGE_CONNECTION);

    //
    // Reject any attaches of inter-router links that arrive on connections that are not inter-router.
    //
    if (((link->link_type == QD_LINK_CONTROL || link->link_type == QD_LINK_ROUTER) &&
         conn->role != QDR_ROLE_INTER_ROUTER)) {
        link->link_type = QD_LINK_ENDPOINT; // Demote the link type to endpoint if this is not an inter-router connection
        qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_FORBIDDEN, true);
        qdr_terminus_free(source);
        qdr_terminus_free(target);
        qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"] Router attach forbidden on non-inter-router connection", conn->identity);
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
        qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"] Endpoint attach forbidden on inter-router connection", conn->identity);
        return;
    }

    char   source_str[1000];
    char   target_str[1000];
    size_t source_len = 1000;
    size_t target_len = 1000;

    source_str[0] = '\0';
    target_str[0] = '\0';

    //
    // Grab the formatted terminus strings before we schedule any IO-thread processing that
    // might get ahead of us and free the terminus objects before we issue the log.
    //
    if (qd_log_enabled(core->log, QD_LOG_INFO)) {
        qdr_terminus_format(source, source_str, &source_len);
        qdr_terminus_format(target, target_str, &target_len);
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
                if (core->addr_lookup_handler)
                    core->addr_lookup_handler(core->addr_lookup_context, conn, link, dir, source, target);
                else {
                    qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
                    qdr_terminus_free(source);
                    qdr_terminus_free(target);
                    qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"] Endpoint attach failed - no address lookup handler", conn->identity);
                    return;
                }
            }
            break;
        }

        case QD_LINK_ROUTER:
            qdr_attach_link_data_CT(core, conn, link);
            // fall-through:
        case QD_LINK_CONTROL:
            qdr_link_outbound_second_attach_CT(core, link, source, target);
            qdr_link_issue_credit_CT(core, link, link->capacity, false);
            break;

        case QD_LINK_EDGE_DOWNLINK:
            break;
        }
    } else {
        //
        // Handle outgoing link cases
        //
        if (initial_dlv) {
            qdr_link_process_initial_delivery_CT(core, link, initial_dlv);
            qdr_delivery_decref(core, initial_dlv,
                                "qdr_link_inbound_first_attach_CT - dropping action reference");
            initial_dlv = 0;
        }

        switch (link->link_type) {
        case QD_LINK_ENDPOINT: {
            if (core->addr_lookup_handler)
                core->addr_lookup_handler(core->addr_lookup_context, conn, link, dir, source, target);
            else {
                qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NO_ROUTE_TO_DESTINATION, true);
                qdr_terminus_free(source);
                qdr_terminus_free(target);
                    qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"] Endpoint attach failed - no address lookup handler", conn->identity);
                return;
            }
            break;
        }

        case QD_LINK_CONTROL:
            qdr_attach_link_control_CT(core, conn, link);
            qdr_link_outbound_second_attach_CT(core, link, source, target);
            break;

        case QD_LINK_ROUTER:
            qdr_attach_link_data_CT(core, conn, link);
            qdr_link_outbound_second_attach_CT(core, link, source, target);
            break;

        case QD_LINK_EDGE_DOWNLINK:
            qdr_attach_link_downlink_CT(core, conn, link, source);
            qdr_link_outbound_second_attach_CT(core, link, source, target);
            break;
        }
    }

    qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"][L%"PRIu64"] Link attached: dir=%s source=%s target=%s",
           conn->identity, link->identity, dir == QD_INCOMING ? "in" : "out", source_str, target_str);
}


static void qdr_link_inbound_second_attach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_link_t       *link = safe_deref_qdr_link_t(action->args.connection.link);
    qdr_connection_t *conn = safe_deref_qdr_connection_t(action->args.connection.conn);
    qdr_terminus_t   *source = action->args.connection.source;
    qdr_terminus_t   *target = action->args.connection.target;

    if (discard || !link || !conn) {
        qdr_terminus_free(source);
        qdr_terminus_free(target);
        return;
    }

    link->oper_status = QDR_LINK_OPER_UP;
    link->attach_count++;

    //
    // Mark the link as an edge link if it's inside an edge connection.
    //
    link->edge = (conn->role == QDR_ROLE_EDGE_CONNECTION);

    if (link->core_endpoint) {
        qdrc_endpoint_do_second_attach_CT(core, link->core_endpoint, source, target);
        return;
    }

    //
    // Handle attach-routed links
    //
    if (link->connected_link) {
        qdr_terminus_t *remote_terminus = link->link_direction == QD_OUTGOING ? target : source;
        if (link->strip_prefix) {
            qdr_terminus_strip_address_prefix(remote_terminus, link->strip_prefix);
        }
        if (link->insert_prefix) {
            qdr_terminus_insert_address_prefix(remote_terminus, link->insert_prefix);
        }

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
                    qdr_core_bind_address_link_CT(core, link->auto_link->addr, link);
                }
            }

            //
            // Issue credit if this is an anonymous link or if its address has at least one reachable destination.
            //
            qdr_address_t *addr = link->owning_addr;
            if (!addr || (DEQ_SIZE(addr->subscriptions) || DEQ_SIZE(addr->rlinks) || qd_bitmask_cardinality(addr->rnodes)
                          || (!!addr->fallback && (DEQ_SIZE(addr->fallback->subscriptions)
                                                    || DEQ_SIZE(addr->fallback->rlinks)
                                                    || qd_bitmask_cardinality(addr->fallback->rnodes)))))
                qdr_link_issue_credit_CT(core, link, link->capacity, false);
            break;

        case QD_LINK_ROUTER:
            qdr_attach_link_data_CT(core, conn, link);
            // fall-through
        case QD_LINK_CONTROL:
            qdr_link_issue_credit_CT(core, link, link->capacity, false);
            break;

        case QD_LINK_EDGE_DOWNLINK:
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
                    qdr_core_bind_address_link_CT(core, link->auto_link->addr, link);
                }
            }
            break;

        case QD_LINK_CONTROL:
            qdr_attach_link_control_CT(core, conn, link);
            break;

        case QD_LINK_ROUTER:
            qdr_attach_link_data_CT(core, conn, link);
            break;

        case QD_LINK_EDGE_DOWNLINK:
            break;
        }
    }

    qdr_terminus_free(source);
    qdr_terminus_free(target);
}


static void qdr_link_inbound_detach_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_connection_t *conn  = safe_deref_qdr_connection_t(action->args.connection.conn);
    qdr_link_t       *link  = safe_deref_qdr_link_t(action->args.connection.link);
    qdr_error_t      *error = action->args.connection.error;
    qd_detach_type_t  dt    = action->args.connection.dt;

    if (discard || !conn || !link) {
        qdr_error_free(error);
        return;
    }

    if (link->detach_received)
        return;

    link->detach_received = true;
    ++link->detach_count;

    if (link->core_endpoint) {
        qdrc_endpoint_do_detach_CT(core, link->core_endpoint, error, dt);
        return;
    }

    //
    // ensure a pooled link is no longer available for use
    //
    if (link->streaming) {
        if (link->in_streaming_pool) {
            DEQ_REMOVE_N(STREAMING_POOL, conn->streaming_link_pool, link);
            link->in_streaming_pool = false;
        }
    }

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
            qdr_link_outbound_detach_CT(core, link->connected_link, 0, QDR_CONDITION_ROUTED_LINK_LOST, !link->terminus_survives_disconnect);
            qdr_error_free(error);
        }

        //
        // If the link is completely detached, release its resources
        //
        if (link->detach_send_done)
            qdr_link_cleanup_protected_CT(core, conn, link, "Link detached");

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

        //
        // The auto link has failed. Periodically retry setting up the auto link until
        // it succeeds.
        //
        qdr_route_auto_link_detached_CT(core, link);
    }



    qdr_address_t *addr = link->owning_addr;
    if (addr)
        addr->ref_count++;

    if (link->link_direction == QD_INCOMING) {
        qdrc_event_link_raise(core, QDRC_EVENT_LINK_IN_DETACHED, link);
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
                // Unbind the address and the link.
                //
                qdr_core_unbind_address_link_CT(core, addr, link);

                //
                // If this is an edge data link, raise a link event to indicate its detachment.
                //
                if (link->conn->role == QDR_ROLE_EDGE_CONNECTION)
                    qdrc_event_link_raise(core, QDRC_EVENT_LINK_EDGE_DATA_DETACHED, link);
            }
            break;

        case QD_LINK_CONTROL:
            break;

        case QD_LINK_ROUTER:
            break;

        case QD_LINK_EDGE_DOWNLINK:
            break;
        }
    } else {
        //
        // Handle outgoing link cases
        //
        qdrc_event_link_raise(core, QDRC_EVENT_LINK_OUT_DETACHED, link);
        switch (link->link_type) {
        case QD_LINK_ENDPOINT:
        case QD_LINK_EDGE_DOWNLINK:
            if (addr) {
                qdr_core_unbind_address_link_CT(core, addr, link);
            }
            break;

        case QD_LINK_CONTROL:
            qdr_detach_link_control_CT(core, conn, link);
            break;

        case QD_LINK_ROUTER:
            qdr_detach_link_data_CT(core, conn, link);
            break;
        }
    }

    //
    // We had increased the ref_count if the link->no_route was true. Now reduce the ref_count
    //
    if (addr && link->no_route && link->no_route_bound) {
        addr->ref_count--;
    }

    link->owning_addr = 0;

    if (link->detach_count == 1) {
        //
        // Handle the disposition of any deliveries that remain on the link
        //
        qdr_link_cleanup_deliveries_CT(core, conn, link, false);

        //
        // If the detach occurred via protocol, send a detach back.
        //
        if (dt != QD_LOST) {
            qdr_link_outbound_detach_CT(core, link, 0, QDR_CONDITION_NONE, dt == QD_CLOSED);
        } else {
            // no detach can be sent out because the connection was lost
            qdr_link_cleanup_protected_CT(core, conn, link, "Link lost");
        }
    } else if (link->detach_send_done) {  // detach count indicates detach has been scheduled
        // I/O thread is finished sending detach, ok to free link now

        qdr_link_cleanup_protected_CT(core, conn, link, "Link detached");
    }

    //
    // If there was an address associated with this link, check to see if any address-related
    // cleanup has to be done.
    //
    if (addr) {
        addr->ref_count--;
        qdr_check_addr_CT(core, addr);
    }

    if (error)
        qdr_error_free(error);
}


/* invoked on core thread to signal that the I/O thread has sent the detach
 */
static void qdr_link_detach_sent_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_link_t *link = safe_deref_qdr_link_t(action->args.connection.link);

    if (discard || !link)
        return;

    link->detach_send_done = true;
    if (link->conn && link->detach_received)
        qdr_link_cleanup_protected_CT(core, link->conn, link, "Link detached");
}


static void qdr_link_processing_complete_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_link_t *link = safe_deref_qdr_link_t(action->args.connection.link);
    if (discard || !link)
        return;

    qdr_link_cleanup_CT(core, link->conn, link, "Link cleanup deferred after IO processing");
}

