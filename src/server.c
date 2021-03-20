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

#include "python_private.h"             // must be first!
#include "dispatch_private.h"
#include <qpid/dispatch/python_embedded.h>

#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/amqp.h>
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/failoverlist.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/platform.h>
#include <qpid/dispatch/proton_utils.h>

#include <proton/event.h>
#include <proton/listener.h>
#include <proton/netaddr.h>
#include <proton/proactor.h>
#include <proton/raw_connection.h>
#include <proton/sasl.h>


#include "entity.h"
#include "entity_cache.h"
#include "dispatch_private.h"
#include "policy.h"
#include "server_private.h"
#include "timer_private.h"
#include "config.h"
#include "remote_sasl.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

struct qd_server_t {
    qd_dispatch_t            *qd;
    const int                 thread_count; /* Immutable */
    const char               *container_name;
    const char               *sasl_config_path;
    const char               *sasl_config_name;
    pn_proactor_t            *proactor;
    qd_container_t           *container;
    qd_log_source_t          *log_source;
    qd_log_source_t          *protocol_log_source; // Log source for the PROTOCOL module
    void                     *start_context;
    sys_cond_t               *cond;
    sys_mutex_t              *lock;
    qd_connection_list_t      conn_list;
    int                       pause_requests;
    int                       threads_paused;
    int                       pause_next_sequence;
    int                       pause_now_serving;
    uint64_t                  next_connection_id;
    void                     *py_displayname_obj;
    qd_http_server_t         *http;
    sys_mutex_t              *conn_activation_lock;
};

#define HEARTBEAT_INTERVAL 1000

ALLOC_DEFINE(qd_listener_t);
ALLOC_DEFINE(qd_connector_t);
ALLOC_DEFINE(qd_deferred_call_t);
ALLOC_DEFINE(qd_connection_t);

const char *MECH_EXTERNAL = "EXTERNAL";

//Allowed uidFormat fields.
const char CERT_COUNTRY_CODE = 'c';
const char CERT_STATE = 's';
const char CERT_CITY_LOCALITY = 'l';
const char CERT_ORGANIZATION_NAME = 'o';
const char CERT_ORGANIZATION_UNIT = 'u';
const char CERT_COMMON_NAME = 'n';
const char CERT_FINGERPRINT_SHA1 = '1';
const char CERT_FINGERPRINT_SHA256 = '2';
const char CERT_FINGERPRINT_SHA512 = '5';
char *COMPONENT_SEPARATOR = ";";

static const int BACKLOG = 50;  /* Listening backlog */

static bool setup_ssl_sasl_and_open(qd_connection_t *ctx); // true if ssl, sasl, and open succeeded
static qd_failover_item_t *qd_connector_get_conn_info(qd_connector_t *ct);

/**
 * This function is set as the pn_transport->tracer and is invoked when proton tries to write the log message to pn_transport->tracer
 */
void transport_tracer(pn_transport_t *transport, const char *message)
{
    qd_connection_t *ctx = (qd_connection_t*) pn_transport_get_context(transport);
    if (ctx) {
        // The PROTOCOL module is used exclusively for logging protocol related tracing. The protocol could be AMQP, HTTP, TCP etc.
        qd_log(ctx->server->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"]:%s", ctx->connection_id, message);
    }
}

void connection_transport_tracer(pn_transport_t *transport, const char *message)
{
    qd_connection_t *ctx = (qd_connection_t*) pn_transport_get_context(transport);
    if (ctx) {
        // Unconditionally write the log at TRACE level to the log file.
        qd_log_impl_v1(ctx->server->protocol_log_source, QD_LOG_TRACE,  __FILE__, __LINE__, "[C%"PRIu64"]:%s", ctx->connection_id, message);
    }
}

/**
 * Save displayNameService object instance and ImportModule address
 * Called with qd_python_lock held
 */
qd_error_t qd_register_display_name_service(qd_dispatch_t *qd, void *displaynameservice)
{
    if (displaynameservice) {
        qd->server->py_displayname_obj = displaynameservice;
        Py_XINCREF((PyObject *)qd->server->py_displayname_obj);
        return QD_ERROR_NONE;
    }
    else {
        return qd_error(QD_ERROR_VALUE, "displaynameservice is not set");
    }
}


/**
 * Returns a char pointer to a user id which is constructed from components specified in the config->ssl_uid_format.
 * Parses through each component and builds a semi-colon delimited string which is returned as the user id.
 */
static const char *transport_get_user(qd_connection_t *conn, pn_transport_t *tport)
{
    const qd_server_config_t *config =
            conn->connector ? &conn->connector->config : &conn->listener->config;

    if (config->ssl_uid_format) {
        // The ssl_uid_format length cannot be greater that 7
        assert(strlen(config->ssl_uid_format) < 8);

        //
        // The tokens in the uidFormat strings are delimited by comma. Load the individual components of the uidFormat
        // into the components[] array. The maximum number of components that are allowed are 7 namely, c, s, l, o, u, n, (1 or 2 or 5)
        //
        char components[8];

        //The strcpy() function copies the string pointed to by src, including the terminating null byte ('\0'), to the buffer pointed to by dest.
        strncpy(components, config->ssl_uid_format, 7);

        const char *country_code = 0;
        const char *state = 0;
        const char *locality_city = 0;
        const char *organization = 0;
        const char *org_unit = 0;
        const char *common_name = 0;
        //
        // SHA1 is 20 octets (40 hex characters); SHA256 is 32 octets (64 hex characters).
        // SHA512 is 64 octets (128 hex characters)
        //
        char fingerprint[129] = "\0";

        int uid_length = 0;
        int semi_colon_count = -1;

        int component_count = strlen(components);

        for (int x = 0; x < component_count ; x++) {
            // accumulate the length into uid_length on each pass so we definitively know the number of octets to malloc.
            if (components[x] == CERT_COUNTRY_CODE) {
                country_code =  pn_ssl_get_remote_subject_subfield(pn_ssl(tport), PN_SSL_CERT_SUBJECT_COUNTRY_NAME);
                if (country_code) {
                    uid_length += strlen((const char *)country_code);
                    semi_colon_count++;
                }
            }
            else if (components[x] == CERT_STATE) {
                state =  pn_ssl_get_remote_subject_subfield(pn_ssl(tport), PN_SSL_CERT_SUBJECT_STATE_OR_PROVINCE);
                if (state) {
                    uid_length += strlen((const char *)state);
                    semi_colon_count++;
                }
            }
            else if (components[x] == CERT_CITY_LOCALITY) {
                locality_city =  pn_ssl_get_remote_subject_subfield(pn_ssl(tport), PN_SSL_CERT_SUBJECT_CITY_OR_LOCALITY);
                if (locality_city) {
                    uid_length += strlen((const char *)locality_city);
                    semi_colon_count++;
                }
            }
            else if (components[x] == CERT_ORGANIZATION_NAME) {
                organization =  pn_ssl_get_remote_subject_subfield(pn_ssl(tport), PN_SSL_CERT_SUBJECT_ORGANIZATION_NAME);
                if(organization) {
                    uid_length += strlen((const char *)organization);
                    semi_colon_count++;
                }
            }
            else if (components[x] == CERT_ORGANIZATION_UNIT) {
                org_unit =  pn_ssl_get_remote_subject_subfield(pn_ssl(tport), PN_SSL_CERT_SUBJECT_ORGANIZATION_UNIT);
                if(org_unit) {
                    uid_length += strlen((const char *)org_unit);
                    semi_colon_count++;
                }
            }
            else if (components[x] == CERT_COMMON_NAME) {
                common_name =  pn_ssl_get_remote_subject_subfield(pn_ssl(tport), PN_SSL_CERT_SUBJECT_COMMON_NAME);
                if(common_name) {
                    uid_length += strlen((const char *)common_name);
                    semi_colon_count++;
                }
            }
            else if (components[x] == CERT_FINGERPRINT_SHA1 || components[x] == CERT_FINGERPRINT_SHA256 || components[x] == CERT_FINGERPRINT_SHA512) {
                // Allocate the memory for message digest
                int out = 0;

                int fingerprint_length = 0;
                if(components[x] == CERT_FINGERPRINT_SHA1) {
                    fingerprint_length = 40;
                    out = pn_ssl_get_cert_fingerprint(pn_ssl(tport), fingerprint, fingerprint_length + 1, PN_SSL_SHA1);
                }
                else if (components[x] == CERT_FINGERPRINT_SHA256) {
                    fingerprint_length = 64;
                    out = pn_ssl_get_cert_fingerprint(pn_ssl(tport), fingerprint, fingerprint_length + 1, PN_SSL_SHA256);
                }
                else if (components[x] == CERT_FINGERPRINT_SHA512) {
                    fingerprint_length = 128;
                    out = pn_ssl_get_cert_fingerprint(pn_ssl(tport), fingerprint, fingerprint_length + 1, PN_SSL_SHA512);
                }

                (void) out;  // avoid 'out unused' compiler warnings if NDEBUG undef'ed
                assert (out != PN_ERR);

                uid_length += fingerprint_length;
                semi_colon_count++;
            }
            else {
                // This is an unrecognized component. log a critical error
                qd_log(conn->server->log_source, QD_LOG_CRITICAL, "[C%"PRIu64"] Unrecognized component '%c' in uidFormat ", conn->connection_id, components[x]);
                return 0;
            }
        }

        if(uid_length > 0) {
            char *user_id = malloc((uid_length + semi_colon_count + 1) * sizeof(char)); // the +1 is for the '\0' character
            //
            // We have allocated memory for user_id. We are responsible for freeing this memory. Set conn->free_user_id
            // to true so that we know that we have to free the user_id
            //
            conn->free_user_id = true;
            memset(user_id, 0, uid_length + semi_colon_count + 1);

            // The components in the user id string must appear in the same order as it appears in the component string. that is
            // why we have this loop
            for (int x=0; x < component_count ; x++) {
                if (components[x] == CERT_COUNTRY_CODE) {
                    if (country_code) {
                        if(*user_id != '\0')
                            strcat(user_id, COMPONENT_SEPARATOR);
                        strcat(user_id, (char *) country_code);
                    }
                }
                else if (components[x] == CERT_STATE) {
                    if (state) {
                        if(*user_id != '\0')
                            strcat(user_id, COMPONENT_SEPARATOR);
                        strcat(user_id, (char *) state);
                    }
                }
                else if (components[x] == CERT_CITY_LOCALITY) {
                    if (locality_city) {
                        if(*user_id != '\0')
                            strcat(user_id, COMPONENT_SEPARATOR);
                        strcat(user_id, (char *) locality_city);
                    }
                }
                else if (components[x] == CERT_ORGANIZATION_NAME) {
                    if (organization) {
                        if(*user_id != '\0')
                            strcat(user_id, COMPONENT_SEPARATOR);
                        strcat(user_id, (char *) organization);
                    }
                }
                else if (components[x] == CERT_ORGANIZATION_UNIT) {
                    if (org_unit) {
                        if(*user_id != '\0')
                            strcat(user_id, COMPONENT_SEPARATOR);
                        strcat(user_id, (char *) org_unit);
                    }
                }
                else if (components[x] == CERT_COMMON_NAME) {
                    if (common_name) {
                        if(*user_id != '\0')
                            strcat(user_id, COMPONENT_SEPARATOR);
                        strcat(user_id, (char *) common_name);
                    }
                }
                else if (components[x] == CERT_FINGERPRINT_SHA1 || components[x] == CERT_FINGERPRINT_SHA256 || components[x] == CERT_FINGERPRINT_SHA512) {
                    if (strlen((char *) fingerprint) > 0) {
                        if(*user_id != '\0')
                            strcat(user_id, COMPONENT_SEPARATOR);
                        strcat(user_id, (char *) fingerprint);
                    }
                }
            }
            if (config->ssl_uid_name_mapping_file) {
                // Translate extracted id into display name
                qd_python_lock_state_t lock_state = qd_python_lock();
                PyObject *result = PyObject_CallMethod((PyObject *)conn->server->py_displayname_obj, "query", "(ss)", config->ssl_profile, user_id );
                if (result) {
                    free(user_id);
                    user_id = py_string_2_c(result);
                    Py_XDECREF(result);
                } else {
                    qd_log(conn->server->log_source, QD_LOG_DEBUG, "[C%"PRIu64"] Internal: failed to read displaynameservice query result", conn->connection_id);
                }
                qd_python_unlock(lock_state);
            }
            qd_log(conn->server->log_source, QD_LOG_DEBUG, "User id is '%s' ", user_id);
            return user_id;
        }
    }
    else //config->ssl_uid_format not specified, just return the username provided by the proton transport.
        return pn_transport_get_user(tport);

    return 0;
}


void qd_connection_set_user(qd_connection_t *conn)
{
    pn_transport_t *tport = pn_connection_transport(conn->pn_conn);
    pn_sasl_t      *sasl  = pn_sasl(tport);
    if (sasl) {
        const char *mech = pn_sasl_get_mech(sasl);
        conn->user_id = pn_transport_get_user(tport);
        // We want to set the user name only if it is not already set and the selected sasl mechanism is EXTERNAL
        if (mech && strcmp(mech, MECH_EXTERNAL) == 0) {
            const char *user_id = transport_get_user(conn, tport);
            if (user_id)
                conn->user_id = user_id;
        }
    }
}


qd_error_t qd_entity_refresh_sslProfile(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}

qd_error_t qd_entity_refresh_authServicePlugin(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}


static qd_error_t listener_setup_ssl(qd_connection_t *ctx, const qd_server_config_t *config, pn_transport_t *tport)
{
    pn_ssl_domain_t *domain = pn_ssl_domain(PN_SSL_MODE_SERVER);
    if (!domain) return qd_error(QD_ERROR_RUNTIME, "No SSL support");

    // setup my identifying cert:
    if (pn_ssl_domain_set_credentials(domain,
                                      config->ssl_certificate_file,
                                      config->ssl_private_key_file,
                                      config->ssl_password)) {
        pn_ssl_domain_free(domain);
        return qd_error(QD_ERROR_RUNTIME, "Cannot set SSL credentials");
    }

    // for peer authentication:
    if (config->ssl_trusted_certificate_db) {
        if (pn_ssl_domain_set_trusted_ca_db(domain, config->ssl_trusted_certificate_db)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME, "Cannot set trusted SSL CA" );
        }
    }

    if (config->ssl_ciphers) {
        if (pn_ssl_domain_set_ciphers(domain, config->ssl_ciphers)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME, "Cannot set ciphers. The ciphers string might be invalid. Use openssl ciphers -v <ciphers> to validate");
        }
    }

    if (config->ssl_protocols) {
        if (pn_ssl_domain_set_protocols(domain, config->ssl_protocols)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME, "Cannot set protocols. The protocols string might be invalid. This list is a space separated string of the allowed TLS protocols (TLSv1 TLSv1.1 TLSv1.2)");
        }
    }

    const char *trusted = config->ssl_trusted_certificate_db;

    // do we force the peer to send a cert?
    if (config->ssl_require_peer_authentication) {
        if (!trusted || pn_ssl_domain_set_peer_authentication(domain, PN_SSL_VERIFY_PEER, trusted)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME, "Cannot set peer authentication");
        }
    }

    ctx->ssl = pn_ssl(tport);
    if (!ctx->ssl || pn_ssl_init(ctx->ssl, domain, 0)) {
        pn_ssl_domain_free(domain);
        return qd_error(QD_ERROR_RUNTIME, "Cannot initialize SSL");
    }

    // By default adding ssl to a transport forces encryption to be required, so if it's not set that here
    if (!config->ssl_required) {
        pn_transport_require_encryption(tport, false);
    }

    pn_ssl_domain_free(domain);
    return QD_ERROR_NONE;
}


static void decorate_connection(qd_server_t *qd_server, pn_connection_t *conn, const qd_server_config_t *config)
{
    //
    // Set the container name
    //
    pn_connection_set_container(conn, qd_server->container_name);

    //
    // Advertise our container capabilities.
    //
    {
        // offered: extension capabilities this router supports
        pn_data_t *ocaps = pn_connection_offered_capabilities(conn);
        pn_data_put_array(ocaps, false, PN_SYMBOL);
        pn_data_enter(ocaps);
        pn_data_put_symbol(ocaps, pn_bytes(strlen(QD_CAPABILITY_ANONYMOUS_RELAY), (char*) QD_CAPABILITY_ANONYMOUS_RELAY));
        pn_data_put_symbol(ocaps, pn_bytes(strlen(QD_CAPABILITY_STREAMING_LINKS), (char*) QD_CAPABILITY_STREAMING_LINKS));
        pn_data_exit(ocaps);

        // The desired-capability list defines which extension capabilities the
        // sender MAY use if the receiver offers them (i.e., they are in the
        // offered-capabilities list received by the sender of the
        // desired-capabilities). The sender MUST NOT attempt to use any
        // capabilities it did not declare in the desired-capabilities
        // field.
        ocaps = pn_connection_desired_capabilities(conn);
        pn_data_put_array(ocaps, false, PN_SYMBOL);
        pn_data_enter(ocaps);
        pn_data_put_symbol(ocaps, pn_bytes(strlen(QD_CAPABILITY_ANONYMOUS_RELAY), (char*) QD_CAPABILITY_ANONYMOUS_RELAY));
        pn_data_put_symbol(ocaps, pn_bytes(strlen(QD_CAPABILITY_STREAMING_LINKS), (char*) QD_CAPABILITY_STREAMING_LINKS));
        pn_data_exit(ocaps);
    }

    //
    // Create the connection properties map
    //
    pn_data_put_map(pn_connection_properties(conn));
    pn_data_enter(pn_connection_properties(conn));

    pn_data_put_symbol(pn_connection_properties(conn),
                       pn_bytes(strlen(QD_CONNECTION_PROPERTY_PRODUCT_KEY), QD_CONNECTION_PROPERTY_PRODUCT_KEY));
    pn_data_put_string(pn_connection_properties(conn),
                       pn_bytes(strlen(QD_CONNECTION_PROPERTY_PRODUCT_VALUE), QD_CONNECTION_PROPERTY_PRODUCT_VALUE));

    pn_data_put_symbol(pn_connection_properties(conn),
                       pn_bytes(strlen(QD_CONNECTION_PROPERTY_VERSION_KEY), QD_CONNECTION_PROPERTY_VERSION_KEY));
    pn_data_put_string(pn_connection_properties(conn),
                       pn_bytes(strlen(QPID_DISPATCH_VERSION), QPID_DISPATCH_VERSION));

    pn_data_put_symbol(pn_connection_properties(conn),
                       pn_bytes(strlen(QD_CONNECTION_PROPERTY_CONN_ID), QD_CONNECTION_PROPERTY_CONN_ID));
    qd_connection_t *qd_conn = pn_connection_get_context(conn);
    pn_data_put_int(pn_connection_properties(conn), qd_conn->connection_id);

    if (config && config->inter_router_cost > 1) {
        pn_data_put_symbol(pn_connection_properties(conn),
                           pn_bytes(strlen(QD_CONNECTION_PROPERTY_COST_KEY), QD_CONNECTION_PROPERTY_COST_KEY));
        pn_data_put_int(pn_connection_properties(conn), config->inter_router_cost);
    }

    if (config) {
        qd_failover_list_t *fol = config->failover_list;
        if (fol) {
            pn_data_put_symbol(pn_connection_properties(conn),
                               pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY), QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY));
            pn_data_put_list(pn_connection_properties(conn));
            pn_data_enter(pn_connection_properties(conn));
            int fol_count = qd_failover_list_size(fol);
            for (int i = 0; i < fol_count; i++) {
                pn_data_put_map(pn_connection_properties(conn));
                pn_data_enter(pn_connection_properties(conn));
                pn_data_put_symbol(pn_connection_properties(conn),
                                   pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_NETHOST_KEY), QD_CONNECTION_PROPERTY_FAILOVER_NETHOST_KEY));
                pn_data_put_string(pn_connection_properties(conn),
                                   pn_bytes(strlen(qd_failover_list_host(fol, i)), qd_failover_list_host(fol, i)));

                pn_data_put_symbol(pn_connection_properties(conn),
                                   pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_PORT_KEY), QD_CONNECTION_PROPERTY_FAILOVER_PORT_KEY));
                pn_data_put_string(pn_connection_properties(conn),
                                   pn_bytes(strlen(qd_failover_list_port(fol, i)), qd_failover_list_port(fol, i)));

                if (qd_failover_list_scheme(fol, i)) {
                    pn_data_put_symbol(pn_connection_properties(conn),
                                       pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_SCHEME_KEY), QD_CONNECTION_PROPERTY_FAILOVER_SCHEME_KEY));
                    pn_data_put_string(pn_connection_properties(conn),
                                       pn_bytes(strlen(qd_failover_list_scheme(fol, i)), qd_failover_list_scheme(fol, i)));
                }

                if (qd_failover_list_hostname(fol, i)) {
                    pn_data_put_symbol(pn_connection_properties(conn),
                                       pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_HOSTNAME_KEY), QD_CONNECTION_PROPERTY_FAILOVER_HOSTNAME_KEY));
                    pn_data_put_string(pn_connection_properties(conn),
                                       pn_bytes(strlen(qd_failover_list_hostname(fol, i)), qd_failover_list_hostname(fol, i)));
                }
                pn_data_exit(pn_connection_properties(conn));
            }
            pn_data_exit(pn_connection_properties(conn));
        }

        // Append any user-configured properties. conn_props is a pn_data_t PN_MAP
        // type. Append the map elements - not the map itself!
        //
        if (config->conn_props) {
            pn_data_t *outp = pn_connection_properties(conn);

            pn_data_rewind(config->conn_props);
            pn_data_next(config->conn_props);
            assert(pn_data_type(config->conn_props) == PN_MAP);
            const size_t count = pn_data_get_map(config->conn_props);
            pn_data_enter(config->conn_props);
            for (size_t i = 0; i < count / 2; ++i) {
                // key: the key must be of type Symbol.  The python agent has
                // validated the keys as ASCII strings, but the JSON converter does
                // not provide a Symbol type so all the keys in conn_props are
                // PN_STRING.
                pn_data_next(config->conn_props);
                assert(pn_data_type(config->conn_props) == PN_STRING);
                pn_data_put_symbol(outp, pn_data_get_string(config->conn_props));
                // put value
                pn_data_next(config->conn_props);
                qdpn_data_insert(outp, config->conn_props);
            }
        }
    }

    pn_data_exit(pn_connection_properties(conn));
}

/* Wake function for proactor-manaed connections */
static void connection_wake(qd_connection_t *ctx) {
    if (ctx->pn_conn) pn_connection_wake(ctx->pn_conn);
}

/* Construct a new qd_connection. Thread safe. */
qd_connection_t *qd_server_connection(qd_server_t *server, qd_server_config_t *config)
{
    qd_connection_t *ctx = new_qd_connection_t();
    if (!ctx) return NULL;
    ZERO(ctx);
    ctx->pn_conn       = pn_connection();
    ctx->deferred_call_lock = sys_mutex();
    ctx->role = strdup(config->role);
    if (!ctx->pn_conn || !ctx->deferred_call_lock || !ctx->role) {
        if (ctx->pn_conn) pn_connection_free(ctx->pn_conn);
        if (ctx->deferred_call_lock) sys_mutex_free(ctx->deferred_call_lock);
        free(ctx->role);
        free(ctx);
        return NULL;
    }
    ctx->server = server;
    ctx->wake = connection_wake; /* Default, over-ridden for HTTP connections */
    pn_connection_set_context(ctx->pn_conn, ctx);
    DEQ_ITEM_INIT(ctx);
    DEQ_INIT(ctx->deferred_calls);
    DEQ_INIT(ctx->free_link_session_list);
    sys_mutex_lock(server->lock);
    ctx->connection_id = server->next_connection_id++;
    DEQ_INSERT_TAIL(server->conn_list, ctx);
    sys_mutex_unlock(server->lock);
    decorate_connection(ctx->server, ctx->pn_conn, config);
    return ctx;
}


static void on_accept(pn_event_t *e, qd_listener_t *listener)
{
    assert(pn_event_type(e) == PN_LISTENER_ACCEPT);
    pn_listener_t *pn_listener = pn_event_listener(e);
    qd_connection_t *ctx = qd_server_connection(listener->server, &listener->config);
    if (!ctx) {
        qd_log(listener->server->log_source, QD_LOG_CRITICAL,
               "Allocation failure during accept to %s", listener->config.host_port);
        return;
    }
    ctx->listener = listener;
    qd_log(listener->server->log_source, QD_LOG_TRACE,
           "[C%"PRIu64"]: Accepting incoming connection to '%s'",
           ctx->connection_id, ctx->listener->config.host_port);
    /* Asynchronous accept, configure the transport on PN_CONNECTION_BOUND */
    pn_listener_accept(pn_listener, ctx->pn_conn);
 }


/* Log the description, set the transport condition (name, description) close the transport tail. */
void connect_fail(qd_connection_t *ctx, const char *name, const char *description, ...)
{
    va_list ap;
    va_start(ap, description);
    qd_verror(QD_ERROR_RUNTIME, description, ap);
    va_end(ap);
    if (ctx->pn_conn) {
        pn_transport_t *t = pn_connection_transport(ctx->pn_conn);
        /* Normally this is closing the transport but if not bound close the connection. */
        pn_condition_t *cond = t ? pn_transport_condition(t) : pn_connection_condition(ctx->pn_conn);
        if (cond && !pn_condition_is_set(cond)) {
            va_start(ap, description);
            pn_condition_vformat(cond, name, description, ap);
            va_end(ap);
        }
        if (t) {
            pn_transport_close_tail(t);
        } else {
            pn_connection_close(ctx->pn_conn);
        }
    }
}


/* Get the host IP address for the remote end */
static void set_rhost_port(qd_connection_t *ctx) {
    pn_transport_t *tport  = pn_connection_transport(ctx->pn_conn);
    const struct sockaddr* sa = pn_netaddr_sockaddr(pn_transport_remote_addr(tport));
    size_t salen = pn_netaddr_socklen(pn_transport_remote_addr(tport));
    if (sa && salen) {
        char rport[NI_MAXSERV] = "";
        int err = getnameinfo(sa, salen,
                              ctx->rhost, sizeof(ctx->rhost), rport, sizeof(rport),
                              NI_NUMERICHOST | NI_NUMERICSERV);
        if (!err) {
            snprintf(ctx->rhost_port, sizeof(ctx->rhost_port), "%s:%s", ctx->rhost, rport);
        }
    }
}


/* Configure the transport once it is bound to the connection */
static void on_connection_bound(qd_server_t *server, pn_event_t *e) {
    pn_connection_t *pn_conn = pn_event_connection(e);
    qd_connection_t *ctx = pn_connection_get_context(pn_conn);
    pn_transport_t *tport  = pn_connection_transport(pn_conn);
    pn_transport_set_context(tport, ctx); /* for transport_tracer */

    //
    // Proton pushes out its trace to transport_tracer() which in turn writes a trace
    // message to the qdrouter log
    // If trace level logging is enabled on the PROTOCOL module, set PN_TRACE_FRM as the transport trace
    // and also set the transport tracer callback.
    // Note here that if trace level logging is enabled on the DEFAULT module, all modules are logging at trace level too.
    //
    if (qd_log_enabled(ctx->server->protocol_log_source, QD_LOG_TRACE)) {
        pn_transport_trace(tport, PN_TRACE_FRM);
        pn_transport_set_tracer(tport, transport_tracer);
    }

    const qd_server_config_t *config = NULL;
    if (ctx->listener) {        /* Accepting an incoming connection */
        config = &ctx->listener->config;
        const char *name = config->host_port;
        pn_transport_set_server(tport);
        set_rhost_port(ctx);

        sys_mutex_lock(server->lock); /* Policy check is not thread safe */
        ctx->policy_counted = qd_policy_socket_accept(server->qd->policy, ctx->rhost);
        sys_mutex_unlock(server->lock);
        if (!ctx->policy_counted) {
            pn_transport_close_tail(tport);
            pn_transport_close_head(tport);
            return;
        }

        // Set up SSL
        if (config->ssl_profile)  {
            qd_log(ctx->server->log_source, QD_LOG_TRACE, "[C%"PRIu64"] Configuring SSL on %s", ctx->connection_id, name);
            if (listener_setup_ssl(ctx, config, tport) != QD_ERROR_NONE) {
                connect_fail(ctx, QD_AMQP_COND_INTERNAL_ERROR, "%s on %s", qd_error_message(), name);
                return;
            }
        }
        //
        // Set up SASL
        //
        sys_mutex_lock(ctx->server->lock);
        pn_sasl_t *sasl = pn_sasl(tport);
        if (ctx->server->sasl_config_path)
            pn_sasl_config_path(sasl, ctx->server->sasl_config_path);
        pn_sasl_config_name(sasl, ctx->server->sasl_config_name);
        if (config->sasl_mechanisms)
            pn_sasl_allowed_mechs(sasl, config->sasl_mechanisms);
        if (config->sasl_plugin_config.auth_service) {
            qd_log(server->log_source, QD_LOG_INFO, "[C%"PRIu64"] Enabling remote authentication service %s", ctx->connection_id, config->sasl_plugin_config.auth_service);
            pn_ssl_domain_t* plugin_ssl_domain = NULL;
            if (config->sasl_plugin_config.use_ssl) {
                plugin_ssl_domain = pn_ssl_domain(PN_SSL_MODE_CLIENT);

                if (config->sasl_plugin_config.ssl_certificate_file) {
                    if (pn_ssl_domain_set_credentials(plugin_ssl_domain,
                                                      config->sasl_plugin_config.ssl_certificate_file,
                                                      config->sasl_plugin_config.ssl_private_key_file,
                                                      config->sasl_plugin_config.ssl_password)) {
                        qd_log(server->log_source, QD_LOG_ERROR, "[C%"PRIu64"] Cannot set SSL credentials for authentication service", ctx->connection_id);
                    }
                }
                if (config->sasl_plugin_config.ssl_trusted_certificate_db) {
                    if (pn_ssl_domain_set_trusted_ca_db(plugin_ssl_domain, config->sasl_plugin_config.ssl_trusted_certificate_db)) {
                        qd_log(server->log_source, QD_LOG_ERROR, "[C%"PRIu64"] Cannot set trusted SSL certificate db for authentication service", ctx->connection_id);
                    } else {
                        if (pn_ssl_domain_set_peer_authentication(plugin_ssl_domain, PN_SSL_VERIFY_PEER, config->sasl_plugin_config.ssl_trusted_certificate_db)) {
                            qd_log(server->log_source, QD_LOG_ERROR, "[C%"PRIu64"] Cannot set SSL peer verification for authentication service", ctx->connection_id);
                        }
                    }
                }
                if (config->sasl_plugin_config.ssl_ciphers) {
                    if (pn_ssl_domain_set_ciphers(plugin_ssl_domain, config->sasl_plugin_config.ssl_ciphers)) {
                        qd_log(server->log_source, QD_LOG_ERROR, "[C%"PRIu64"] Cannot set ciphers for authentication service", ctx->connection_id);
                    }
                }
                if (config->sasl_plugin_config.ssl_protocols) {
                    if (pn_ssl_domain_set_protocols(plugin_ssl_domain, config->sasl_plugin_config.ssl_protocols)) {
                        qd_log(server->log_source, QD_LOG_ERROR, "[C%"PRIu64"] Cannot set protocols for authentication service", ctx->connection_id);
                    }
                }
            }
            qdr_use_remote_authentication_service(tport, config->sasl_plugin_config.auth_service, config->sasl_plugin_config.sasl_init_hostname, plugin_ssl_domain, server->proactor);
        }
        pn_transport_require_auth(tport, config->requireAuthentication);
        pn_transport_require_encryption(tport, config->requireEncryption);
        pn_sasl_set_allow_insecure_mechs(sasl, config->allowInsecureAuthentication);
        sys_mutex_unlock(ctx->server->lock);

        qd_log(ctx->server->log_source, QD_LOG_INFO, "[C%"PRIu64"] Accepted connection to %s from %s",
               ctx->connection_id, name, ctx->rhost_port);
    } else if (ctx->connector) { /* Establishing an outgoing connection */
        config = &ctx->connector->config;
        if (!setup_ssl_sasl_and_open(ctx)) {
            qd_log(ctx->server->log_source, QD_LOG_ERROR, "[C%"PRIu64"] Connection aborted due to internal setup error",
               ctx->connection_id);
            pn_transport_close_tail(tport);
            pn_transport_close_head(tport);
            return;
        }

    } else {                    /* No connector and no listener */
        connect_fail(ctx, QD_AMQP_COND_INTERNAL_ERROR, "unknown Connection");
        return;
    }

    //
    // Common transport configuration.
    //
    pn_transport_set_max_frame(tport, config->max_frame_size);
    pn_transport_set_channel_max(tport, config->max_sessions - 1);
    pn_transport_set_idle_timeout(tport, config->idle_timeout_seconds * 1000);
}


static void invoke_deferred_calls(qd_connection_t *conn, bool discard)
{
    if (!conn)
        return;

    // Lock access to deferred_calls, other threads may concurrently add to it.  Invoke
    // the calls outside of the critical section.
    //
    sys_mutex_lock(conn->deferred_call_lock);
    qd_deferred_call_t *dc;
    while ((dc = DEQ_HEAD(conn->deferred_calls))) {
        DEQ_REMOVE_HEAD(conn->deferred_calls);
        sys_mutex_unlock(conn->deferred_call_lock);
        dc->call(dc->context, discard);
        free_qd_deferred_call_t(dc);
        sys_mutex_lock(conn->deferred_call_lock);
    }
    sys_mutex_unlock(conn->deferred_call_lock);
}

void qd_container_handle_event(qd_container_t *container, pn_event_t *event, pn_connection_t *pn_conn, qd_connection_t *qd_conn);
void qd_conn_event_batch_complete(qd_container_t *container, qd_connection_t *qd_conn, bool conn_closed);

static void handle_event_with_context(pn_event_t *e, qd_server_t *qd_server, qd_handler_context_t *context)
{
    if (context && context->handler) {
        context->handler(e, qd_server, context->context);
    }
}

static void do_handle_raw_connection_event(pn_event_t *e, qd_server_t *qd_server)
{
    handle_event_with_context(e, qd_server, (qd_handler_context_t*) pn_raw_connection_get_context(pn_event_raw_connection(e)));
}

static void do_handle_listener(pn_event_t *e, qd_server_t *qd_server)
{
    handle_event_with_context(e, qd_server, (qd_handler_context_t*) pn_listener_get_context(pn_event_listener(e)));
}

pn_proactor_t *qd_server_proactor(qd_server_t *qd_server)
{
    return qd_server->proactor;
}

static void handle_listener(pn_event_t *e, qd_server_t *qd_server, void *context) {
    qd_log_source_t *log = qd_server->log_source;

    qd_listener_t *li = (qd_listener_t*) context;
    const char *host_port = li->config.host_port;
    const char *port = li->config.port;

    switch (pn_event_type(e)) {

    case PN_LISTENER_OPEN: {

        if (strcmp(port, "0") == 0) {
            // If a 0 (zero) is specified for a port, get the actual listening port from the listener.
            pn_listener_t *l = pn_event_listener(e);
            const pn_netaddr_t *na = pn_listener_addr(l);
            char str[PN_MAX_ADDR] = "";
            pn_netaddr_str(na, str, sizeof(str));
            // "str" contains the host and port on which this listener is listening.
            if (li->config.name)
                qd_log(log, QD_LOG_NOTICE, "Listening on %s (%s)", str, li->config.name);
            else
                qd_log(log, QD_LOG_NOTICE, "Listening on %s", str);
        }
        else {
            qd_log(log, QD_LOG_NOTICE, "Listening on %s", host_port);
        }

        break;
    }

    case PN_LISTENER_ACCEPT:
        qd_log(log, QD_LOG_TRACE, "Accepting connection on %s", host_port);
        on_accept(e, li);
        break;

    case PN_LISTENER_CLOSE:
        if (li->pn_listener) {
            pn_condition_t *cond = pn_listener_condition(li->pn_listener);
            if (pn_condition_is_set(cond)) {
                qd_log(log, QD_LOG_ERROR, "Listener error on %s: %s (%s)", host_port,
                       pn_condition_get_description(cond),
                       pn_condition_get_name(cond));
                if (li->exit_on_error) {
                    qd_log(log, QD_LOG_CRITICAL, "Shutting down, required listener failed %s",
                           host_port);
                    exit(1);
                }
            } else {
                qd_log(log, QD_LOG_TRACE, "Listener closed on %s", host_port);
            }
            pn_listener_set_context(li->pn_listener, 0);
            li->pn_listener = 0;
            qd_listener_decref(li);
        }
        break;

    default:
        break;
    }
}


bool qd_connector_has_failover_info(qd_connector_t* ct)
{
    if (ct && DEQ_SIZE(ct->conn_info_list) > 1)
        return true;
    return false;
}


static void qd_connection_free(qd_connection_t *ctx)
{
    qd_server_t *qd_server = ctx->server;

    // If this is a dispatch connector, schedule the re-connect timer
    if (ctx->connector) {
        long delay = ctx->connector->delay;
        sys_mutex_lock(ctx->connector->lock);
        ctx->connector->ctx = 0;
        // Increment the connection index by so that we can try connecting to the failover url (if any).
        bool has_failover = qd_connector_has_failover_info(ctx->connector);

        if (has_failover) {
            // Go thru the failover list round robin.
            // IMPORTANT: Note here that we set the re-try timer to 1 second.
            // We want to quickly keep cycling thru the failover urls every second.
            delay = 1000;
        }

        ctx->connector->state = CXTR_STATE_CONNECTING;
        sys_mutex_unlock(ctx->connector->lock);

        //
        // Increment the ref-count to account for the timer's reference to the connector.
        //
        sys_atomic_inc(&ctx->connector->ref_count);
        qd_timer_schedule(ctx->connector->timer, delay);
    }

    sys_mutex_lock(qd_server->lock);
    DEQ_REMOVE(qd_server->conn_list, ctx);

    // If counted for policy enforcement, notify it has closed
    if (ctx->policy_counted) {
        qd_policy_socket_close(qd_server->qd->policy, ctx);
    }
    sys_mutex_unlock(qd_server->lock);

    invoke_deferred_calls(ctx, true);  // Discard any pending deferred calls
    sys_mutex_free(ctx->deferred_call_lock);
    qd_policy_settings_free(ctx->policy_settings);
    if (ctx->free_user_id) free((char*)ctx->user_id);
    if (ctx->timer) qd_timer_free(ctx->timer);
    free(ctx->name);
    free(ctx->role);
    sys_mutex_lock(qd_server->conn_activation_lock);
    free_qd_connection_t(ctx);
    sys_mutex_unlock(qd_server->conn_activation_lock);

    /* Note: pn_conn is freed by the proactor */
}


static void timeout_on_handhsake(void *context, bool discard)
{
    if (discard)
        return;

    qd_connection_t *ctx   = (qd_connection_t*) context;
    pn_transport_t  *tport = pn_connection_transport(ctx->pn_conn);
    pn_transport_close_head(tport);
    connect_fail(ctx, QD_AMQP_COND_NOT_ALLOWED, "Timeout waiting for initial handshake");
}


static void startup_timer_handler(void *context)
{
    //
    // This timer fires for a connection if it has not had a REMOTE_OPEN
    // event in a time interval from the CONNECTION_INIT event.  Close
    // down the transport in an IO thread reserved for that connection.
    //
    qd_connection_t *ctx = (qd_connection_t*) context;
    qd_timer_free(ctx->timer);
    ctx->timer = 0;
    qd_connection_invoke_deferred(ctx, timeout_on_handhsake, context);
}

static void qd_increment_conn_index(qd_connection_t *ctx)
{
    if (ctx->connector) {
        qd_failover_item_t *item = qd_connector_get_conn_info(ctx->connector);

        if (item->retries == 1) {
            ctx->connector->conn_index += 1;
            if (ctx->connector->conn_index > DEQ_SIZE(ctx->connector->conn_info_list))
                ctx->connector->conn_index = 1;
            item->retries = 0;
        }
        else
            item->retries += 1;
    }

}


/* Events involving a connection or listener are serialized by the proactor so
 * only one event per connection / listener will be processed at a time.
 */
static bool handle(qd_server_t *qd_server, pn_event_t *e, pn_connection_t *pn_conn, qd_connection_t *ctx)
{
    if (pn_conn && qdr_is_authentication_service_connection(pn_conn)) {
        qdr_handle_authentication_service_connection_event(e);
        return true;
    }

    switch (pn_event_type(e)) {

    case PN_PROACTOR_INTERRUPT:
        /* Interrupt the next thread */
        pn_proactor_interrupt(qd_server->proactor);
        /* Stop the current thread */
        return false;

    case PN_PROACTOR_TIMEOUT:
        qd_timer_visit();
        break;

    case PN_LISTENER_OPEN:
    case PN_LISTENER_ACCEPT:
    case PN_LISTENER_CLOSE:
        do_handle_listener(e, qd_server);
        break;

    case PN_CONNECTION_INIT: {
        const qd_server_config_t *config = ctx && ctx->listener ? &ctx->listener->config : 0;
        if (config && config->initial_handshake_timeout_seconds > 0) {
            ctx->timer = qd_timer(qd_server->qd, startup_timer_handler, ctx);
            qd_timer_schedule(ctx->timer, config->initial_handshake_timeout_seconds * 1000);
        }
        break;
    }

    case PN_CONNECTION_BOUND:
        on_connection_bound(qd_server, e);
        break;

    case PN_CONNECTION_REMOTE_OPEN:
        // If we are transitioning to the open state, notify the client via callback.
        if (ctx && ctx->timer) {
            qd_timer_free(ctx->timer);
            ctx->timer = 0;
        }
        if (ctx && !ctx->opened) {
            ctx->opened = true;
            if (ctx->connector) {
                ctx->connector->delay = 2000;  // Delay re-connect in case there is a recurring error
                qd_failover_item_t *item = qd_connector_get_conn_info(ctx->connector);
                if (item)
                    item->retries = 0;
            }
        }
        break;

    case PN_CONNECTION_WAKE:
        invoke_deferred_calls(ctx, false);
        break;

    case PN_TRANSPORT_ERROR:
        {
            pn_transport_t *transport = pn_event_transport(e);
            pn_condition_t* condition = transport ? pn_transport_condition(transport) : NULL;
            if (ctx && ctx->connector) { /* Outgoing connection */
                qd_increment_conn_index(ctx);
                const qd_server_config_t *config = &ctx->connector->config;
                ctx->connector->state = CXTR_STATE_FAILED;
                char conn_msg[300];
                if (condition  && pn_condition_is_set(condition)) {
                    qd_format_string(conn_msg, 300, "[C%"PRIu64"] Connection to %s failed: %s %s", ctx->connection_id, config->host_port,
                            pn_condition_get_name(condition), pn_condition_get_description(condition));
                    strcpy(ctx->connector->conn_msg, conn_msg);

                    qd_log(qd_server->log_source, QD_LOG_INFO, conn_msg);
                } else {
                    qd_format_string(conn_msg, 300, "[C%"PRIu64"] Connection to %s failed", ctx->connection_id, config->host_port);
                    strcpy(ctx->connector->conn_msg, conn_msg);
                    qd_log(qd_server->log_source, QD_LOG_INFO, conn_msg);
                }
            } else if (ctx && ctx->listener) { /* Incoming connection */
                if (condition && pn_condition_is_set(condition)) {
                    qd_log(ctx->server->log_source, QD_LOG_INFO, "[C%"PRIu64"] Connection from %s (to %s) failed: %s %s",
                           ctx->connection_id, ctx->rhost_port, ctx->listener->config.host_port, pn_condition_get_name(condition),
                           pn_condition_get_description(condition));
                }
            }
        }
        break;

    case PN_RAW_CONNECTION_CONNECTED:
    case PN_RAW_CONNECTION_CLOSED_READ:
    case PN_RAW_CONNECTION_CLOSED_WRITE:
    case PN_RAW_CONNECTION_DISCONNECTED:
    case PN_RAW_CONNECTION_NEED_READ_BUFFERS:
    case PN_RAW_CONNECTION_NEED_WRITE_BUFFERS:
    case PN_RAW_CONNECTION_READ:
    case PN_RAW_CONNECTION_WRITTEN:
    case PN_RAW_CONNECTION_WAKE:
        do_handle_raw_connection_event(e, qd_server);
        break;
    default:
        break;
    } // Switch event type

    if (ctx)
        qd_container_handle_event(qd_server->container, e, pn_conn, ctx);

    return true;
}

static void *thread_run(void *arg)
{
    qd_server_t      *qd_server = (qd_server_t*)arg;
    bool running = true;
    while (running) {
        pn_event_batch_t *events = pn_proactor_wait(qd_server->proactor);
        pn_event_t * e;
        qd_connection_t *qd_conn = 0;
        pn_connection_t *pn_conn = 0;

        while (running && (e = pn_event_batch_next(events))) {
            pn_connection_t *conn = pn_event_connection(e);

            if (!pn_conn)
                pn_conn = conn;
            assert(pn_conn == conn);

            if (!qd_conn)
                qd_conn = !!pn_conn ? (qd_connection_t*) pn_connection_get_context(pn_conn) : 0;

            running = handle(qd_server, e, conn, qd_conn);

            /* Free the connection after all other processing is complete */
            if (qd_conn && pn_event_type(e) == PN_TRANSPORT_CLOSED) {
                qd_conn_event_batch_complete(qd_server->container, qd_conn, true);
                pn_connection_set_context(pn_conn, NULL);
                qd_connection_free(qd_conn);
                qd_conn = 0;
            }
        }

        //
        // Notify the container that the batch is complete so it can do after-batch
        // processing.
        //
        if (qd_conn)
            qd_conn_event_batch_complete(qd_server->container, qd_conn, false);

        pn_proactor_done(qd_server->proactor, events);
    }
    return NULL;
}


static qd_failover_item_t *qd_connector_get_conn_info(qd_connector_t *ct) {

    qd_failover_item_t *item = DEQ_HEAD(ct->conn_info_list);

    if (DEQ_SIZE(ct->conn_info_list) > 1) {
        for (int i=1; i < ct->conn_index; i++) {
            item = DEQ_NEXT(item);
        }
    }
    return item;
}


/* Timer callback to try/retry connection open */
static void try_open_lh(qd_connector_t *ct)
{
    if (ct->state != CXTR_STATE_CONNECTING && ct->state != CXTR_STATE_INIT) {
        /* No longer referenced by pn_connection or timer */
        qd_connector_decref(ct);
        return;
    }

    qd_connection_t *ctx = qd_server_connection(ct->server, &ct->config);
    if (!ctx) {                 /* Try again later */
        qd_log(ct->server->log_source, QD_LOG_CRITICAL, "Allocation failure connecting to %s",
               ct->config.host_port);
        ct->delay = 10000;
        sys_atomic_inc(&ct->ref_count);
        qd_timer_schedule(ct->timer, ct->delay);
        return;
    }

    ctx->connector    = ct;
    const qd_server_config_t *config = &ct->config;

    //
    // Set the hostname on the pn_connection. This hostname will be used by proton as the
    // hostname in the open frame.
    //

    qd_failover_item_t *item = qd_connector_get_conn_info(ct);

    char *current_host = item->host;
    char *host_port = item->host_port;

    pn_connection_set_hostname(ctx->pn_conn, current_host);

    // Set the sasl user name and password on the proton connection object. This has to be
    // done before pn_proactor_connect which will bind a transport to the connection
    if(config->sasl_username)
        pn_connection_set_user(ctx->pn_conn, config->sasl_username);
    if (config->sasl_password)
        pn_connection_set_password(ctx->pn_conn, config->sasl_password);

    ctx->connector->state = CXTR_STATE_OPEN;
    ct->ctx   = ctx;
    ct->delay = 5000;

    qd_log(ct->server->log_source, QD_LOG_TRACE,
           "[C%"PRIu64"] Connecting to %s", ctx->connection_id, host_port);
    /* Note: the transport is configured in the PN_CONNECTION_BOUND event */
    pn_proactor_connect(ct->server->proactor, ctx->pn_conn, host_port);
}

static bool setup_ssl_sasl_and_open(qd_connection_t *ctx)
{
    qd_connector_t *ct = ctx->connector;
    const qd_server_config_t *config = &ct->config;
    pn_transport_t *tport  = pn_connection_transport(ctx->pn_conn);

    //
    // Set up SSL if appropriate
    //
    if (config->ssl_profile) {
        pn_ssl_domain_t *domain = pn_ssl_domain(PN_SSL_MODE_CLIENT);

        if (!domain) {
            qd_error(QD_ERROR_RUNTIME, "SSL domain allocation failed for connection [C%"PRIu64"] to %s:%s",
                     ctx->connection_id, config->host, config->port);
            return false;
        }

        bool failed = false;

        // set our trusted database for checking the peer's cert:
        if (config->ssl_trusted_certificate_db) {
            if (pn_ssl_domain_set_trusted_ca_db(domain, config->ssl_trusted_certificate_db)) {
                qd_log(ct->server->log_source, QD_LOG_ERROR,
                       "SSL CA configuration failed for connection [C%"PRIu64"] to %s:%s",
                       ctx->connection_id, config->host, config->port);
                failed = true;
            }
        }

        // peer must provide a cert
        if (pn_ssl_domain_set_peer_authentication(domain,
                                                  PN_SSL_VERIFY_PEER,
                                                  config->ssl_trusted_certificate_db)) {
            qd_log(ct->server->log_source, QD_LOG_ERROR,
                    "SSL peer auth configuration failed for connection [C%"PRIu64"] to %s:%s",
                    ctx->connection_id, config->host, config->port);
                failed = true;
        }

        // configure our certificate if the peer requests one:
        if (config->ssl_certificate_file) {
            if (pn_ssl_domain_set_credentials(domain,
                                              config->ssl_certificate_file,
                                              config->ssl_private_key_file,
                                              config->ssl_password)) {
                qd_log(ct->server->log_source, QD_LOG_ERROR,
                       "SSL local certificate configuration failed for connection [C%"PRIu64"] to %s:%s",
                       ctx->connection_id, config->host, config->port);
                failed = true;
            }
        }

        if (config->ssl_ciphers) {
            if (pn_ssl_domain_set_ciphers(domain, config->ssl_ciphers)) {
                qd_log(ct->server->log_source, QD_LOG_ERROR,
                       "SSL cipher configuration failed for connection [C%"PRIu64"] to %s:%s",
                       ctx->connection_id, config->host, config->port);
                failed = true;
            }
        }

        if (config->ssl_protocols) {
            if (pn_ssl_domain_set_protocols(domain, config->ssl_protocols)) {
                qd_log(ct->server->log_source, QD_LOG_ERROR,
                       "Permitted TLS protocols configuration failed for connection [C%"PRIu64"] to %s:%s",
                       ctx->connection_id, config->host, config->port);
                failed = true;
            }
        }

        //If ssl is enabled and verify_host_name is true, instruct proton to verify peer name
        if (config->verify_host_name) {
            if (pn_ssl_domain_set_peer_authentication(domain, PN_SSL_VERIFY_PEER_NAME, NULL)) {
                qd_log(ct->server->log_source, QD_LOG_ERROR,
                        "SSL peer host name verification configuration failed for connection [C%"PRIu64"] to %s:%s",
                        ctx->connection_id, config->host, config->port);
                failed = true;
            }
        }

        if (!failed) {
            ctx->ssl = pn_ssl(tport);
            if (pn_ssl_init(ctx->ssl, domain, 0) != 0) {
                 qd_log(ct->server->log_source, QD_LOG_ERROR,
                        "SSL domain internal initialization failed for connection [C%"PRIu64"] to %s:%s",
                        ctx->connection_id, config->host, config->port);
                failed = true;
            }
        }
        pn_ssl_domain_free(domain);
        if (failed) {
            return false;
        }

    }

    //
    // Set up SASL
    //
    sys_mutex_lock(ct->server->lock);
    pn_sasl_t *sasl = pn_sasl(tport);
    if (config->sasl_mechanisms)
        pn_sasl_allowed_mechs(sasl, config->sasl_mechanisms);
    pn_sasl_set_allow_insecure_mechs(sasl, config->allowInsecureAuthentication);
    sys_mutex_unlock(ct->server->lock);

    pn_connection_open(ctx->pn_conn);
    return true;
}

static void try_open_cb(void *context) {
    qd_connector_t *ct = (qd_connector_t*) context;
    if (!qd_connector_decref(ct)) {
        sys_mutex_lock(ct->lock);   /* TODO aconway 2017-05-09: this lock looks too big */
        try_open_lh(ct);
        sys_mutex_unlock(ct->lock);
    }
}


qd_server_t *qd_server(qd_dispatch_t *qd, int thread_count, const char *container_name,
                       const char *sasl_config_path, const char *sasl_config_name)
{
    /* Initialize const members, 0 initialize all others. */
    qd_server_t tmp = { .thread_count = thread_count };
    qd_server_t *qd_server = NEW(qd_server_t);
    if (qd_server == 0)
        return 0;
    memcpy(qd_server, &tmp, sizeof(tmp));

    qd_server->qd               = qd;
    qd_server->log_source       = qd_log_source("SERVER");
    qd_server->protocol_log_source = qd_log_source("PROTOCOL");
    qd_server->container_name   = container_name;
    qd_server->sasl_config_path = sasl_config_path;
    qd_server->sasl_config_name = sasl_config_name;
    qd_server->proactor         = pn_proactor();
    qd_server->container        = 0;
    qd_server->start_context    = 0;
    qd_server->lock             = sys_mutex();
    qd_server->conn_activation_lock = sys_mutex();
    qd_server->cond             = sys_cond();
    DEQ_INIT(qd_server->conn_list);

    qd_timer_initialize(qd_server->lock);

    qd_server->pause_requests         = 0;
    qd_server->threads_paused         = 0;
    qd_server->pause_next_sequence    = 0;
    qd_server->pause_now_serving      = 0;
    qd_server->next_connection_id     = 1;
    qd_server->py_displayname_obj     = 0;

    qd_server->http = qd_http_server(qd_server, qd_server->log_source);

    qd_log(qd_server->log_source, QD_LOG_INFO, "Container Name: %s", qd_server->container_name);

    return qd_server;
}


void qd_server_free(qd_server_t *qd_server)
{
    if (!qd_server) return;

    qd_http_server_stop(qd_server->http); /* Stop HTTP threads immediately */
    qd_http_server_free(qd_server->http);

    qd_connection_t *ctx = DEQ_HEAD(qd_server->conn_list);
    while (ctx) {
        qd_log(qd_server->log_source, QD_LOG_INFO,
               "[C%"PRIu64"] Closing connection on shutdown",
               ctx->connection_id);
        DEQ_REMOVE_HEAD(qd_server->conn_list);
        if (ctx->pn_conn) {
            pn_transport_t *tport = pn_connection_transport(ctx->pn_conn);
            if (tport)
                pn_transport_set_context(tport, 0); /* for transport_tracer */
            qd_session_cleanup(ctx);
            pn_connection_set_context(ctx->pn_conn, 0);
        }
        if (ctx->free_user_id) free((char*)ctx->user_id);
        sys_mutex_free(ctx->deferred_call_lock);
        free(ctx->name);
        free(ctx->role);
        if (ctx->policy_settings)
            free_qd_policy_settings_t(ctx->policy_settings);
        free_qd_connection_t(ctx);
        ctx = DEQ_HEAD(qd_server->conn_list);
    }
    pn_proactor_free(qd_server->proactor);
    qd_timer_finalize();
    sys_mutex_free(qd_server->lock);
    sys_mutex_free(qd_server->conn_activation_lock);
    sys_cond_free(qd_server->cond);
    Py_XDECREF((PyObject *)qd_server->py_displayname_obj);
    free(qd_server);
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
        qd_connection_list_t  conn_list = qd->server->conn_list;
        qd_connection_t *conn = DEQ_HEAD(conn_list);
        while(conn) {
            //
            // If there is already a tracer on the transport, nothing to do, move on to the next connection.
            //
            pn_transport_t *tport  = pn_connection_transport(conn->pn_conn);
            if (! pn_transport_get_tracer(tport)) {
                pn_transport_trace(tport, PN_TRACE_FRM);
                pn_transport_set_tracer(tport, transport_tracer);
            }
            conn = DEQ_NEXT(conn);
        }
        sys_mutex_unlock(qd->server->lock);
    }
}


static double normalize_memory_size(const uint64_t bytes, const char **suffix)
{
    static const char * const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    const int units_ct = 5;
    const double base = 1024.0;

    double value = (double)bytes;
    for (int i = 0; i < units_ct; ++i) {
        if (value < base) {
            if (suffix)
                *suffix = units[i];
            return value;
        }
        value /= base;
    }
    if (suffix)
        *suffix = units[units_ct - 1];
    return value;
}

void qd_server_run(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;
    int i;
    assert(qd_server);
    assert(qd_server->container); // Server can't run without a container
    qd_log(qd_server->log_source,
           QD_LOG_NOTICE, "Operational, %d Threads Running (process ID %ld)",
           qd_server->thread_count, (long)getpid());

    const uintmax_t ram_size = qd_platform_memory_size();
    const uint64_t  vm_size = qd_router_memory_usage();
    if (ram_size && vm_size) {
        const char *suffix_vm = 0;
        const char *suffix_ram = 0;
        double vm = normalize_memory_size(vm_size, &suffix_vm);
        double ram = normalize_memory_size(ram_size, &suffix_ram);
        qd_log(qd_server->log_source, QD_LOG_NOTICE,
               "Process VmSize %.2f %s (%.2f %s available memory)",
               vm, suffix_vm, ram, suffix_ram);
    }

#ifndef NDEBUG
    qd_log(qd_server->log_source, QD_LOG_INFO, "Running in DEBUG Mode");
#endif
    int n = qd_server->thread_count - 1; /* Start count-1 threads + use current thread */
    sys_thread_t **threads = (sys_thread_t **)qd_calloc(n, sizeof(sys_thread_t*));
    for (i = 0; i < n; i++) {
        threads[i] = sys_thread(thread_run, qd_server);
    }
    thread_run(qd_server);      /* Use the current thread */
    for (i = 0; i < n; i++) {
        sys_thread_join(threads[i]);
        sys_thread_free(threads[i]);
    }
    free(threads);

    qd_log(qd_server->log_source, QD_LOG_NOTICE, "Shut Down");
}


void qd_server_stop(qd_dispatch_t *qd)
{
    /* Interrupt the proactor, async-signal-safe */
    pn_proactor_interrupt(qd->server->proactor);
}

void qd_server_activate(qd_connection_t *ctx)
{
    if (ctx) ctx->wake(ctx);
}


void qd_connection_set_context(qd_connection_t *conn, void *context)
{
    conn->user_context = context;
}


void *qd_connection_get_context(qd_connection_t *conn)
{
    return conn->user_context;
}


void *qd_connection_get_config_context(qd_connection_t *conn)
{
    return conn->context;
}


void qd_connection_set_link_context(qd_connection_t *conn, void *context)
{
    conn->link_context = context;
}


void *qd_connection_get_link_context(qd_connection_t *conn)
{
    return conn->link_context;
}


pn_connection_t *qd_connection_pn(qd_connection_t *conn)
{
    return conn->pn_conn;
}


bool qd_connection_inbound(qd_connection_t *conn)
{
    return conn->listener != 0;
}


uint64_t qd_connection_connection_id(qd_connection_t *conn)
{
    return conn->connection_id;
}


const qd_server_config_t *qd_connection_config(const qd_connection_t *conn)
{
    if (conn->listener)
        return &conn->listener->config;
    if (conn->connector)
        return &conn->connector->config;
    return NULL;
}


void qd_connection_invoke_deferred(qd_connection_t *conn, qd_deferred_t call, void *context)
{
    if (!conn)
        return;

    qd_deferred_call_t *dc = new_qd_deferred_call_t();
    DEQ_ITEM_INIT(dc);
    dc->call    = call;
    dc->context = context;

    sys_mutex_lock(conn->deferred_call_lock);
    DEQ_INSERT_TAIL(conn->deferred_calls, dc);
    sys_mutex_unlock(conn->deferred_call_lock);

    sys_mutex_lock(conn->server->conn_activation_lock);
    qd_server_activate(conn);
    sys_mutex_unlock(conn->server->conn_activation_lock);
}


qd_listener_t *qd_server_listener(qd_server_t *server)
{
    qd_listener_t *li = new_qd_listener_t();
    if (!li) return 0;
    ZERO(li);
    sys_atomic_init(&li->ref_count, 1);
    li->server      = server;
    li->http = NULL;
    li->type.context = li;
    li->type.handler = &handle_listener;
    return li;
}

static bool qd_listener_listen_pn(qd_listener_t *li) {
   li->pn_listener = pn_listener();
    if (li->pn_listener) {
        pn_listener_set_context(li->pn_listener, &li->type);
        pn_proactor_listen(li->server->proactor, li->pn_listener, li->config.host_port,
                           BACKLOG);
        sys_atomic_inc(&li->ref_count); /* In use by proactor, PN_LISTENER_CLOSE will dec */
        /* Listen is asynchronous, log "listening" message on PN_LISTENER_OPEN event */
    } else {
        qd_log(li->server->log_source, QD_LOG_CRITICAL, "No memory listening on %s",
               li->config.host_port);
     }
    return li->pn_listener;
}

static bool qd_listener_listen_http(qd_listener_t *li) {
    if (li->server->http) {
        /* qd_lws_listener holds a reference to li, will decref when closed */
        qd_http_server_listen(li->server->http, li);
        return li->http;
    } else {
        qd_log(li->server->log_source, QD_LOG_ERROR, "No HTTP support to listen on %s",
               li->config.host_port);
        return false;
    }
}


bool qd_listener_listen(qd_listener_t *li) {
    if (li->pn_listener || li->http) /* Already listening */
        return true;
    return li->config.http ? qd_listener_listen_http(li) : qd_listener_listen_pn(li);
}


void qd_listener_decref(qd_listener_t* li)
{
    if (li && sys_atomic_dec(&li->ref_count) == 1) {
        qd_server_config_free(&li->config);
        free_qd_listener_t(li);
    }
}


qd_connector_t *qd_server_connector(qd_server_t *server)
{
    qd_connector_t *ct = new_qd_connector_t();
    if (!ct) return 0;
    ZERO(ct);
    sys_atomic_init(&ct->ref_count, 1);
    ct->server  = server;
    qd_failover_item_list_t conn_info_list;
    DEQ_INIT(conn_info_list);
    ct->conn_info_list = conn_info_list;
    ct->conn_index = 1;
    ct->state   = CXTR_STATE_INIT;
    ct->lock = sys_mutex();
    ct->conn_msg = (char*) malloc(300);
    memset(ct->conn_msg, 0, 300);
    ct->timer = qd_timer(ct->server->qd, try_open_cb, ct);
    if (!ct->lock || !ct->timer) {
        qd_connector_decref(ct);
        return 0;
    }
    return ct;
}


const char *qd_connector_policy_vhost(qd_connector_t* ct)
{
    return ct->policy_vhost;
}


bool qd_connector_connect(qd_connector_t *ct)
{
    sys_mutex_lock(ct->lock);
    ct->ctx     = 0;
    ct->delay   = 0;
    /* Referenced by timer */
    sys_atomic_inc(&ct->ref_count);
    qd_timer_schedule(ct->timer, ct->delay);
    sys_mutex_unlock(ct->lock);
    return true;
}


bool qd_connector_decref(qd_connector_t* ct)
{
    if (ct && sys_atomic_dec(&ct->ref_count) == 1) {
        sys_mutex_lock(ct->lock);
        if (ct->ctx) {
            ct->ctx->connector = 0;
        }
        sys_mutex_unlock(ct->lock);
        qd_server_config_free(&ct->config);
        qd_timer_free(ct->timer);

        qd_failover_item_t *item = DEQ_HEAD(ct->conn_info_list);
        while (item) {
            DEQ_REMOVE_HEAD(ct->conn_info_list);
            free(item->scheme);
            free(item->host);
            free(item->port);
            free(item->hostname);
            free(item->host_port);
            free(item);
            item = DEQ_HEAD(ct->conn_info_list);
        }
        sys_mutex_free(ct->lock);
        if (ct->policy_vhost) free(ct->policy_vhost);
        free(ct->conn_msg);
        free_qd_connector_t(ct);
        return true;
    }
    return false;
}

__attribute__((noinline)) // permit replacement by dummy implementation in unit_tests
void qd_server_timeout(qd_server_t *server, qd_duration_t duration) {
    pn_proactor_set_timeout(server->proactor, duration);
}

qd_dispatch_t* qd_server_dispatch(qd_server_t *server) { return server->qd; }

const char* qd_connection_name(const qd_connection_t *c) {
    if (c->connector) {
        return c->connector->config.host_port;
    } else {
        return c->rhost_port;
    }
}

qd_connector_t* qd_connection_connector(const qd_connection_t *c) {
    return c->connector;
}

const qd_server_config_t *qd_connector_config(const qd_connector_t *c) {
    return &c->config;
}

qd_lws_listener_t *qd_listener_http(qd_listener_t *li) {
    return li->http;
}

const char* qd_connection_remote_ip(const qd_connection_t *c) {
    return c->rhost;
}

/* Expose event handling for HTTP connections */
bool qd_connection_handle(qd_connection_t *c, pn_event_t *e) {
    if (!c)
        return false;
    pn_connection_t *pn_conn = pn_event_connection(e);
    qd_connection_t *qd_conn = !!pn_conn ? (qd_connection_t*) pn_connection_get_context(pn_conn) : 0;
    handle(c->server, e, pn_conn, qd_conn);
    if (qd_conn && pn_event_type(e) == PN_TRANSPORT_CLOSED) {
        pn_connection_set_context(pn_conn, NULL);
        qd_connection_free(qd_conn);
        return false;
    }
    return true;
}


uint64_t qd_server_allocate_connection_id(qd_server_t *server)
{
    uint64_t id;
    sys_mutex_lock(server->lock);
    id = server->next_connection_id++;
    sys_mutex_unlock(server->lock);
    return id;
}


bool qd_connection_strip_annotations_in(const qd_connection_t *c) {
    return c->strip_annotations_in;
}

sys_mutex_t *qd_server_get_activation_lock(qd_server_t * server)
{
    return server->conn_activation_lock;
}

uint64_t qd_connection_max_message_size(const qd_connection_t *c) {
    return (c && c->policy_settings) ? c->policy_settings->spec.maxMessageSize : 0;
}
