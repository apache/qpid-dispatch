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

// This must be first to work around a Python bug
//
// clang-format off
#include "python_private.h"
// clang-format on

#include <errno.h>
#include <inttypes.h>
#include <proton/condition.h>
#include <proton/event.h>
#include <proton/listener.h>
#include <proton/netaddr.h>
#include <proton/proactor.h>
#include <proton/sasl.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/amqp.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/failoverlist.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/platform.h>
#include <qpid/dispatch/python_embedded.h>
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/threading.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "connection.h"
#include "connector.h"
#include "dispatch_private.h"
#include "entity.h"
#include "entity_cache.h"
#include "listener.h"
#include "policy.h"
#include "remote_sasl.h"
#include "server_private.h"
#include "timer_private.h"

#define HEARTBEAT_INTERVAL 1000

ALLOC_DEFINE(qd_deferred_call_t);

// Save displayNameService object instance and ImportModule address.
// Called with qd_python_lock held.
//
// XXX But it doesn't use the _lh convention?
qd_error_t qd_register_display_name_service(qd_dispatch_t *qd, void *displaynameservice)
{
    if (displaynameservice) {
        qd->server->py_displayname_obj = displaynameservice;
        Py_XINCREF((PyObject *) qd->server->py_displayname_obj);
        return QD_ERROR_NONE;
    } else {
        return qd_error(QD_ERROR_VALUE, "displaynameservice is not set");
    }
}

qd_error_t qd_entity_refresh_sslProfile(qd_entity_t *entity, void *impl)
{
    return QD_ERROR_NONE;
}

qd_error_t qd_entity_refresh_authServicePlugin(qd_entity_t *entity, void *impl)
{
    return QD_ERROR_NONE;
}

qd_server_t *qd_server(qd_dispatch_t *qd, int thread_count, const char *container_name, const char *sasl_config_path,
                       const char *sasl_config_name)
{
    // Initialize const members. Zero initialize all others.

    qd_server_t tmp = {.thread_count = thread_count};
    // XXX This type contructor is different from what I see elsewhere.
    qd_server_t *server = NEW(qd_server_t);

    if (!server) {
        return NULL;
    }

    memcpy(server, &tmp, sizeof(tmp));

    server->qd                   = qd;
    server->log_source           = qd_log_source("SERVER");
    server->protocol_log_source  = qd_log_source("PROTOCOL");
    server->container_name       = container_name;
    server->sasl_config_path     = sasl_config_path;
    server->sasl_config_name     = sasl_config_name;
    server->proactor             = pn_proactor();
    server->container            = NULL;
    server->start_context        = 0;  // XXX Should be NULL?
    server->lock                 = sys_mutex();
    server->conn_activation_lock = sys_mutex();
    server->cond                 = sys_cond();

    DEQ_INIT(server->conn_list);
    qd_timer_initialize(server->lock);

    server->pause_requests      = 0;
    server->threads_paused      = 0;
    server->pause_next_sequence = 0;
    server->pause_now_serving   = 0;
    server->next_connection_id  = 1;
    server->py_displayname_obj  = 0;

    server->http = qd_http_server(server, server->log_source);

    qd_log(server->log_source, QD_LOG_INFO, "Container Name: %s", server->container_name);

    return server;
}

void qd_server_free(qd_server_t *server)
{
    if (!server) {
        return;
    }

    qd_connection_t *conn = DEQ_HEAD(server->conn_list);

    while (conn) {
        qd_log(server->log_source, QD_LOG_INFO, "[C%" PRIu64 "] Closing connection on shutdown", conn->connection_id);

        DEQ_REMOVE_HEAD(server->conn_list);

        if (conn->pn_conn) {
            pn_transport_t *transport = pn_connection_transport(conn->pn_conn);

            if (transport) {
                // For the transport tracer
                pn_transport_set_context(transport, 0);
            }

            qd_session_cleanup(conn);
            pn_connection_set_context(conn->pn_conn, 0);
        }

        if (conn->free_user_id) {
            free((char *) conn->user_id);
        }

        sys_mutex_free(conn->deferred_call_lock);

        free(conn->name);
        free(conn->role);

        if (conn->policy_settings) {
            free_qd_policy_settings_t(conn->policy_settings);
        }

        free_qd_connection_t(conn);

        conn = DEQ_HEAD(server->conn_list);
    }

    pn_proactor_free(server->proactor);
    qd_timer_finalize();
    sys_mutex_free(server->lock);
    sys_mutex_free(server->conn_activation_lock);
    sys_cond_free(server->cond);
    Py_XDECREF((PyObject *) server->py_displayname_obj);

    free(server);
}

void qd_server_set_container(qd_dispatch_t *qd, qd_container_t *container)
{
    qd->server->container = container;
}

void qd_server_trace_all_connections()
{
    qd_dispatch_t *qd = qd_dispatch_get_dispatch();

    if (qd->server) {
        sys_mutex_lock(qd->server->lock);

        qd_connection_list_t conn_list = qd->server->conn_list;
        qd_connection_t *conn          = DEQ_HEAD(conn_list);

        while (conn) {
            // If there is already a tracer on the transport, nothing
            // to do, move on to the next connection.
            pn_transport_t *transport = pn_connection_transport(conn->pn_conn);

            if (!pn_transport_get_tracer(transport)) {
                pn_transport_trace(transport, PN_TRACE_FRM);
                pn_transport_set_tracer(transport, transport_tracer);
            }

            conn = DEQ_NEXT(conn);
        }

        sys_mutex_unlock(qd->server->lock);
    }
}

static double normalize_memory_size(const uint64_t bytes, const char **suffix);
static void *server_thread_run(void *arg);

void qd_server_run(qd_dispatch_t *qd)
{
    qd_server_t *server = qd->server;
    int i;

    assert(server);
    // Server can't run without a container
    assert(server->container);

    qd_log(server->log_source, QD_LOG_NOTICE, "Operational, %d Threads Running (process ID %ld)", server->thread_count,
           (long) getpid());

    const uintmax_t ram_size = qd_platform_memory_size();
    const uint64_t vm_size   = qd_router_memory_usage();

    if (ram_size && vm_size) {
        const char *suffix_vm  = 0;
        const char *suffix_ram = 0;
        double vm              = normalize_memory_size(vm_size, &suffix_vm);
        double ram             = normalize_memory_size(ram_size, &suffix_ram);

        qd_log(server->log_source, QD_LOG_NOTICE, "Process VmSize %.2f %s (%.2f %s available memory)", vm, suffix_vm,
               ram, suffix_ram);
    }

#ifndef NDEBUG
    qd_log(server->log_source, QD_LOG_INFO, "Running in DEBUG Mode");
#endif

    // Start count - 1 threads and use the current thread

    int n                  = server->thread_count - 1;
    sys_thread_t **threads = (sys_thread_t **) calloc(n, sizeof(sys_thread_t *));

    for (i = 0; i < n; i++) {
        threads[i] = sys_thread(server_thread_run, server);
    }

    // Use the current thread
    server_thread_run(server);

    for (i = 0; i < n; i++) {
        sys_thread_join(threads[i]);
        sys_thread_free(threads[i]);
    }

    free(threads);

    // Stop HTTP threads immediately
    qd_http_server_stop(server->http);
    qd_http_server_free(server->http);

    qd_log(server->log_source, QD_LOG_NOTICE, "Shut Down");
}

void qd_server_stop(qd_dispatch_t *qd)
{
    // Interrupt the proactor.  Async-signal safe.
    pn_proactor_interrupt(qd->server->proactor);
}

void qd_server_activate(qd_connection_t *conn)
{
    if (conn) {
        conn->wake(conn);
    }
}

qd_dispatch_t *qd_server_dispatch(qd_server_t *server)
{
    return server->qd;
}

// Permit replacement by dummy implementation in unit_tests
__attribute__((noinline)) void qd_server_timeout(qd_server_t *server, qd_duration_t duration)
{
    pn_proactor_set_timeout(server->proactor, duration);
}

sys_mutex_t *qd_server_get_activation_lock(qd_server_t *server)
{
    return server->conn_activation_lock;
}

static void connection_wake(qd_connection_t *conn);
static void connection_decorate(qd_server_t *server, qd_connection_t *conn, const qd_server_config_t *config);

// Construct a new qd_connection. Thread safe.
qd_connection_t *qd_server_connection(qd_server_t *server, qd_server_config_t *config)
{
    qd_connection_t *conn = new_qd_connection_t();

    if (!conn) {
        return NULL;
    }

    ZERO(conn);

    conn->pn_conn            = pn_connection();
    conn->deferred_call_lock = sys_mutex();
    conn->role               = strdup(config->role);

    if (!conn->pn_conn || !conn->deferred_call_lock || !conn->role) {
        if (conn->pn_conn) {
            pn_connection_free(conn->pn_conn);
        }

        if (conn->deferred_call_lock) {
            sys_mutex_free(conn->deferred_call_lock);
        }

        free(conn->role);
        free(conn);

        return NULL;
    }

    conn->server = server;

    // The default.  It is overridden for HTTP connections.
    conn->wake = connection_wake;

    pn_connection_set_context(conn->pn_conn, conn);

    DEQ_ITEM_INIT(conn);
    DEQ_INIT(conn->deferred_calls);
    DEQ_INIT(conn->free_link_session_list);

    sys_mutex_lock(server->lock);
    conn->connection_id = server->next_connection_id++;
    DEQ_INSERT_TAIL(server->conn_list, conn);
    sys_mutex_unlock(server->lock);

    connection_decorate(conn->server, conn, config);

    return conn;
}

qd_listener_t *qd_server_listener(qd_server_t *server)
{
    qd_listener_t *listener = new_qd_listener_t();

    if (!listener) {
        return 0;
    }

    ZERO(listener);
    sys_atomic_init(&listener->ref_count, 1);

    listener->server = server;
    listener->http   = NULL;

    return listener;
}

static void connector_try_open_handler(void *context);

qd_connector_t *qd_server_connector(qd_server_t *server)
{
    qd_connector_t *connector = new_qd_connector_t();

    if (!connector) {
        return NULL;
    }

    ZERO(connector);
    sys_atomic_init(&connector->ref_count, 1);

    connector->server = server;
    qd_failover_item_list_t conn_info_list;
    DEQ_INIT(conn_info_list);
    connector->conn_info_list = conn_info_list;
    connector->conn_index     = 1;
    connector->state          = CXTR_STATE_INIT;
    connector->lock           = sys_mutex();
    connector->timer          = qd_timer(connector->server->qd, connector_try_open_handler, connector);

    connector->conn_msg = (char *) malloc(300);
    memset(connector->conn_msg, 0, 300);

    if (!connector->lock || !connector->timer) {
        qd_connector_decref(connector);
        return NULL;
    }

    return connector;
}

static double normalize_memory_size(const uint64_t bytes, const char **suffix)
{
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    const int units_ct               = 5;
    const double base                = 1024.0;
    double value                     = (double) bytes;

    for (int i = 0; i < units_ct; ++i) {
        if (value < base) {
            if (suffix) {
                *suffix = units[i];
            }

            return value;
        }

        value /= base;
    }

    if (suffix) {
        *suffix = units[units_ct - 1];
    }

    return value;
}

void qd_conn_event_batch_complete(qd_container_t *container, qd_connection_t *conn, bool conn_closed);

static void *server_thread_run(void *arg)
{
    qd_server_t *server     = (qd_server_t *) arg;
    pn_proactor_t *proactor = server->proactor;
    bool running            = true;

    while (running) {
        pn_event_batch_t *batch  = pn_proactor_wait(proactor);
        pn_event_t *event        = pn_event_batch_next(batch);
        pn_connection_t *pn_conn = pn_event_connection(event);

        do {
            running = qd_server_handle_event(server, event);
        } while ((event = pn_event_batch_next(batch)));

        if (pn_conn) {
            qd_connection_t *conn = (qd_connection_t *) pn_connection_get_context(pn_conn);

            if (conn) {
                qd_conn_event_batch_complete(server->container, conn, false);
            }
        }

        pn_proactor_done(proactor, batch);
    }

    return NULL;
}

// Wake function for proactor-managed connections
static void connection_wake(qd_connection_t *conn)
{
    assert(conn);

    pn_connection_wake(conn->pn_conn);
}

static void data_put_symbol(pn_data_t *data, const char *value)
{
    pn_data_put_symbol(data, pn_bytes(strlen(value), value));
}

static void data_put_string(pn_data_t *data, const char *value)
{
    pn_data_put_string(data, pn_bytes(strlen(value), value));
}

static void data_put_map_entry(pn_data_t *data, const char *key, const char *value)
{
    data_put_symbol(data, key);
    data_put_string(data, value);
}

static void connection_decorate(qd_server_t *server, qd_connection_t *conn, const qd_server_config_t *config)
{
    pn_connection_t *pn_conn = conn->pn_conn;

    // Set the container name
    pn_connection_set_container(pn_conn, server->container_name);

    // Offer ANONYMOUS_RELAY capability
    size_t clen = strlen(QD_CAPABILITY_ANONYMOUS_RELAY);
    pn_data_put_symbol(pn_connection_offered_capabilities(pn_conn),
                       pn_bytes(clen, (char *) QD_CAPABILITY_ANONYMOUS_RELAY));

    // Create the connection properties map

    pn_data_t *props = pn_connection_properties(pn_conn);

    pn_data_put_map(props);
    pn_data_enter(props);

    data_put_map_entry(props, QD_CONNECTION_PROPERTY_PRODUCT_KEY, QD_CONNECTION_PROPERTY_PRODUCT_VALUE);
    data_put_map_entry(props, QD_CONNECTION_PROPERTY_VERSION_KEY, QPID_DISPATCH_VERSION);

    data_put_symbol(props, QD_CONNECTION_PROPERTY_CONN_ID);
    pn_data_put_int(props, conn->connection_id);

    // XXX When is config going to be null?
    if (config && config->inter_router_cost > 1) {
        data_put_symbol(props, QD_CONNECTION_PROPERTY_COST_KEY);
        pn_data_put_int(props, config->inter_router_cost);
    }

    if (config) {
        qd_failover_list_t *fol = config->failover_list;

        if (fol) {
            data_put_symbol(props, QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY);
            pn_data_put_list(props);

            pn_data_enter(props);

            int fol_count = qd_failover_list_size(fol);

            for (int i = 0; i < fol_count; i++) {
                pn_data_put_map(props);
                pn_data_enter(props);

                data_put_map_entry(props, QD_CONNECTION_PROPERTY_FAILOVER_NETHOST_KEY, qd_failover_list_host(fol, i));
                data_put_map_entry(props, QD_CONNECTION_PROPERTY_FAILOVER_PORT_KEY, qd_failover_list_port(fol, i));

                if (qd_failover_list_scheme(fol, i)) {
                    data_put_map_entry(props, QD_CONNECTION_PROPERTY_FAILOVER_SCHEME_KEY,
                                       qd_failover_list_scheme(fol, i));
                }

                if (qd_failover_list_hostname(fol, i)) {
                    data_put_map_entry(props, QD_CONNECTION_PROPERTY_FAILOVER_HOSTNAME_KEY,
                                       qd_failover_list_hostname(fol, i));
                }

                pn_data_exit(props);
            }

            pn_data_exit(props);
        }
    }

    pn_data_exit(props);
}

// Timer callback to try or retry connection open
static void connector_try_open_lh(qd_connector_t *connector)
{
    qd_log_source_t *log = connector->server->log_source;

    if (connector->state != CXTR_STATE_CONNECTING && connector->state != CXTR_STATE_INIT) {
        // No longer referenced by pn_connection or timer
        qd_connector_decref(connector);
        return;
    }

    qd_connection_t *conn = qd_server_connection(connector->server, &connector->config);

    if (!conn) {
        // Try again later
        qd_log(log, QD_LOG_CRITICAL, "Allocation failure connecting to %s", connector->config.host_port);
        connector->delay = 10000;
        sys_atomic_inc(&connector->ref_count);
        qd_timer_schedule(connector->timer, connector->delay);
        return;
    }

    conn->connector                  = connector;
    const qd_server_config_t *config = &connector->config;

    // Set the hostname on the pn_connection. This hostname will be
    // used by proton as the hostname in the open frame.

    qd_failover_item_t *item = qd_connector_get_conn_info(connector);
    char *current_host       = item->host;
    char *host_port          = item->host_port;

    pn_connection_set_hostname(conn->pn_conn, current_host);

    // Set the sasl user name and password on the proton connection
    // object. This has to be done before pn_proactor_connect, which
    // will bind a transport to the connection.

    if (config->sasl_username) {
        pn_connection_set_user(conn->pn_conn, config->sasl_username);
    }

    if (config->sasl_password) {
        pn_connection_set_password(conn->pn_conn, config->sasl_password);
    }

    conn->connector->state = CXTR_STATE_OPEN;
    connector->ctx         = conn;
    connector->delay       = 5000;

    qd_log(log, QD_LOG_TRACE, "[C%" PRIu64 "] Connecting to %s", conn->connection_id, host_port);

    // Note: The transport is configured in the PN_CONNECTION_BOUND
    // event
    pn_proactor_connect(connector->server->proactor, conn->pn_conn, host_port);
}

static void connector_try_open_handler(void *context)
{
    qd_connector_t *connector = (qd_connector_t *) context;

    if (!qd_connector_decref(connector)) {
        // TODO aconway 2017-05-09: this lock looks too big
        sys_mutex_lock(connector->lock);
        connector_try_open_lh(connector);
        sys_mutex_unlock(connector->lock);
    }
}

// This function is set as the pn_transport->tracer and is invoked
// when proton tries to write the log message to pn_transport->tracer
void transport_tracer(pn_transport_t *transport, const char *message)
{
    qd_connection_t *conn = (qd_connection_t *) pn_transport_get_context(transport);

    if (conn) {
        // The PROTOCOL module is used exclusively for logging
        // protocol related tracing. The protocol could be AMQP, HTTP,
        // TCP, etc.
        qd_log(conn->server->protocol_log_source, QD_LOG_TRACE, "[C%" PRIu64 "]:%s", conn->connection_id, message);
    }
}

// XXX I don't see how connection_ clarifies this function's distinct purpose
void connection_transport_tracer(pn_transport_t *transport, const char *message)
{
    qd_connection_t *conn = (qd_connection_t *) pn_transport_get_context(transport);

    if (conn) {
        // Unconditionally write the log at TRACE level to the log
        // file
        qd_log_impl_v1(conn->server->protocol_log_source, QD_LOG_TRACE, __FILE__, __LINE__, "[C%" PRIu64 "]:%s",
                       conn->connection_id, message);
    }
}
