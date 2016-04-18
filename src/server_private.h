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

#include <qpid/dispatch/enum.h>
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/user_fd.h>
#include "alloc.h"
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/driver.h>
#include <proton/engine.h>
#include <proton/event.h>

#include "dispatch_private.h"
#include "timer_private.h"

void qd_server_timer_pending_LH(qd_timer_t *timer);
void qd_server_timer_cancel_LH(qd_timer_t *timer);

#define CONTEXT_NO_OWNER -1
#define CONTEXT_UNSPECIFIED_OWNER -2

typedef enum {
    QD_BIND_SUCCESSFUL, // Bind to socket was attempted and the bind succeeded
    QD_BIND_FAILED,     // Bind to socket was attempted and bind failed
    QD_BIND_NONE,    // Bind to socket not attempted yet
} qd_bind_state_t;

typedef enum {
    CXTR_STATE_CONNECTING = 0,
    CXTR_STATE_OPEN,
    CXTR_STATE_FAILED
} cxtr_state_t;


/**
 * Listener objects represent the desire to accept incoming transport connections.
 */
struct qd_listener_t {
    qd_server_t              *server;
    const qd_server_config_t *config;
    void                     *context;
    qdpn_listener_t          *pn_listener;
};


/**
 * Connector objects represent the desire to create and maintain an outgoing transport connection.
 */
struct qd_connector_t {
    qd_server_t              *server;
    cxtr_state_t              state;
    const qd_server_config_t *config;
    void                     *context;
    qd_connection_t          *ctx;
    qd_timer_t               *timer;
    long                      delay;
};


typedef struct qd_deferred_call_t {
    DEQ_LINKS(struct qd_deferred_call_t);
    qd_deferred_t  call;
    void          *context;
} qd_deferred_call_t;

DEQ_DECLARE(qd_deferred_call_t, qd_deferred_call_list_t);

/**
 * Connection objects wrap Proton connection objects.
 */
struct qd_connection_t {
    DEQ_LINKS(qd_connection_t);
    qd_server_t              *server;
    bool                      opened; // An open callback was invoked for this connection
    bool                      closed;
    int                       owner_thread;
    int                       enqueued;
    qdpn_connector_t         *pn_cxtr;
    pn_connection_t          *pn_conn;
    pn_collector_t           *collector;
    pn_ssl_t                 *ssl;
    qd_listener_t            *listener;
    qd_connector_t           *connector;
    void                     *context; // Copy of context from listener or connector
    void                     *user_context;
    void                     *link_context; // Context shared by this connection's links
    qd_user_fd_t             *ufd;
    uint64_t                  connection_id; // A unique identifier for the qd_connection_t. The underlying pn_connection already has one but it is long and clunky.
    const char               *user_id; // A unique identifier for the user on the connection. This is currently populated  from the client ssl cert. See ssl_uid_format in server.h for more info
    bool                      free_user_id;
    qd_policy_settings_t     *policy_settings;
    int                       n_sessions;
    int                       n_senders;
    int                       n_receivers;
    void                     *open_container;
    qd_deferred_call_list_t   deferred_calls;
    sys_mutex_t              *deferred_call_lock;
    bool                      event_stall;
    bool                      policy_counted;
};

DEQ_DECLARE(qd_connection_t, qd_connection_list_t);


struct qd_user_fd_t {
    qd_server_t      *server;
    void             *context;
    int               fd;
    qdpn_connector_t *pn_conn;
};


typedef struct qd_thread_t {
    qd_server_t  *qd_server;
    int           thread_id;
    volatile int  running;
    volatile int  canceled;
    int           using_thread;
    sys_thread_t *thread;
} qd_thread_t;


typedef struct qd_work_item_t {
    DEQ_LINKS(struct qd_work_item_t);
    qdpn_connector_t *cxtr;
} qd_work_item_t;

DEQ_DECLARE(qd_work_item_t, qd_work_list_t);


struct qd_server_t {
    qd_dispatch_t            *qd;
    int                       thread_count;
    const char               *container_name;
    const char               *sasl_config_path;
    const char               *sasl_config_name;
    qdpn_driver_t            *driver;
    qd_log_source_t          *log_source;
    qd_thread_start_cb_t      start_handler;
    qd_conn_handler_cb_t      conn_handler;
    qd_pn_event_handler_cb_t  pn_event_handler;
    qd_user_fd_handler_cb_t   ufd_handler;
    void                     *start_context;
    void                     *conn_handler_context;
    sys_cond_t               *cond;
    sys_mutex_t              *lock;
    qd_thread_t             **threads;
    qd_work_list_t            work_queue;
    qd_timer_list_t           pending_timers;
    bool                      a_thread_is_waiting;
    int                       threads_active;
    int                       pause_requests;
    int                       threads_paused;
    int                       pause_next_sequence;
    int                       pause_now_serving;
    qd_signal_handler_cb_t    signal_handler;
    void                     *signal_context;
    int                       pending_signal;
    qd_connection_list_t      connections;
    qd_timer_t               *heartbeat_timer;
    uint64_t                 next_connection_id;
};

ALLOC_DECLARE(qd_work_item_t);
ALLOC_DECLARE(qd_listener_t);
ALLOC_DECLARE(qd_deferred_call_t);
ALLOC_DECLARE(qd_connector_t);
ALLOC_DECLARE(qd_connection_t);
ALLOC_DECLARE(qd_user_fd_t);

#endif
