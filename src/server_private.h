#ifndef __server_private_h__
#define __server_private_h__ 1
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

#include "dispatch_private.h"
#include "http.h"
#include "timer_private.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/atomic.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/enum.h"
#include "qpid/dispatch/log.h"
#include "qpid/dispatch/server.h"
#include "qpid/dispatch/threading.h"

#include <proton/engine.h>
#include <proton/event.h>
#include <proton/ssl.h>

#include <netdb.h>              /* For NI_MAXHOST/NI_MAXSERV */

qd_dispatch_t* qd_server_dispatch(qd_server_t *server);
void qd_server_timeout(qd_server_t *server, qd_duration_t delay);

qd_connection_t *qd_server_connection(qd_server_t *server, qd_server_config_t* config);

qd_connector_t* qd_connection_connector(const qd_connection_t *c);

bool qd_connection_handle(qd_connection_t *c, pn_event_t *e);

uint64_t qd_server_allocate_connection_id(qd_server_t *server);


const qd_server_config_t *qd_connector_config(const qd_connector_t *c);

qd_listener_t *qd_server_listener(qd_server_t *server);
qd_connector_t *qd_server_connector(qd_server_t *server);

void qd_connector_decref(qd_connector_t* ct);
void qd_listener_decref(qd_listener_t* ct);
void qd_server_config_free(qd_server_config_t *cf);

typedef enum {
    CXTR_STATE_INIT = 0,
    CXTR_STATE_CONNECTING,
    CXTR_STATE_OPEN,
    CXTR_STATE_FAILED,
    CXTR_STATE_DELETED  // by management
} cxtr_state_t;


typedef struct qd_deferred_call_t {
    DEQ_LINKS(struct qd_deferred_call_t);
    qd_deferred_t  call;
    void          *context;
} qd_deferred_call_t;

DEQ_DECLARE(qd_deferred_call_t, qd_deferred_call_list_t);

typedef struct qd_pn_free_link_session_t {
    DEQ_LINKS(struct qd_pn_free_link_session_t);
    pn_session_t *pn_session;
    pn_link_t    *pn_link;
} qd_pn_free_link_session_t;

DEQ_DECLARE(qd_pn_free_link_session_t, qd_pn_free_link_session_list_t);

#ifndef NI_MAXHOST
# define NI_MAXHOST 1025
#endif

#ifndef NI_MAXSERV
# define NI_MAXSERV 32
#endif

pn_proactor_t* qd_server_proactor(qd_server_t *s);

qd_http_server_t *qd_server_http(qd_server_t *server);

typedef void (*qd_server_event_handler_t) (pn_event_t *e, qd_server_t *qd_server, void *context);

typedef struct qd_handler_context_t {
    void                      *context;
    qd_server_event_handler_t  handler;
} qd_handler_context_t;

/**
 * Listener objects represent the desire to accept incoming transport connections.
 */
struct qd_listener_t {
    /* May be referenced by connection_manager and pn_listener_t */
    qd_handler_context_t      type;
    sys_atomic_t              ref_count;
    qd_server_t              *server;
    qd_server_config_t        config;
    pn_listener_t            *pn_listener;
    qd_lws_listener_t        *http;
    DEQ_LINKS(qd_listener_t);
    bool                      exit_on_error;
};

DEQ_DECLARE(qd_listener_t, qd_listener_list_t);


/**
 * Connector objects represent the desire to create and maintain an outgoing transport connection.
 */
struct qd_connector_t {
    /* Referenced by connection_manager and pn_connection_t */
    sys_atomic_t              ref_count;
    qd_server_t              *server;
    qd_server_config_t        config;
    qd_timer_t               *timer;
    long                      delay;

    /* Connector state and ctx can be modified by I/O or management threads. */
    sys_mutex_t              *lock;
    cxtr_state_t              state;
    char                     *conn_msg;
    qd_connection_t          *qd_conn;

    /* This conn_list contains all the connection information needed to make a connection. It also includes failover connection information */
    qd_failover_item_list_t   conn_info_list;
    int                       conn_index; // Which connection in the connection list to connect to next.

    /* Optional policy vhost name */
    char                     *policy_vhost;

    DEQ_LINKS(qd_connector_t);
};

DEQ_DECLARE(qd_connector_t, qd_connector_list_t);

const char *qd_connector_policy_vhost(qd_connector_t* ct);

/**
 * Connection objects wrap Proton connection objects.
 */
struct qd_connection_t {
    DEQ_LINKS(qd_connection_t);
    char                            *name;
    qd_server_t                     *server;
    bool                            opened; // An open callback was invoked for this connection
    bool                            closed;
    bool                            closed_locally;
    int                             enqueued;
    qd_timer_t                      *timer;   // Timer for initial-setup
    pn_connection_t                 *pn_conn;
    pn_session_t                    *pn_sessions[QD_SSN_CLASS_COUNT];
    pn_ssl_t                        *ssl;
    qd_listener_t                   *listener;
    qd_connector_t                  *connector;
    void                            *context; // context from listener or connector
    void                            *user_context;
    void                            *link_context; // Context shared by this connection's links
    uint64_t                        connection_id; // A unique identifier for the qd_connection_t. The underlying pn_connection already has one but it is long and clunky.
    const char                      *user_id; // A unique identifier for the user on the connection. This is currently populated  from the client ssl cert. See ssl_uid_format in server.h for more info
    bool                            free_user_id;
    qd_policy_settings_t            *policy_settings;
    int                             n_sessions;
    int                             n_senders;
    int                             n_receivers;
    void                            *open_container;
    qd_deferred_call_list_t         deferred_calls;
    sys_mutex_t                     *deferred_call_lock;
    bool                            policy_counted;
    char                            *role;  //The specified role of the connection, e.g. "normal", "inter-router", "route-container" etc.
    qd_pn_free_link_session_list_t  free_link_session_list;
    bool                            strip_annotations_in;
    bool                            strip_annotations_out;
    void (*wake)(qd_connection_t*); /* Wake method, different for HTTP vs. proactor */
    char rhost[NI_MAXHOST];     /* Remote host numeric IP for incoming connections */
    char rhost_port[NI_MAXHOST+NI_MAXSERV]; /* Remote host:port for incoming connections */
};

DEQ_DECLARE(qd_connection_t, qd_connection_list_t);

ALLOC_DECLARE(qd_listener_t);
ALLOC_DECLARE(qd_deferred_call_t);
ALLOC_DECLARE(qd_connector_t);
ALLOC_DECLARE(qd_connection_t);
ALLOC_DECLARE(qd_pn_free_link_session_t);

/**
 * For every connection on the server's connection list, call pn_transport_set_tracer and enable trace logging
 */
void qd_server_trace_all_connections();

/**
 * This function is set as the pn_transport->tracer and is invoked when proton tries to write the log message to pn_transport->tracer
 */
void transport_tracer(pn_transport_t *transport, const char *message);

/**
 * This is similar to the transport_tracer but it writes the message to the log at the trace level even if trace level is not enabled.
 */
void connection_transport_tracer(pn_transport_t *transport, const char *message);

#endif
