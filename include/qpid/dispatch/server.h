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

#include "qpid/dispatch/dispatch.h"
#include "qpid/dispatch/failoverlist.h"

#include <proton/engine.h>
#include <proton/event.h>
#include <proton/ssl.h>

struct qd_container_t;

/**@file
 * Control server threads and connections.
 */

/**
 * @defgroup server server
 *
 * Control server threads, starting and stopping the server.
 * @{
 */

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
 * Run the server threads until completion - The blocking version.
 *
 * Start the operation of the server, including launching all of the worker
 * threads.  Returns when all server threads have exited. The thread that calls
 * qd_server_run is used as one of the worker threads.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
void qd_server_run(qd_dispatch_t *qd);


/**
 * Tells the server to stop but doesn't wait for server to exit.
 * The call to qd_server_run() will exit when all server threads have exited.
 *
 * May be called from any thread or from a signal handler.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */

void qd_server_stop(qd_dispatch_t *qd);

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
    /// The connection was closed at the transport level (not cleanly).
    QD_CONN_EVENT_CLOSE,

    /// The connection is writable
    QD_CONN_EVENT_WRITABLE
} qd_conn_event_t;

typedef uint32_t qd_log_bits;

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
     * Expose simple liveness check.
     */
    bool healthz;

    /**
     * Export metrics.
     */
    bool metrics;

    /**
     * Websockets enabled.
     */
    bool websockets;

    /**
     * Accept HTTP connections, allow WebSocket "amqp" protocol upgrades.
     */
    bool http;

    /**
     * Directory for HTTP content
     */
    char *http_root_dir;

    /**
     * Connection name, used as a reference from other parts of the configuration.
     */
    char *name;

    /**
     * Space-separated list of SASL mechanisms to be accepted for the connection.
     */
    char *sasl_mechanisms;

    /**
     * The name of the sasl plugin config if used.
     */
    char *sasl_plugin;
    /**
     * The config of the sasl plugin config if used.
     */
    struct {
        /**
         * Address, i.e. host:port, of remote authentication service to connect to.
         * (listener only)
         */
        char *auth_service;
        /**
         * Hostname to set on connection (used for SNI in TLS connections).
         */
        char *hostname;
        /**
         * Hostname to set on sasl-init sent to authentication service.
         */
        char *sasl_init_hostname;
        bool use_ssl;
        //ssl config for sasl auth plugin:
        char *ssl_certificate_file;
        char *ssl_private_key_file;
        char *ssl_uid_format;
        char *ssl_profile;
        char *ssl_uid_name_mapping_file;
        char *ssl_password;
        char *ssl_trusted_certificate_db;
        char *ssl_ciphers;
        char *ssl_protocols;
    } sasl_plugin_config;

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
     * The name of the related ssl profile.
     */
    char *ssl_profile;

    /**
     * Full path to the file that contains the uid to display name mapping.
     */
    char *ssl_uid_name_mapping_file;

    /**
     * The password used to sign the private key, or NULL if the key is not protected.
     */
    char *ssl_password;

    /**
     * Path to the file containing the PEM-formatted set of certificates of trusted CAs.
     */
    char *ssl_trusted_certificate_db;

    /**
     * Iff true, require that the peer's certificate be supplied and that it be authentic
     * according to the set of trusted CAs.
     */
    bool ssl_require_peer_authentication;

    /**
     * Specifies the enabled ciphers so the SSL Ciphers can be hardened.
     */
    char *ssl_ciphers;

    /**
     * This list is a space separated string of the allowed TLS protocols. The current possibilities are TLSv1 TLSv1.1 TLSv1.2.
     * For example, if you want to permit only TLSv.1.1 and TLSv1.2, your value for the protocols would be TLSv1.1 TLSv1.2. If this attribute is not set, then all the TLS protocols are allowed.
     */
    char *ssl_protocols;

    /**
     * Allow the connection to be redirected by the peer (via CLOSE->Redirect).  This is
     * meaningful for outgoing (connector) connections only.
     */
    bool allow_redirect;

    /**
     * MultiTenancy support.  If true, the vhost is used to define the address space of
     * addresses used over this connection.
     */
    bool multi_tenant;

    /**
     * Optional vhost to use for policy lookup.  If non-null, this overrides the vhost supplied
     * in the OPEN from the peer only for the purpose of identifying the policy to enforce.
     */
    char *policy_vhost;

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
     * The max_sessions value is the number of sessions allowed on the Connection. 
     */
    uint32_t max_sessions;

    /**
     * The incoming capacity value is calculated to be (sessionMaxFrames * maxFrameSize).
     * In a round about way the calculation forces the AMQP Begin/incoming-capacity value
     * to equal the specified sessionMaxFrames value measured in units of transfer frames.
     * This calculation is done to satisfy proton pn_session_set_incoming_capacity().
     */
    size_t incoming_capacity;

    /**
     * The idle timeout, in seconds.  If the peer sends no data frames in this many seconds, the
     * connection will be automatically closed.
     */
    int idle_timeout_seconds;

    /**
     * The timeout, in seconds, for the initial connection handshake.  If a connection is established
     * inbound (via a listener) and the timeout expires before the OPEN frame arrives, the connection
     * shall be closed.
     */
    int initial_handshake_timeout_seconds;

    /**
     *  Holds comma separated list that indicates which components of the message should be logged.
     *  Defaults to 'none' (log nothing). If you want all properties and application properties of the message logged use 'all'.
     *  Specific components of the message can be logged by indicating the components via a comma separated list.
     *  The components are
     *  message-id
     *   user-id
     *   to
     *   subject
     *   reply-to
     *   correlation-id
     *   content-type
     *   content-encoding
     *   absolute-expiry-time
     *   creation-time
     *   group-id
     *   group-sequence
     *   reply-to-group-id
     *   app-properties.
     */
    char *log_message;

    /**
     * A bitwise representation of which log components have been enabled in the log_message field.
     */
    qd_log_bits log_bits;

    /**
     * Configured failover list
     */
    qd_failover_list_t *failover_list;

    /**
     * Extra connection properties to include in the outgoing Open frame.  Stored as a map.
     */
    pn_data_t *conn_props;

    /**
     * @name These fields are not primary configuration, they are computed.
     * @{
     */

    /**
     * Concatenated connect/listen address "host:port"
     */
    char *host_port;

    /**
     * @}
     */
} qd_server_config_t;


/**
 * Set the container, must be set prior to the invocation of qd_server_run.
 */
void qd_server_set_container(qd_dispatch_t *qd, struct qd_container_t *container);

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
 * Schedule a call to be invoked on a thread that has ownership of this connection
 * when it will be safe for the callback to perform operations related to this connection.
 * A qd_deferred_call_t object has been allocated before hand to avoid taking
 * the ENTITY_CACHE lock.
 *
 * @param conn Connection object
 * @param call The function to be invoked on the connection's thread
 * @param context The context to be passed back in the callback
 * @param dct Pointer to preallocated qd_deferred_call_t object
 */
void qd_connection_invoke_deferred_impl(qd_connection_t *conn, qd_deferred_t call, void *context, void *dct);


/**
 * Allocate a qd_deferred_call_t object
 */
void *qd_connection_new_qd_deferred_call_t();


/**
 * Deallocate a qd_deferred_call_t object
 *
 * @param dct Pointer to preallocated qd_deferred_call_t object
 */
void qd_connection_free_qd_deferred_call_t(void *dct);



/**
 * Listen for incoming connections, return true if listening succeeded.
 */
bool qd_listener_listen(qd_listener_t *l);

/**
 * Initiate an outgoing connection. Returns true if successful.
 */
bool qd_connector_connect(qd_connector_t *ct);

/**
 * Store address of display name service py object for C code use
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 * @param display_name_service address of python object
 */
qd_error_t qd_register_display_name_service(qd_dispatch_t *qd, void *display_name_service);

/**
 * Get the name of the connection, based on its IP address.
 */
const char* qd_connection_name(const qd_connection_t *c);


/**
 * Get the remote host IP address of the connection.
 */
const char* qd_connection_remote_ip(const qd_connection_t *c);

bool qd_connection_strip_annotations_in(const qd_connection_t *c);

void qd_connection_wake(qd_connection_t *ctx);

uint64_t qd_connection_max_message_size(const qd_connection_t *c);

/**
 * @}
 */

#endif
