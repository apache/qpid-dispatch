#ifndef __connection_private_h__
#define __connection_private_h__ 1

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

#include <proton/engine.h>
#include <proton/event.h>
#include <proton/ssl.h>
#include <qpid/dispatch/container.h>
#include <qpid/dispatch/server.h>

#include "server_private.h"

// Connection objects wrap Proton connection objects.
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

//DEQ_DECLARE(qd_connection_t, qd_connection_list_t); // XXX JRR
ALLOC_DECLARE(qd_connection_t);

qd_connector_t* qd_connection_connector(const qd_connection_t* conn);
bool qd_connection_handle(qd_connection_t *conn, pn_event_t* event);
void qd_connection_free(qd_connection_t *conn);

#endif
