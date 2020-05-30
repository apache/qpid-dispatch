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

#include <inttypes.h>
#include <proton/condition.h>
#include <proton/event.h>
#include <proton/listener.h>
#include <proton/netaddr.h>
#include <proton/proactor.h>
#include <proton/sasl.h>
#include <qpid/dispatch/server.h>
#include <stdio.h>

#include "connection.h"
#include "connector.h"
#include "listener.h"
#include "remote_sasl.h"
#include "server_private.h"

static void server_handle_listener_open(qd_server_t *server, qd_listener_t *listener)
{
    qd_log_source_t *log = server->log_source;
    qd_log(log, QD_LOG_TRACE, "Handling listener open");

    const char *port = listener->config.port;

    if (strcmp(port, "0") == 0) {
        // If a 0 (zero) is specified for a port, get the actual
        // listening port from the listener
        const pn_netaddr_t *addr   = pn_listener_addr(listener->pn_listener);
        char addr_str[PN_MAX_ADDR] = "";

        pn_netaddr_str(addr, addr_str, sizeof(addr_str));

        if (listener->config.name) {
            qd_log(log, QD_LOG_NOTICE, "Listening on %s (%s)", addr_str, listener->config.name);
        } else {
            qd_log(log, QD_LOG_NOTICE, "Listening on %s", addr_str);
        }
    } else {
        const char *host_port = listener->config.host_port;
        qd_log(log, QD_LOG_NOTICE, "Listening on %s", host_port);
    }
}

static void server_handle_listener_accept(qd_server_t *server, qd_listener_t *listener)
{
    qd_log_source_t *log = server->log_source;
    qd_log(log, QD_LOG_TRACE, "Handling listener accept");

    qd_connection_t *conn = qd_server_connection(listener->server, &listener->config);

    if (!conn) {
        qd_log(log, QD_LOG_CRITICAL, "Allocation failure during accept to %s", listener->config.host_port);
        return;
    }

    qd_log(log, QD_LOG_TRACE, "[C%" PRIu64 "] Accepting connection on %s", conn->connection_id,
           listener->config.host_port);

    conn->listener = listener;

    // Accept is asynchronous.  Configure the transport on
    // PN_CONNECTION_BOUND.
    pn_listener_accept(listener->pn_listener, conn->pn_conn);
}

static void server_handle_listener_close(qd_server_t *server, qd_listener_t *listener)
{
    qd_log_source_t *log = server->log_source;
    qd_log(log, QD_LOG_TRACE, "Handling listener close");

    pn_condition_t *cond  = pn_listener_condition(listener->pn_listener);
    const char *host_port = listener->config.host_port;

    if (pn_condition_is_set(cond)) {
        qd_log(log, QD_LOG_ERROR, "Listener error on %s: %s (%s)", host_port, pn_condition_get_description(cond),
               pn_condition_get_name(cond));

        if (listener->exit_on_error) {
            qd_log(log, QD_LOG_CRITICAL, "Shutting down: Required listener %s failed", host_port);
            exit(1);
        }
    } else {
        qd_log(log, QD_LOG_TRACE, "Listener closed on %s", host_port);
    }

    // XXX Shouldn't stuff like this happen in a listener cleanup function?
    pn_listener_set_context(listener->pn_listener, NULL);
    listener->pn_listener = NULL;
    qd_listener_decref(listener);
}

static void connection_startup_timer_handler(void *context);

static void server_handle_connection_init(qd_server_t *server, qd_connection_t *conn)
{
    qd_log(server->log_source, QD_LOG_TRACE, "[C%" PRIu64 "] Handling connection init", conn->connection_id);

    if (conn->listener) {
        const qd_server_config_t *config = &conn->listener->config;

        assert(config);

        if (config->initial_handshake_timeout_seconds > 0) {
            conn->timer = qd_timer(server->qd, connection_startup_timer_handler, conn);
            qd_timer_schedule(conn->timer, config->initial_handshake_timeout_seconds * 1000);
        }
    }
}

static void connection_set_rhost_port(qd_connection_t *conn);
static qd_error_t listener_setup_ssl(qd_connection_t *conn, const qd_server_config_t *config,
                                     pn_transport_t *transport);
static void connection_setup_ssl(qd_connection_t *conn);
static void connection_setup_sasl(qd_connection_t *conn);
static void connection_fail(qd_connection_t *conn, const char *name, const char *description, ...);

// Configure the transport once it is bound to the connection
static void server_handle_connection_bound(qd_server_t *server, qd_connection_t *conn)
{
    qd_log_source_t *log = server->log_source;
    qd_log(log, QD_LOG_TRACE, "[C%" PRIu64 "] Handling connection bound", conn->connection_id);

    pn_transport_t *transport = pn_connection_transport(conn->pn_conn);

    // For transport_tracer
    pn_transport_set_context(transport, conn);

    // Proton pushes out its trace to transport_tracer() which in turn
    // writes a trace message to the qdrouter log.  If trace level
    // logging is enabled on the PROTOCOL module, set PN_TRACE_FRM as
    // the transport trace and also set the transport tracer callback.
    // Note here that if trace level logging is enabled on the DEFAULT
    // module, all modules are logging at trace level too.
    if (qd_log_enabled(server->protocol_log_source, QD_LOG_TRACE)) {
        pn_transport_trace(transport, PN_TRACE_FRM);
        pn_transport_set_tracer(transport, transport_tracer);
    }

    const qd_server_config_t *config = NULL;

    if (conn->listener) {
        // Incoming connection

        config           = &conn->listener->config;
        const char *name = config->host_port;

        pn_transport_set_server(transport);
        connection_set_rhost_port(conn);

        // Policy check is not thread safe
        sys_mutex_lock(server->lock);
        conn->policy_counted = qd_policy_socket_accept(server->qd->policy, conn->rhost);
        sys_mutex_unlock(server->lock);

        if (!conn->policy_counted) {
            pn_transport_close_tail(transport);
            pn_transport_close_head(transport);
            return;
        }

        // Set up SSL

        if (config->ssl_profile) {
            qd_log(log, QD_LOG_TRACE, "Configuring SSL on %s", name);

            if (listener_setup_ssl(conn, config, transport) != QD_ERROR_NONE) {
                connection_fail(conn, QD_AMQP_COND_INTERNAL_ERROR, "%s on %s", qd_error_message(), name);
                return;
            }
        }

        // Set up SASL

        sys_mutex_lock(server->lock);

        pn_sasl_t *sasl = pn_sasl(transport);

        if (server->sasl_config_path) {
            pn_sasl_config_path(sasl, server->sasl_config_path);
        }

        pn_sasl_config_name(sasl, server->sasl_config_name);

        if (config->sasl_mechanisms) {
            pn_sasl_allowed_mechs(sasl, config->sasl_mechanisms);
        }

        if (config->sasl_plugin_config.auth_service) {
            qd_log(log, QD_LOG_INFO, "Enabling remote authentication service %s",
                   config->sasl_plugin_config.auth_service);
            pn_ssl_domain_t *plugin_ssl_domain = NULL;

            if (config->sasl_plugin_config.use_ssl) {
                plugin_ssl_domain = pn_ssl_domain(PN_SSL_MODE_CLIENT);

                if (config->sasl_plugin_config.ssl_certificate_file) {
                    if (pn_ssl_domain_set_credentials(
                            plugin_ssl_domain, config->sasl_plugin_config.ssl_certificate_file,
                            config->sasl_plugin_config.ssl_private_key_file, config->sasl_plugin_config.ssl_password)) {
                        qd_log(log, QD_LOG_ERROR,
                               "Cannot set SSL credentials for authentication "
                               "service");
                    }
                }

                if (config->sasl_plugin_config.ssl_trusted_certificate_db) {
                    if (pn_ssl_domain_set_trusted_ca_db(plugin_ssl_domain,
                                                        config->sasl_plugin_config.ssl_trusted_certificate_db)) {
                        qd_log(log, QD_LOG_ERROR,
                               "Cannot set trusted SSL certificate db for "
                               "authentication service");
                    } else {
                        if (pn_ssl_domain_set_peer_authentication(
                                plugin_ssl_domain, PN_SSL_VERIFY_PEER,
                                config->sasl_plugin_config.ssl_trusted_certificate_db)) {
                            qd_log(log, QD_LOG_ERROR,
                                   "Cannot set SSL peer verification for "
                                   "authentication service");
                        }
                    }
                }

                if (config->sasl_plugin_config.ssl_ciphers) {
                    if (pn_ssl_domain_set_ciphers(plugin_ssl_domain, config->sasl_plugin_config.ssl_ciphers)) {
                        qd_log(log, QD_LOG_ERROR, "Cannot set ciphers for authentication service");
                    }
                }

                if (config->sasl_plugin_config.ssl_protocols) {
                    if (pn_ssl_domain_set_protocols(plugin_ssl_domain, config->sasl_plugin_config.ssl_protocols)) {
                        qd_log(log, QD_LOG_ERROR, "Cannot set protocols for authentication service");
                    }
                }
            }

            qdr_use_remote_authentication_service(transport, config->sasl_plugin_config.auth_service,
                                                  config->sasl_plugin_config.sasl_init_hostname, plugin_ssl_domain,
                                                  server->proactor);
        }

        pn_transport_require_auth(transport, config->requireAuthentication);
        pn_transport_require_encryption(transport, config->requireEncryption);
        pn_sasl_set_allow_insecure_mechs(sasl, config->allowInsecureAuthentication);

        sys_mutex_unlock(server->lock);

        qd_log(log, QD_LOG_INFO, "[C%" PRIu64 "] Accepted connection to %s from %s", conn->connection_id, name,
               conn->rhost_port);
    } else if (conn->connector) {
        // Outgoing connection

        config = &conn->connector->config;

        connection_setup_ssl(conn);
        connection_setup_sasl(conn);

        pn_connection_open(conn->pn_conn);
    } else {
        // No connector and no listener
        connection_fail(conn, QD_AMQP_COND_INTERNAL_ERROR, "Unknown connection");
        return;
    }

    // Common transport configuration.
    pn_transport_set_max_frame(transport, config->max_frame_size);
    pn_transport_set_channel_max(transport, config->max_sessions - 1);
    pn_transport_set_idle_timeout(transport, config->idle_timeout_seconds * 1000);
}

static void server_handle_connection_remote_open(qd_server_t *server, qd_connection_t *conn)
{
    qd_log(server->log_source, QD_LOG_TRACE, "[C%" PRIu64 "] Handling connection remote open", conn->connection_id);

    if (conn->timer) {
        qd_timer_free(conn->timer);
        conn->timer = NULL;
    }

    if (!conn->opened) {
        conn->opened = true;

        if (conn->connector) {
            // Delay reconnect in case there is a recurring error
            conn->connector->delay = 2000;

            qd_failover_item_t *item = qd_connector_get_conn_info(conn->connector);

            if (item) {
                item->retries = 0;
            }
        }
    }
}

void connection_invoke_deferred_calls(qd_connection_t *conn, bool discard);

static void server_handle_connection_wake(qd_server_t *server, qd_connection_t *conn)
{
    qd_log(server->log_source, QD_LOG_TRACE, "[C%" PRIu64 "] Handling connection wake", conn->connection_id);

    connection_invoke_deferred_calls(conn, false);
}

static void server_handle_transport_error(qd_server_t *server, qd_connection_t *conn)
{
    qd_log_source_t *log = server->log_source;
    qd_log(log, QD_LOG_TRACE, "[C%" PRIu64 "] Handling transport error", conn->connection_id);

    pn_transport_t *transport = pn_connection_transport(conn->pn_conn);
    pn_condition_t *condition = pn_transport_condition(transport);

    if (conn->connector) {
        // Outgoing connection

        qd_failover_item_t *item = qd_connector_get_conn_info(conn->connector);

        if (item->retries == 1) {
            // Increment the connection index

            conn->connector->conn_index += 1;

            if (conn->connector->conn_index > DEQ_SIZE(conn->connector->conn_info_list)) {
                conn->connector->conn_index = 1;
            }

            item->retries = 0;
        } else {
            item->retries += 1;
        }

        const qd_server_config_t *config = &conn->connector->config;
        conn->connector->state           = CXTR_STATE_FAILED;
        char conn_msg[300];

        if (pn_condition_is_set(condition)) {
            qd_format_string(conn_msg, 300, "[C%" PRIu64 "] Connection to %s failed: %s %s", conn->connection_id,
                             config->host_port, pn_condition_get_name(condition),
                             pn_condition_get_description(condition));
            strcpy(conn->connector->conn_msg, conn_msg);
            qd_log(log, QD_LOG_INFO, conn_msg);
        } else {
            qd_format_string(conn_msg, 300, "[C%" PRIu64 "] Connection to %s failed", conn->connection_id,
                             config->host_port);
            strcpy(conn->connector->conn_msg, conn_msg);
            qd_log(log, QD_LOG_INFO, conn_msg);
        }
    } else if (conn->listener) {
        // Incoming connection

        if (pn_condition_is_set(condition)) {
            qd_log(log, QD_LOG_INFO, "[C%" PRIu64 "] Connection from %s (to %s) failed: %s %s", conn->connection_id,
                   conn->rhost_port, conn->listener->config.host_port, pn_condition_get_name(condition),
                   pn_condition_get_description(condition));
        }
    } else {
        assert(false);
    }
}

static void server_handle_listener_event(qd_server_t *server, pn_event_t *event)
{
    qd_listener_t *listener = (qd_listener_t *) pn_listener_get_context(pn_event_listener(event));

    assert(listener);
    assert(listener->pn_listener == pn_event_listener(event));

    switch (pn_event_type(event)) {
        case PN_LISTENER_OPEN:
            server_handle_listener_open(server, listener);
            break;

        case PN_LISTENER_ACCEPT:
            server_handle_listener_accept(server, listener);
            break;

        case PN_LISTENER_CLOSE:
            server_handle_listener_close(server, listener);
            break;

        default:
            assert(false);
    }
}

void qd_container_handle_event(qd_container_t *container, pn_event_t *event, pn_connection_t *pn_conn,
                               qd_connection_t *conn);
void qd_conn_event_batch_complete(qd_container_t *container, qd_connection_t *conn, bool conn_closed);

static void server_handle_connection_event(qd_server_t *server, pn_event_t *event)
{
    pn_connection_t *pn_conn = pn_event_connection(event);

    assert(pn_conn);

    if (qdr_is_authentication_service_connection(pn_conn)) {
        qdr_handle_authentication_service_connection_event(event);
        return;
    }

    qd_connection_t *conn = (qd_connection_t *) pn_connection_get_context(pn_conn);

    assert(conn);
    assert(conn->pn_conn == pn_conn);

    switch (pn_event_type(event)) {
        case PN_CONNECTION_INIT:
            server_handle_connection_init(server, conn);
            break;

        case PN_CONNECTION_BOUND:
            server_handle_connection_bound(server, conn);
            break;

        case PN_CONNECTION_REMOTE_OPEN:
            server_handle_connection_remote_open(server, conn);
            break;

        case PN_CONNECTION_WAKE:
            server_handle_connection_wake(server, conn);
            break;

        case PN_TRANSPORT_ERROR:
            server_handle_transport_error(server, conn);
            break;

        default:
            break;
    }

    qd_container_handle_event(server->container, event, pn_conn, conn);

    if (pn_event_type(event) == PN_TRANSPORT_CLOSED) {
        qd_conn_event_batch_complete(server->container, conn, true);
        pn_connection_set_context(pn_conn, NULL);
        qd_connection_free(conn);
    }
}

// Events involving a connection or listener are serialized by the
// proactor so only one event per connection or listener is processed
// at a time
bool qd_server_handle_event(qd_server_t *server, pn_event_t *event)
{
    switch (pn_event_type(event)) {
        case PN_PROACTOR_INTERRUPT:
            // Interrupt the next thread and stop the current thread
            pn_proactor_interrupt(server->proactor);
            return false;

        case PN_PROACTOR_TIMEOUT:
            qd_timer_visit();
            return true;

        case PN_LISTENER_OPEN:
        case PN_LISTENER_ACCEPT:
        case PN_LISTENER_CLOSE:
            server_handle_listener_event(server, event);
            return true;

        default:
            server_handle_connection_event(server, event);
            return true;
    }
}

static void connection_timeout_on_handshake(void *context, bool discard)
{
    if (discard) {
        return;
    }

    qd_connection_t *conn     = (qd_connection_t *) context;
    pn_transport_t *transport = pn_connection_transport(conn->pn_conn);

    pn_transport_close_head(transport);
    connection_fail(conn, QD_AMQP_COND_NOT_ALLOWED, "Timeout waiting for initial handshake");
}

static void connection_startup_timer_handler(void *context)
{
    // This timer fires for a connection if it has not had a
    // REMOTE_OPEN event in a time interval from the CONNECTION_INIT
    // event.  Close down the transport in an IO thread reserved for
    // that connection.

    qd_connection_t *conn = (qd_connection_t *) context;

    qd_timer_free(conn->timer);
    conn->timer = NULL;
    qd_connection_invoke_deferred(conn, connection_timeout_on_handshake, context);
}

// Get the host IP address for the remote end
static void connection_set_rhost_port(qd_connection_t *conn)
{
    pn_transport_t *transport = pn_connection_transport(conn->pn_conn);

    const struct sockaddr *addr = pn_netaddr_sockaddr(pn_transport_remote_addr(transport));
    size_t addrlen              = pn_netaddr_socklen(pn_transport_remote_addr(transport));

    assert(addr);

    if (addrlen) {
        char rport[NI_MAXSERV] = "";
        int err                = getnameinfo(addr, addrlen, conn->rhost, sizeof(conn->rhost), rport, sizeof(rport),
                              NI_NUMERICHOST | NI_NUMERICSERV);

        if (!err) {
            snprintf(conn->rhost_port, sizeof(conn->rhost_port), "%s:%s", conn->rhost, rport);
        }
    }
}

// XXX Maybe rename to connection_setup_ssl_incoming?
static qd_error_t listener_setup_ssl(qd_connection_t *conn, const qd_server_config_t *config, pn_transport_t *transport)
{
    pn_ssl_domain_t *domain = pn_ssl_domain(PN_SSL_MODE_SERVER);

    if (!domain) {
        return qd_error(QD_ERROR_RUNTIME, "No SSL support");
    }

    // Setup my identifying cert
    if (pn_ssl_domain_set_credentials(domain, config->ssl_certificate_file, config->ssl_private_key_file,
                                      config->ssl_password)) {
        pn_ssl_domain_free(domain);
        return qd_error(QD_ERROR_RUNTIME, "Cannot set SSL credentials");
    }

    // for peer authentication:
    if (config->ssl_trusted_certificate_db) {
        if (pn_ssl_domain_set_trusted_ca_db(domain, config->ssl_trusted_certificate_db)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME, "Cannot set trusted SSL CA");
        }
    }

    if (config->ssl_ciphers) {
        if (pn_ssl_domain_set_ciphers(domain, config->ssl_ciphers)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME,
                            "Cannot set ciphers. The ciphers string might be invalid. Use "
                            "openssl ciphers -v <ciphers> to validate");
        }
    }

    if (config->ssl_protocols) {
        if (pn_ssl_domain_set_protocols(domain, config->ssl_protocols)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME,
                            "Cannot set protocols. The protocols string might be invalid. "
                            "This list is a space separated string of the allowed TLS "
                            "protocols (TLSv1 TLSv1.1 TLSv1.2)");
        }
    }

    const char *trusted = config->ssl_trusted_certificate_db;

    if (config->ssl_trusted_certificates) {
        trusted = config->ssl_trusted_certificates;
    }

    // Do we force the peer to send a cert?
    if (config->ssl_require_peer_authentication) {
        if (!trusted || pn_ssl_domain_set_peer_authentication(domain, PN_SSL_VERIFY_PEER, trusted)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME, "Cannot set peer authentication");
        }
    }

    conn->ssl = pn_ssl(transport);

    if (!conn->ssl || pn_ssl_init(conn->ssl, domain, 0)) {
        pn_ssl_domain_free(domain);
        return qd_error(QD_ERROR_RUNTIME, "Cannot initialize SSL");
    }

    // By default, adding SSL to a transport forces encryption to be
    // required, so if it's not, set that here
    if (!config->ssl_required) {
        pn_transport_require_encryption(transport, false);
    }

    pn_ssl_domain_free(domain);

    return QD_ERROR_NONE;
}

// Log the description, set the transport condition (name,
// description), and close the transport tail
static void connection_fail(qd_connection_t *conn, const char *name, const char *description, ...)
{
    va_list ap;
    va_start(ap, description);
    qd_verror(QD_ERROR_RUNTIME, description, ap);
    va_end(ap);

    if (conn->pn_conn) {
        // Normally this is closing the transport, but if not bound,
        // close the connection

        pn_transport_t *transport = pn_connection_transport(conn->pn_conn);
        pn_condition_t *condition = NULL;

        if (transport) {
            condition = pn_transport_condition(transport);
        } else {
            condition = pn_connection_condition(conn->pn_conn);
        }

        if (!pn_condition_is_set(condition)) {
            va_start(ap, description);
            pn_condition_vformat(condition, name, description, ap);
            va_end(ap);
        }

        if (transport) {
            pn_transport_close_tail(transport);
        } else {
            pn_connection_close(conn->pn_conn);
        }
    }
}

static void connection_setup_ssl(qd_connection_t *conn)
{
    qd_log_source_t *log = conn->server->log_source;
    qd_log(log, QD_LOG_TRACE, "[C%" PRIu64 "] Setting up SSL", conn->connection_id);

    qd_connector_t *connector        = conn->connector;
    const qd_server_config_t *config = &connector->config;
    pn_transport_t *transport        = pn_connection_transport(conn->pn_conn);

    if (!config->ssl_profile) {
        return;
    }

    pn_ssl_domain_t *domain = pn_ssl_domain(PN_SSL_MODE_CLIENT);

    if (!domain) {
        qd_error(QD_ERROR_RUNTIME, "SSL domain failed for connection to %s:%s", connector->config.host,
                 connector->config.port);
        return;
    }

    // Set our trusted database for checking the peer's cert
    if (config->ssl_trusted_certificate_db) {
        if (pn_ssl_domain_set_trusted_ca_db(domain, config->ssl_trusted_certificate_db)) {
            qd_log(log, QD_LOG_ERROR, "SSL CA configuration failed for %s:%s", connector->config.host,
                   connector->config.port);
        }
    }

    // Should we force the peer to provide a cert?
    if (config->ssl_require_peer_authentication) {
        const char *trusted =
            (config->ssl_trusted_certificates) ? config->ssl_trusted_certificates : config->ssl_trusted_certificate_db;

        if (pn_ssl_domain_set_peer_authentication(domain, PN_SSL_VERIFY_PEER, trusted)) {
            qd_log(log, QD_LOG_ERROR, "SSL peer auth configuration failed for %s:%s", config->host, config->port);
        }
    }

    // Configure our certificate if the peer requests one
    if (config->ssl_certificate_file) {
        if (pn_ssl_domain_set_credentials(domain, config->ssl_certificate_file, config->ssl_private_key_file,
                                          config->ssl_password)) {
            qd_log(log, QD_LOG_ERROR, "SSL local configuration failed for %s:%s", config->host, config->port);
        }
    }

    if (config->ssl_ciphers) {
        if (pn_ssl_domain_set_ciphers(domain, config->ssl_ciphers)) {
            qd_log(log, QD_LOG_ERROR, "SSL cipher configuration failed for %s:%s", config->host, config->port);
        }
    }

    if (config->ssl_protocols) {
        if (pn_ssl_domain_set_protocols(domain, config->ssl_protocols)) {
            qd_log(log, QD_LOG_ERROR, "Permitted TLS protocols configuration failed %s:%s", config->host, config->port);
        }
    }

    // If ssl is enabled and verify_host_name is true, instruct proton to
    // verify peer name
    if (config->verify_host_name) {
        if (pn_ssl_domain_set_peer_authentication(domain, PN_SSL_VERIFY_PEER_NAME, NULL)) {
            qd_log(log, QD_LOG_ERROR, "SSL peer host name verification failed for %s:%s", config->host, config->port);
        }
    }

    conn->ssl = pn_ssl(transport);
    pn_ssl_init(conn->ssl, domain, 0);
    pn_ssl_domain_free(domain);
}

static void connection_setup_sasl(qd_connection_t *conn)
{
    qd_log(conn->server->log_source, QD_LOG_TRACE, "[C%" PRIu64 "] Setting up SASL", conn->connection_id);

    qd_connector_t *connector        = conn->connector;
    const qd_server_config_t *config = &connector->config;
    pn_transport_t *transport        = pn_connection_transport(conn->pn_conn);

    sys_mutex_lock(connector->server->lock);

    pn_sasl_t *sasl = pn_sasl(transport);

    if (config->sasl_mechanisms) {
        pn_sasl_allowed_mechs(sasl, config->sasl_mechanisms);
    }

    pn_sasl_set_allow_insecure_mechs(sasl, config->allowInsecureAuthentication);

    sys_mutex_unlock(connector->server->lock);
}

void connection_invoke_deferred_calls(qd_connection_t *conn, bool discard)
{
    assert(conn);

    // Lock access to deferred_calls.  Other threads may concurrently
    // add to it.  Invoke the calls outside of the critical section.

    qd_deferred_call_t *dc;

    sys_mutex_lock(conn->deferred_call_lock);

    while ((dc = DEQ_HEAD(conn->deferred_calls))) {
        DEQ_REMOVE_HEAD(conn->deferred_calls);

        sys_mutex_unlock(conn->deferred_call_lock);
        dc->call(dc->context, discard);
        free_qd_deferred_call_t(dc);
        sys_mutex_lock(conn->deferred_call_lock);
    }

    sys_mutex_unlock(conn->deferred_call_lock);
}
