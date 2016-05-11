#ifndef __dispatch_server_h__
#define __dispatch_server_h__ 1
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

#include <qpid/dispatch/dispatch.h>
#include <proton/engine.h>
#include <proton/event.h>

/**@file
 * Control server threads, signals and connections.
 */

/**
 * @defgroup server server
 *
 * Control server threads, starting and stopping the server.
 * @{
 */

/**
 * Thread Start Handler
 *
 * Callback invoked when a new server thread is started.  The callback is
 * invoked on the newly created thread.
 *
 * This handler can be used to set processor affinity or other thread-specific
 * tuning values.
 *
 * @param context The handler context supplied in qd_server_initialize.
 * @param thread_id The integer thread identifier that uniquely identifies this thread.
 */
typedef void (*qd_thread_start_cb_t)(void* context, int thread_id);


/**
 * Deferred callback
 *
 * This type is for calls that are deferred until they can be invoked on
 * a specific connection's thread.
 *
 * @param context An opaque context to be passed back with the call.
 * @param discard If true, the call should be discarded because the connection it
 *        was pending on was deleted.
 */
typedef void (*qd_deferred_t)(void *context, bool discard);


/**
 * Set the optional thread-start handler.
 *
 * This handler is called once on each worker thread at the time the thread is
 * started.  This may be used to set tuning settings like processor affinity,
 * etc.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 * @param start_handler The thread-start handler invoked per thread on thread startup.
 * @param context Opaque context to be passed back in the callback function.
 */
void qd_server_set_start_handler(qd_dispatch_t *qd, qd_thread_start_cb_t start_handler, void *context);


/**
 * Run the server threads until completion - The blocking version.
 *
 * Start the operation of the server, including launching all of the worker
 * threads.  This function does not return until after the server has been
 * stopped.  The thread that calls qd_server_run is used as one of the worker
 * threads.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
void qd_server_run(qd_dispatch_t *qd);


/**
 * Start the server threads and return immediately - The non-blocking version.
 *
 * Start the operation of the server, including launching all of the worker
 * threads.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
void qd_server_start(qd_dispatch_t *qd);


/**
 * Stop the server
 *
 * Stop the server and join all of its worker threads.  This function may be
 * called from any thread.  When this function returns, all of the other
 * server threads have been closed and joined.  The calling thread will be the
 * only running thread in the process.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
void qd_server_stop(qd_dispatch_t *qd);


/**
 * Pause (quiesce) the server.
 *
 * This call blocks until all of the worker threads (except the one calling
 * this function) are finished processing and have been blocked.  When this
 * call returns, the calling thread is the only thread running in the process.
 *
 * If the calling process is *not* one of the server's worker threads, then
 * this function will block all of the worker threads before returning.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
void qd_server_pause(qd_dispatch_t *qd);


/**
 * Resume normal operation of a paused server.
 *
 * This call unblocks all of the worker threads so they can resume normal
 * connection processing.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
void qd_server_resume(qd_dispatch_t *qd);


/**
 * @}
 * @defgroup server_signal server_signal
 *
 * Server Signal Handling
 * 
 * @{
 */


/**
 * Signal Handler
 *
 * Callback for signal handling.  This handler will be invoked on one of the
 * worker threads in an orderly fashion.  This callback is triggered by a call
 * to qd_server_signal.
 *
 * @param context The handler context supplied in qd_server_initialize.
 * @param signum The signal number that was passed into qd_server_signal.
 */
typedef void (*qd_signal_handler_cb_t)(void* context, int signum);


/**
 * Set the signal handler for the server.  The signal handler is invoked
 * cleanly on a worker thread after a call is made to qd_server_signal.  The
 * signal handler is optional and need not be set.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 * @param signal_handler The signal handler called when a registered signal is caught.
 * @param context Opaque context to be passed back in the callback function.
 */
void qd_server_set_signal_handler(qd_dispatch_t *qd, qd_signal_handler_cb_t signal_handler, void *context);


/**
 * Schedule the invocation of the Server's signal handler.
 *
 * This function is safe to call from any context, including an OS signal
 * handler or an Interrupt Service Routine.  It schedules the orderly
 * invocation of the Server's signal handler on one of the worker threads.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 * @param signum The signal number... TODO
 */
void qd_server_signal(qd_dispatch_t *qd, int signum);


/**
 * @}
 * @defgroup connection connection
 *
 * Server AMQP Connection Handling
 *
 * Handling listeners, connectors, connections and events.
 * @{
 */

/**
 * Listener objects represent the desire to accept incoming transport connections.
 */
typedef struct qd_listener_t qd_listener_t;

/**
 * Connector objects represent the desire to create and maintain an outgoing transport connection.
 */
typedef struct qd_connector_t qd_connector_t;

/**
 * Connection objects wrap Proton connection objects.
 */
typedef struct qd_connection_t qd_connection_t;

/**
 * Event type for the connection callback.
 */
typedef enum {
    /// The connection just opened via a listener (inbound).
    /// This event occurs when the AMQP OPEN is received from the client.
    /// The AMQP connection still needs to be locally opened
    QD_CONN_EVENT_LISTENER_OPEN,

    /// The connection just opened via a connector (outbound).
    /// The AMQP connection has been locally and remotely opened.
    QD_CONN_EVENT_CONNECTOR_OPEN,

    /// The connection was closed at the transport level (not cleanly).
    QD_CONN_EVENT_CLOSE,

    /// The connection is writable
    QD_CONN_EVENT_WRITABLE
} qd_conn_event_t;


/**
 * Configuration block for a connector or a listener.
 */
typedef struct qd_server_config_t {
    /**
     * Host name or network address to bind to a listener or use in the connector.
     */
    char *host;

    /**
     * Port name or number to bind to a listener or use in the connector.
     */
    char *port;

    /**
     * Protocol family that the socket will use when binding listener or connector.
     * Possible values are IPv4 or IPv6. If not specified, the protocol family will be automatically determined from the address
     */
    char *protocol_family;

    /**
     * Connection name, used as a reference from other parts of the configuration.
     */
    char *name;

    /**
     * Space-separated list of SASL mechanisms to be accepted for the connection.
     */
    char *sasl_mechanisms;

    /**
     * If appropriate for the mechanism, the username for authentication
     * (connector only)
     */
    char *sasl_username;

    /**
     * If appropriate for the mechanism, the password for authentication
     * (connector only)
     */
    char *sasl_password;

    /**
     * If appropriate for the mechanism, the minimum acceptable security strength factor
     */
    int sasl_minssf;

    /**
     * If appropriate for the mechanism, the maximum acceptable security strength factor
     */
    int sasl_maxssf;

    /**
     * SSL is enabled for this connection iff true.
     */
    bool ssl_enabled;

    /**
     * Iff true, SSL/TLS must be used on the connection.
     */
    bool ssl_required;

    /**
     * Iff true, the client of the connection must authenticate with the server.
     */
    bool requireAuthentication;

    /**
     * Iff true, client authentication _may_ be insecure (i.e. PLAIN over plaintext).
     */
    bool allowInsecureAuthentication;

    /**
     * Iff true, the payload of the connection must be encrypted.
     */
    bool requireEncryption;

    /**
     * Ensures that when initiating a connection (as a client) the host name in the URL to which this connector
     * connects to matches the host name in the digital certificate that the peer sends back as part of the SSL connection
     */
    bool verify_host_name;

    /**
     * If true, strip the inbound qpid dispatch specific message annotations. This only applies to ingress and egress routers.
     * Annotations generated by inter-router messages will be untouched.
     */
    bool strip_inbound_annotations;

    /**
     * If true, strip the outbound qpid dispatch specific message annotations. This only applies to ingress and egress routers.
     * Annotations generated by inter-router messages will be untouched.
     */
    bool strip_outbound_annotations;

    /**
     * The number of deliveries that can be in-flight concurrently for each link within the connection.
     */
    int link_capacity;

    /**
     * Path to the file containing the PEM-formatted public certificate for the local end
     * of the connection.
     */
    char *ssl_certificate_file;

    /**
     * Path to the file containing the PEM-formatted private key for the local end of the
     * connection.
     */
    char *ssl_private_key_file;

    /**
     * Holds the list of component fields of the client certificate from which a unique identifier is constructed.
     * For e.g, this field could have the format of 'cou' indicating that the uid will consist of
     * c - common name concatenated with o - organization-company name concatenated with u - organization unit
     * Allowed components are
     * Allowed values can be any combination of comma separated
     * 'c'( ISO3166 two character country code),
     * 's'(state or province),
     * 'l'(Locality; generally - city),
     * 'o'(Organization - Company Name),
     * 'u'(Organization Unit - typically certificate type or brand),
     * 'n'(CommonName - typically a user name for client certificates) and
     * '1'(sha1 certificate fingerprint, the fingerprint, as displayed in the fingerprints section when looking at a certificate
     *  with say a web browser is the hash of the entire certificate in DER form)
     * '2'(sha256 certificate fingerprint)
     * '5'(sha512 certificate fingerprint)
     */
    char *ssl_uid_format;

    /**
     * Full path to the file that contains the uid to display name mapping.
     */
    char *ssl_display_name_file;

    /**
     * The password used to sign the private key, or NULL if the key is not protected.
     */
    char *ssl_password;

    /**
     * Path to the file containing the PEM-formatted set of certificates of trusted CAs.
     */
    char *ssl_trusted_certificate_db;

    /**
     * Path to an optional file containing the PEM-formatted set of certificates of
     * trusted CAs for a particular connection/listener.  This must be a subset of the
     * set of certificates in the ssl_trusted_certificate_db.  If this is left NULL,
     * the entire set within the db will be used.
     */
    char *ssl_trusted_certificates;

    /**
     * Iff true, require that the peer's certificate be supplied and that it be authentic
     * according to the set of trusted CAs.
     */
    bool ssl_require_peer_authentication;

    /**
     * Allow the connection to be redirected by the peer (via CLOSE->Redirect).  This is
     * meaningful for outgoing (connector) connections only.
     */
    bool allow_redirect;

    /**
     * The specified role of the connection.  This can be used to control the behavior and
     * capabilities of the connections.
     */
    char *role;

    /**
     * If the role is "inter-router", the cost can be set to a number greater than
     * or equal to one.  Inter-router cost is used to influence the routing algorithm
     * such that it prefers lower-cost paths.
     */
    int inter_router_cost;

    /**
     * The maximum size of an AMQP frame in octets.
     */
    uint32_t max_frame_size;

    /**
     * The idle timeout, in seconds.  If the peer sends no data frames in this many seconds, the
     * connection will be automatically closed.
     */
    int idle_timeout_seconds;
} qd_server_config_t;


/**
 * Connection Event Handler
 *
 * Callback invoked when processing is needed on a proton connection.  This
 * callback shall be invoked on one of the server's worker threads.  The
 * server guarantees that no two threads shall be allowed to process a single
 * connection concurrently.  The implementation of this handler may assume
 * that it has exclusive access to the connection and its subservient
 * components (sessions, links, deliveries, etc.).
 *
 * @param handler_context The handler context supplied in qd_server_set_conn_handler.
 * @param conn_context The handler context supplied in qd_server_{connect,listen}.
 * @param event The event/reason for the invocation of the handler.
 * @param conn The connection that requires processing by the handler.
 * @return A value greater than zero if the handler did any proton processing for
 *         the connection.  If no work was done, zero is returned.
 */
typedef int (*qd_conn_handler_cb_t)(void *handler_context, void* conn_context, qd_conn_event_t event, qd_connection_t *conn);

/**
 * Proton Event Handler
 *
 * This callback is invoked when proton events for a connection require
 * processing.
 *
 * @param handler_context The handler context supplied in qd_server_set_conn_handler.
 * @param conn_context The handler context supplied in qd_server_{connect,listen}.
 * @param event The proton event being raised.
 * @param conn The connection associated with this proton event.
 */
typedef int (*qd_pn_event_handler_cb_t)(void *handler_context, void* conn_context, pn_event_t *event, qd_connection_t *conn);


/**
 * Set the connection event handler callback.
 *
 * Set the connection handler callback for the server.  This callback is
 * mandatory and must be set prior to the invocation of qd_server_run.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 * @param conn_handler The handler for processing connection-related events.
 * @param pn_event_handler The handler for proton events.
 * @param handler_context Context data to associate with the handler.
 */
void qd_server_set_conn_handler(qd_dispatch_t *qd, qd_conn_handler_cb_t conn_handler, qd_pn_event_handler_cb_t pn_event_handler, void *handler_context);


/**
 * Set the user context for a connection.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNECTOR}_OPEN
 * @param context User context to be stored with the connection.
 */
void qd_connection_set_context(qd_connection_t *conn, void *context);


/**
 * Get the user context from a connection.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return The user context stored with the connection.
 */
void *qd_connection_get_context(qd_connection_t *conn);


/**
 * Get the configuration context (connector or listener) for this connection.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return The context supplied at the creation of the listener or connector.
 */
void *qd_connection_get_config_context(qd_connection_t *conn);


/**
 * Set the link context for a connection.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @param context Link context to be stored with the connection.
 */
void qd_connection_set_link_context(qd_connection_t *conn, void *context);


/**
 * Get the link context from a connection.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return The link context stored with the connection.
 */
void *qd_connection_get_link_context(qd_connection_t *conn);


/**
 * Sets the user id on the connection.
 * If the sasl mech is EXTERNAL, set the user_id on the connection as the concatenated
 * list of fields specified in the uidFormat field of qdrouter.json
 * If no uidFormat is specified, the user is set to the pn_transport_user
 *
 * @param conn Connection object
 */
void qd_connection_set_user(qd_connection_t *conn);


/**
 * Activate a connection for output.
 *
 * This function is used to request that the server activate the indicated
 * connection.  It is assumed that the connection is one that the caller does
 * not have permission to access (i.e. it may be owned by another thread
 * currently).  An activated connection will, when writable, appear in the
 * internal work list and be invoked for processing by a worker thread.
 *
 * @param conn The connection over which the application wishes to send data
 */
void qd_server_activate(qd_connection_t *conn);


/**
 * Get the wrapped proton-engine connection object.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return The proton connection object.
 */
pn_connection_t *qd_connection_pn(qd_connection_t *conn);


/**
 * Get the direction of establishment for this connection.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return true if connection came through a listener, false if through a connector.
 */
bool qd_connection_inbound(qd_connection_t *conn);


/**
 * Get the event collector for a connection.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return The pn_collector associated with the connection.
 */
pn_collector_t *qd_connection_collector(qd_connection_t *conn);


/**
 * Get the connection id of a connection.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return The connection_id associated with the connection.
 */
uint64_t qd_connection_connection_id(qd_connection_t *conn);


/**
 * Get the configuration that was used in the setup of this connection.
 *
 * @param conn Connection object supplied in QD_CONN_EVENT_{LISTENER,CONNETOR}_OPEN
 * @return A pointer to the server configuration used in the establishment of this connection.
 */
const qd_server_config_t *qd_connection_config(const qd_connection_t *conn);


/**
 * Schedule a call to be invoked on a thread that has ownership of this connection.
 * It will be safe for the callback to perform operations related to this connection.
 *
 * @param conn Connection object
 * @param call The function to be invoked on the connection's thread
 * @param context The context to be passed back in the callback
 */
void qd_connection_invoke_deferred(qd_connection_t *conn, qd_deferred_t call, void *context);


/**
 * Write accessor to the connection's proton-event stall flag.
 * When set no further events are processed on this connection.
 * Used during processing of policy decisions to hold off incoming
 * pipeline of amqp events.
 *
 * @param conn Connection object
 * @param stall Value of stall flag
 */
void qd_connection_set_event_stall(qd_connection_t *conn, bool stall);


/**
 * Create a listener for incoming connections.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 * @param config Pointer to a configuration block for this listener.  This block will be
 *               referenced by the server, not copied.  The referenced record must remain
 *               in-scope for the life of the listener.
 * @param context User context passed back in the connection handler.
 * @return A pointer to the new listener, or NULL in case of failure.
 */
qd_listener_t *qd_server_listen(qd_dispatch_t *qd, const qd_server_config_t *config, void *context);


/**
 * Free the resources associated with a listener.
 *
 * @param li A listener pointer returned by qd_listen.
 */
void qd_server_listener_free(qd_listener_t* li);


/**
 * Close a listener so it will accept no more connections.
 *
 * @param li A listener pointer returned by qd_listen.
 */
void qd_server_listener_close(qd_listener_t* li);


/**
 * Create a connector for an outgoing connection.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 * @param config Pointer to a configuration block for this connector.  This block will be
 *               referenced by the server, not copied.  The referenced record must remain
 *               in-scope for the life of the connector..
 * @param context User context passed back in the connection handler.
 * @return A pointer to the new connector, or NULL in case of failure.
 */
qd_connector_t *qd_server_connect(qd_dispatch_t *qd, const qd_server_config_t *config, void *context);


/**
 * Free the resources associated with a connector.
 *
 * @param ct A connector pointer returned by qd_connect.
 */
void qd_server_connector_free(qd_connector_t* ct);

/**
 * @}
 */

#endif
