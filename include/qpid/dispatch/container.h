#ifndef __dispatch_container_h__
#define __dispatch_container_h__ 1
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

/**@file
 * Container for nodes, links and deliveries.
 *
 * @defgroup container container
 *
 * Container for nodes, links and deliveries.
 *
 * @{
 */

#include <proton/engine.h>
#include <qpid/dispatch/dispatch.h>
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/alloc.h>

typedef uint8_t qd_dist_mode_t;
#define QD_DIST_COPY 0x01
#define QD_DIST_MOVE 0x02
#define QD_DIST_BOTH 0x03

/**
 * Node Lifetime Policy (see AMQP 3.5.9)
 */
typedef enum {
    QD_LIFE_PERMANENT,
    QD_LIFE_DELETE_CLOSE,
    QD_LIFE_DELETE_NO_LINKS,
    QD_LIFE_DELETE_NO_MESSAGES,
    QD_LIFE_DELETE_NO_LINKS_MESSAGES
} qd_lifetime_policy_t;


/**
 * Link Direction
 */
typedef enum {
    QD_INCOMING,
    QD_OUTGOING
} qd_direction_t;


typedef enum {
    QD_DETACHED,  // Protocol detach
    QD_CLOSED,    // Protocol close
    QD_LOST       // Connection or session closed
} qd_detach_type_t;


typedef struct qd_node_t     qd_node_t;
typedef struct qd_link_t     qd_link_t;

ALLOC_DECLARE(qd_link_t);

typedef bool (*qd_container_delivery_handler_t)                  (void *node_context, qd_link_t *link);
typedef void (*qd_container_disposition_handler_t)               (void *node_context, qd_link_t *link, pn_delivery_t *pnd);
typedef int  (*qd_container_link_handler_t)                      (void *node_context, qd_link_t *link);
typedef int  (*qd_container_link_detach_handler_t)               (void *node_context, qd_link_t *link, qd_detach_type_t dt);
typedef void (*qd_container_node_handler_t)                      (void *type_context, qd_node_t *node);
typedef int  (*qd_container_conn_handler_t)                      (void *type_context, qd_connection_t *conn, void *context);
typedef void (*qd_container_link_abandoned_deliveries_handler_t) (void *node_context, qd_link_t *link);

/**
 * A set  of Node handlers for deliveries, links and container events.
 */
typedef struct {
    char *type_name;
    void *type_context;
    int   allow_dynamic_creation;

    /** @name Node-Instance Handlers
     * @{
     */

    /** Invoked when a new or existing received delivery is avaliable for processing. */
    qd_container_delivery_handler_t rx_handler;

    /** Invoked when an existing delivery changes disposition or settlement state. */
    qd_container_disposition_handler_t disp_handler;

    /** Invoked when an attach for a new incoming link is received. */
    qd_container_link_handler_t incoming_handler;

    /** Invoked when an attach for a new outgoing link is received. */
    qd_container_link_handler_t outgoing_handler;

    /** Invoked when an activated connection is available for writing. */
    qd_container_conn_handler_t writable_handler;

    /** Invoked when a link is detached. */
    qd_container_link_detach_handler_t link_detach_handler;
    ///@}

    /** Invoked when a link we created was opened by the peer */
    qd_container_link_handler_t link_attach_handler;

    qd_container_link_abandoned_deliveries_handler_t link_abandoned_deliveries_handler;

    /** Invoked when a link receives a flow event */
    qd_container_link_handler_t link_flow_handler;

    /** @name Node-Type Handlers
     * @{
     */

    /** Invoked when a new instance of the node-type is created. */
    qd_container_node_handler_t  node_created_handler;

    /** Invoked when an instance of the node type is destroyed. */
    qd_container_node_handler_t  node_destroyed_handler;

    /** Invoked when an incoming connection (via listener) is opened. */
    qd_container_conn_handler_t  inbound_conn_opened_handler;

    /** Invoked when an outgoing connection (via connector) is opened. */
    qd_container_conn_handler_t  outbound_conn_opened_handler;

    /** Invoked when a connection is closed. */
    qd_container_conn_handler_t  conn_closed_handler;
} qd_node_type_t;


int qd_container_register_node_type(qd_dispatch_t *dispatch, const qd_node_type_t *nt);

qd_node_t *qd_container_set_default_node_type(qd_dispatch_t        *dispatch,
                                              const qd_node_type_t *nt,
                                              void                 *node_context,
                                              qd_dist_mode_t        supported_dist);

qd_node_t *qd_container_create_node(qd_dispatch_t        *dispatch,
                                    const qd_node_type_t *nt,
                                    const char           *name,
                                    void                 *node_context,
                                    qd_dist_mode_t        supported_dist,
                                    qd_lifetime_policy_t  life_policy);
void qd_container_destroy_node(qd_node_t *node);

void qd_container_node_set_context(qd_node_t *node, void *node_context);
qd_dist_mode_t qd_container_node_get_dist_modes(const qd_node_t *node);
qd_lifetime_policy_t qd_container_node_get_life_policy(const qd_node_t *node);

qd_link_t *qd_link(qd_node_t *node, qd_connection_t *conn, qd_direction_t dir, const char *name);
void qd_link_free(qd_link_t *link);

/**
 * List of reference in the qd_link used to track abandoned deliveries
 */
typedef struct qd_link_ref_t {
    DEQ_LINKS(struct qd_link_ref_t);
    void *ref;
} qd_link_ref_t;

ALLOC_DECLARE(qd_link_ref_t);
DEQ_DECLARE(qd_link_ref_t, qd_link_ref_list_t);

qd_link_ref_list_t *qd_link_get_ref_list(qd_link_t *link);

/**
 * Context associated with the link for storing link-specific state.
 */
void qd_link_set_context(qd_link_t *link, void *link_context);
void *qd_link_get_context(qd_link_t *link);

void policy_notify_opened(void *container, qd_connection_t *conn, void *context);
qd_direction_t qd_link_direction(const qd_link_t *link);
bool qd_link_is_q2_limit_unbounded(qd_link_t *link);
void qd_link_set_q2_limit_unbounded(qd_link_t *link, bool q2_limit_unbounded);
pn_snd_settle_mode_t qd_link_remote_snd_settle_mode(const qd_link_t *link);
qd_connection_t *qd_link_connection(qd_link_t *link);
pn_link_t *qd_link_pn(qd_link_t *link);
pn_session_t *qd_link_pn_session(qd_link_t *link);
pn_terminus_t *qd_link_source(qd_link_t *link);
pn_terminus_t *qd_link_target(qd_link_t *link);
pn_terminus_t *qd_link_remote_source(qd_link_t *link);
pn_terminus_t *qd_link_remote_target(qd_link_t *link);
void qd_link_activate(qd_link_t *link);
void qd_link_close(qd_link_t *link);
void qd_link_detach(qd_link_t *link);
void qd_link_free(qd_link_t *link);
void *qd_link_get_node_context(const qd_link_t *link);
void qd_link_restart_rx(qd_link_t *link);
void qd_connection_log_policy_denial(qd_link_t *link, const char *text);

///@}
#endif
