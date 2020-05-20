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

#include <proton/engine.h>
#include <proton/event.h>
#include <proton/ssl.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/atomic.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/enum.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/threading.h>

#include "dispatch_private.h"
#include "http.h"
#include "timer_private.h"

typedef struct qd_deferred_call_t {
    DEQ_LINKS(struct qd_deferred_call_t);
    qd_deferred_t call;
    void*         context;
} qd_deferred_call_t;

typedef struct qd_pn_free_link_session_t {
    DEQ_LINKS(struct qd_pn_free_link_session_t);
    pn_session_t* pn_session;
    pn_link_t*    pn_link;
} qd_pn_free_link_session_t;

DEQ_DECLARE(qd_connection_t, qd_connection_list_t);  // XXX JRR

struct qd_server_t {
    qd_dispatch_t*       qd;
    const int            thread_count;  // Immutable
    const char*          container_name;
    const char*          sasl_config_path;
    const char*          sasl_config_name;
    pn_proactor_t*       proactor;
    qd_container_t*      container;
    qd_log_source_t*     log_source;
    qd_log_source_t*     protocol_log_source;  // Log source for the PROTOCOL module
    void*                start_context;
    sys_cond_t*          cond;
    sys_mutex_t*         lock;
    qd_connection_list_t conn_list;
    int                  pause_requests;
    int                  threads_paused;
    int                  pause_next_sequence;
    int                  pause_now_serving;
    uint64_t             next_connection_id;
    void*                py_displayname_obj;
    qd_http_server_t*    http;
    sys_mutex_t*         conn_activation_lock;
};

DEQ_DECLARE(qd_deferred_call_t, qd_deferred_call_list_t);
DEQ_DECLARE(qd_pn_free_link_session_t, qd_pn_free_link_session_list_t);

ALLOC_DECLARE(qd_deferred_call_t);
ALLOC_DECLARE(qd_pn_free_link_session_t);

qd_connection_t* qd_server_connection(qd_server_t* server, qd_server_config_t* config);
qd_connector_t*  qd_server_connector(qd_server_t* server);
qd_dispatch_t*   qd_server_dispatch(qd_server_t* server);
qd_listener_t*   qd_server_listener(qd_server_t* server);
void             qd_server_config_free(qd_server_config_t* config);
void             qd_server_timeout(qd_server_t* server, qd_duration_t delay);

/// For every connection on the server's connection list, call
/// pn_transport_set_tracer and enable trace logging
void qd_server_trace_all_connections();

/// This function is set as the pn_transport->tracer and is invoked
/// when proton tries to write the log message to pn_transport->tracer
void transport_tracer(pn_transport_t* transport, const char* message);

/// This is similar to the transport_tracer, but it writes the message
/// to the log at the trace level even if trace level is not enabled
void connection_transport_tracer(pn_transport_t* transport, const char* message);

#endif
