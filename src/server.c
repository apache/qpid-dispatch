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

#include <Python.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/amqp.h>
#include <qpid/dispatch/server.h>
#include "qpid/dispatch/python_embedded.h"
#include "entity.h"
#include "entity_cache.h"
#include "dispatch_private.h"
#include "policy.h"
#include "server_private.h"
#include "timer_private.h"
#include "alloc.h"
#include "config.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

static __thread qd_server_t *thread_server = 0;

#define HEARTBEAT_INTERVAL 1000

ALLOC_DEFINE(qd_work_item_t);
ALLOC_DEFINE(qd_listener_t);
ALLOC_DEFINE(qd_connector_t);
ALLOC_DEFINE(qd_deferred_call_t);
ALLOC_DEFINE(qd_connection_t);
ALLOC_DEFINE(qd_user_fd_t);

const char *QD_CONNECTION_TYPE = "connection";
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

const char *DEFAULT_USER_ID = "anonymous";

static qd_thread_t *thread(qd_server_t *qd_server, int id)
{
    qd_thread_t *thread = NEW(qd_thread_t);
    if (!thread)
        return 0;

    thread->qd_server    = qd_server;
    thread->thread_id    = id;
    thread->running      = 0;
    thread->canceled     = 0;
    thread->using_thread = 0;

    return thread;
}

static void free_qd_connection(qd_connection_t *ctx)
{
    if (ctx->policy_settings) {
        if (ctx->policy_settings->sources)
            free(ctx->policy_settings->sources);
        if (ctx->policy_settings->targets)
            free(ctx->policy_settings->targets);
        free (ctx->policy_settings);
        ctx->policy_settings = 0;
    }
    if (ctx->pn_conn) {
        pn_connection_set_context(ctx->pn_conn, 0);
        pn_decref(ctx->pn_conn);
        ctx->pn_conn = NULL;
    }
    if (ctx->collector) {
        pn_collector_free(ctx->collector);
        ctx->collector = NULL;
    }

    if (ctx->free_user_id)
        free((char*)ctx->user_id);

    free(ctx->role);

    free_qd_connection_t(ctx);
}

qd_error_t qd_entity_update_connection(qd_entity_t* entity, void *impl);

/**
 * This function is set as the pn_transport->tracer and is invoked when proton tries to write the log message to pn_transport->tracer
 */
static void qd_transport_tracer(pn_transport_t *transport, const char *message)
{
    qd_connection_t *ctx = (qd_connection_t*) pn_transport_get_context(transport);
    if (ctx)
        qd_log(ctx->server->log_source, QD_LOG_TRACE, "[%"PRIu64"]:%s", ctx->connection_id, message);
}

static qd_error_t connection_entity_update_host(qd_entity_t* entity, qd_connection_t *conn)
{
    const qd_server_config_t *config;
    if (conn->connector) {
        config = conn->connector->config;
        char host[strlen(config->host)+strlen(config->port)+2];
        snprintf(host, sizeof(host), "%s:%s", config->host, config->port);
        return qd_entity_set_string(entity, "host", host);
    }
    else
        return qd_entity_set_string(entity, "host", qdpn_connector_name(conn->pn_cxtr));
}


/**
 * Save displayNameService object instance and ImportModule address
 * Called with qd_python_lock held
 */
qd_error_t qd_register_display_name_service(qd_dispatch_t *qd, void *displaynameservice)
{
    qd->server->py_displayname_obj    = displaynameservice;
    qd->server->py_displayname_module = PyImport_ImportModule("qpid_dispatch_internal.display_name.display_name");
    return qd->server->py_displayname_module ? QD_ERROR_NONE : qd_error(QD_ERROR_RUNTIME, "Fail importing DisplayNameService module");
}


/**
 * Returns a char pointer to a user id which is constructed from components specified in the config->ssl_uid_format.
 * Parses through each component and builds a semi-colon delimited string which is returned as the user id.
 */
static const char *qd_transport_get_user(qd_connection_t *conn, pn_transport_t *tport)
{
    const qd_server_config_t *config =
            conn->connector ? conn->connector->config : conn->listener->config;

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
                qd_log(conn->server->log_source, QD_LOG_CRITICAL, "Unrecognized component '%c' in uidFormat ", components[x]);
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
            if (config->ssl_display_name_file) {
                // Translate extracted id into display name
                qd_python_lock_state_t lock_state = qd_python_lock();
                PyObject *module = (PyObject*)conn->server->py_displayname_module;
                PyObject *query = PyObject_GetAttrString(module, "display_name_local_query");
                if (query) {
                    PyObject *result = PyObject_CallFunction(query, "(Oss)",
                                                            (PyObject *)conn->server->py_displayname_obj,
                                                            config->ssl_profile, user_id);
                    if (result) {
                        const char *res_string = PyString_AsString(result);
                        free(user_id);
                        user_id = malloc(strlen(res_string) + 1);
                        user_id[0] = '\0';
                        strcat(user_id, res_string);
                        Py_XDECREF(result);
                    } else {
                        qd_log(conn->server->log_source, QD_LOG_DEBUG, "Internal: failed to read displaynameservice query result");
                    }
                    Py_XDECREF(query);
                } else {
                    qd_log(conn->server->log_source, QD_LOG_DEBUG, "Internal: failed to locate query function");
                }
                Py_XDECREF(module);
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


/**
 * Allocate a new qd_connection
 *  with DEQ items initialized, call lock allocated, and all other fields cleared.
 */
qd_connection_t *qd_connection_allocate()
{
    qd_connection_t *ctx = new_qd_connection_t();
    ZERO(ctx);
    DEQ_ITEM_INIT(ctx);
    DEQ_INIT(ctx->deferred_calls);
    ctx->deferred_call_lock = sys_mutex();
    return ctx;
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
            const char *user_id = qd_transport_get_user(conn, tport);
            if (user_id)
                conn->user_id = user_id;
        }
    }
    if (!conn->user_id)
        conn->user_id = DEFAULT_USER_ID;
}


static void qd_get_next_pn_data(pn_data_t *data, const char **d, int *d1)
{
    if (pn_data_next(data)) {
        switch (pn_data_type(data)) {
            case PN_STRING:
                *d = pn_data_get_string(data).start;
                break;
            case PN_SYMBOL:
                *d = pn_data_get_symbol(data).start;
                break;
            case PN_INT:
                *d1 = pn_data_get_int(data);
                break;
            default:
                break;
        }
    }
}


/**
 * Obtains the remote connection properties and sets it as a map on the passed in entity.
 * @param
 */
static qd_error_t qd_set_connection_properties(qd_entity_t* entity, qd_connection_t *conn)
{
    // Get the connection properties and stick it into the entity as a map
    pn_data_t *data = pn_connection_remote_properties(conn->pn_conn);
    const char *props = "properties";
    if (data) {
        size_t count = pn_data_get_map(data);
        pn_data_enter(data);

        // Create a new map.
        qd_error_t error_t = qd_entity_set_map(entity, props);

        if (error_t != QD_ERROR_NONE)
            return error_t;

        for (size_t i = 0; i < count/2; i++) {
            const char *key   = 0;
            // We are assuming for now that all keys are strings
            qd_get_next_pn_data(data, &key, 0);
            const char *value_string = 0;
            int value_int = 0;
            // We are assuming for now that all values are either strings or integers
            qd_get_next_pn_data(data, &value_string, &value_int);

            if (value_string)
                error_t = qd_entity_set_map_key_value_string(entity, props, key, value_string);
            else if (value_int)
                error_t = qd_entity_set_map_key_value_int(entity, props, key, value_int);

            if (error_t != QD_ERROR_NONE)
                return error_t;
        }
        pn_data_exit(data);
    }

    return QD_ERROR_NONE;
}


qd_error_t qd_entity_refresh_sslProfile(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}


qd_error_t qd_entity_refresh_connection(qd_entity_t* entity, void *impl)
{
    qd_connection_t *conn = (qd_connection_t*)impl;
    pn_transport_t *tport = 0;
    pn_sasl_t      *sasl  = 0;
    pn_ssl_t       *ssl   = 0;
    const char     *mech  = 0;
    const char     *user  = 0;

    if (conn->pn_conn) {
        tport = pn_connection_transport(conn->pn_conn);
        ssl   = conn->ssl;
    }
    if (tport) {
        sasl = pn_sasl(tport);
        if(conn->user_id)
            user = conn->user_id;
        else
            user = pn_transport_get_user(tport);
    }
    if (sasl)
        mech = pn_sasl_get_mech(sasl);

    if (qd_entity_set_bool(entity, "opened", conn->opened) == 0 &&
        qd_entity_set_string(entity, "container",
                             conn->pn_conn ? pn_connection_remote_container(conn->pn_conn) : 0) == 0 &&
        connection_entity_update_host(entity, conn) == 0 &&
        qd_entity_set_string(entity, "sasl", mech) == 0 &&
        qd_entity_set_string(entity, "role", conn->role) == 0 &&
        qd_entity_set_string(entity, "dir", conn->connector ? "out" : "in") == 0 &&
        qd_entity_set_string(entity, "user", user) == 0 &&
        qd_set_connection_properties(entity, conn) == 0 &&
        qd_entity_set_long(entity, "identity", conn->connection_id) == 0 &&
        qd_entity_set_bool(entity, "isAuthenticated", tport && pn_transport_is_authenticated(tport)) == 0 &&
        qd_entity_set_bool(entity, "isEncrypted", tport && pn_transport_is_encrypted(tport)) == 0 &&
        qd_entity_set_bool(entity, "ssl", ssl != 0) == 0) {

        if (ssl) {
            #define SSL_ATTR_SIZE 50
            char proto[SSL_ATTR_SIZE];
            char cipher[SSL_ATTR_SIZE];
            pn_ssl_get_protocol_name(ssl, proto, SSL_ATTR_SIZE);
            pn_ssl_get_cipher_name(ssl, cipher, SSL_ATTR_SIZE);
            if (qd_entity_set_string(entity, "sslProto", proto)   == 0 &&
                qd_entity_set_string(entity, "sslCipher", cipher) == 0 &&
                qd_entity_set_long(entity, "sslSsf", pn_ssl_get_ssf(ssl)) == 0) {
                    return QD_ERROR_NONE;
            }
        }
        else
            return QD_ERROR_NONE;
    }

    return qd_error_code();
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
    if (!config->ssl_required) {
        if (pn_ssl_domain_allow_unsecured_client(domain)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME, "Cannot allow unsecured client");
        }
    }

    // for peer authentication:
    if (config->ssl_trusted_certificate_db) {
        if (pn_ssl_domain_set_trusted_ca_db(domain, config->ssl_trusted_certificate_db)) {
            pn_ssl_domain_free(domain);
            return qd_error(QD_ERROR_RUNTIME, "Cannot set trusted SSL CA" );
        }
    }

    const char *trusted = config->ssl_trusted_certificate_db;
    if (config->ssl_trusted_certificates)
        trusted = config->ssl_trusted_certificates;

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

    pn_ssl_domain_free(domain);
    return QD_ERROR_NONE;
}

// Format the identity of an incoming connection to buf for logging
static const char *log_incoming(char *buf, size_t size, qdpn_connector_t *cxtr)
{
    qd_listener_t *qd_listener = qdpn_listener_context(qdpn_connector_listener(cxtr));
    assert(qd_listener);
    const char *cname = qdpn_connector_name(cxtr);
    const char *host = qd_listener->config->host;
    const char *port = qd_listener->config->port;
    snprintf(buf, size, "incoming connection from %s to %s:%s", cname, host, port);
    return buf;
}


static void decorate_connection(qd_server_t *qd_server, pn_connection_t *conn, const qd_server_config_t *config)
{
    size_t clen = strlen(QD_CAPABILITY_ANONYMOUS_RELAY);

    //
    // Set the container name
    //
    pn_connection_set_container(conn, qd_server->container_name);

    //
    // Offer ANONYMOUS_RELAY capability
    //
    pn_data_put_symbol(pn_connection_offered_capabilities(conn), pn_bytes(clen, (char*) QD_CAPABILITY_ANONYMOUS_RELAY));

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

    if (config && config->inter_router_cost > 1) {
        pn_data_put_symbol(pn_connection_properties(conn),
                           pn_bytes(strlen(QD_CONNECTION_PROPERTY_COST_KEY), QD_CONNECTION_PROPERTY_COST_KEY));
        pn_data_put_int(pn_connection_properties(conn), config->inter_router_cost);
    }

    pn_data_exit(pn_connection_properties(conn));
}


static void thread_process_listeners_LH(qd_server_t *qd_server)
{
    qdpn_driver_t    *driver = qd_server->driver;
    qdpn_listener_t  *listener;
    qdpn_connector_t *cxtr;
    qd_connection_t  *ctx;

    for (listener = qdpn_driver_listener(driver); listener; listener = qdpn_driver_listener(driver)) {
        bool policy_counted = false;
        cxtr = qdpn_listener_accept(listener, qd_server->qd->policy, &qd_policy_socket_accept, &policy_counted);
        if (!cxtr)
            continue;

        char logbuf[qd_log_max_len()];

        qd_log(qd_server->log_source, QD_LOG_DEBUG, "Accepting %s",
               log_incoming(logbuf, sizeof(logbuf), cxtr));
        
        ctx = qd_connection_allocate();
        ctx->server        = qd_server;
        ctx->owner_thread  = CONTEXT_UNSPECIFIED_OWNER;
        ctx->pn_cxtr       = cxtr;
        ctx->listener      = qdpn_listener_context(listener);
        ctx->context       = ctx->listener->context;
        ctx->connection_id = qd_server->next_connection_id++; // Increment the connection id so the next connection can use it
        ctx->policy_counted = policy_counted;

        // Copy the role from the listener config
        int role_length    = strlen(ctx->listener->config->role) + 1;
        ctx->role          = (char*) malloc(role_length);
        strcpy(ctx->role, ctx->listener->config->role);

        pn_connection_t *conn = pn_connection();
        ctx->collector = pn_collector();
        pn_connection_collect(conn, ctx->collector);
        decorate_connection(qd_server, conn, ctx->listener->config);
        qdpn_connector_set_connection(cxtr, conn);
        pn_connection_set_context(conn, ctx);
        ctx->pn_conn = conn;
        ctx->owner_thread = CONTEXT_NO_OWNER;
        qdpn_connector_set_context(cxtr, ctx);

        // qd_server->lock is already locked
        DEQ_INSERT_TAIL(qd_server->connections, ctx);
        qd_entity_cache_add(QD_CONNECTION_TYPE, ctx);

        //
        // Get a pointer to the transport so we can insert security components into it
        //
        pn_transport_t           *tport  = qdpn_connector_transport(cxtr);
        const qd_server_config_t *config = ctx->listener->config;

        //
        // Configure the transport.
        //
        pn_transport_set_server(tport);
        pn_transport_set_max_frame(tport, config->max_frame_size);
        pn_transport_set_idle_timeout(tport, config->idle_timeout_seconds * 1000);

        //
        // Proton pushes out its trace to qd_transport_tracer() which in turn writes a trace message to the qdrouter log
        // If trace level logging is enabled on the router set PN_TRACE_DRV | PN_TRACE_FRM | PN_TRACE_RAW on the proton transport
        //
        pn_transport_set_context(tport, ctx);
        if (qd_log_enabled(qd_server->log_source, QD_LOG_TRACE)) {
            pn_transport_trace(tport, PN_TRACE_DRV | PN_TRACE_FRM | PN_TRACE_RAW);
            pn_transport_set_tracer(tport, qd_transport_tracer);
        }

        // Set up SSL if configured
        if (config->ssl_profile) {
            qd_log(qd_server->log_source, QD_LOG_TRACE, "Configuring SSL on %s",
                   log_incoming(logbuf, sizeof(logbuf), cxtr));
            if (listener_setup_ssl(ctx, config, tport) != QD_ERROR_NONE) {
                qd_log(qd_server->log_source, QD_LOG_ERROR, "%s on %s",
                       qd_error_message(), log_incoming(logbuf, sizeof(logbuf), cxtr));
                qdpn_connector_close(cxtr);
                continue;
            }
        }

        //
        // Set up SASL
        //
        pn_sasl_t *sasl = pn_sasl(tport);
        if (qd_server->sasl_config_path)
            pn_sasl_config_path(sasl, qd_server->sasl_config_path);
        pn_sasl_config_name(sasl, qd_server->sasl_config_name);
        if (config->sasl_mechanisms)
            pn_sasl_allowed_mechs(sasl, config->sasl_mechanisms);
        pn_transport_require_auth(tport, config->requireAuthentication);
        pn_transport_require_encryption(tport, config->requireEncryption);
        pn_sasl_set_allow_insecure_mechs(sasl, config->allowInsecureAuthentication);
    }
}


static void handle_signals_LH(qd_server_t *qd_server)
{
    int signum = qd_server->pending_signal;

    if (signum && !qd_server->signal_handler_running) {
        qd_server->signal_handler_running = true;
        qd_server->pending_signal = 0;
        if (qd_server->signal_handler) {
            sys_mutex_unlock(qd_server->lock);
            qd_server->signal_handler(qd_server->signal_context, signum);
            sys_mutex_lock(qd_server->lock);
        }
        qd_server->signal_handler_running = false;
    }
}


static void block_if_paused_LH(qd_server_t *qd_server)
{
    if (qd_server->pause_requests > 0) {
        qd_server->threads_paused++;
        sys_cond_signal_all(qd_server->cond);
        while (qd_server->pause_requests > 0)
            sys_cond_wait(qd_server->cond, qd_server->lock);
        qd_server->threads_paused--;
    }
}


static void invoke_deferred_calls(qd_connection_t *conn, bool discard)
{
    qd_deferred_call_list_t  calls;
    qd_deferred_call_t      *dc;

    //
    // Copy the deferred calls out of the connection under lock.
    //
    DEQ_INIT(calls);
    sys_mutex_lock(conn->deferred_call_lock);
    dc = DEQ_HEAD(conn->deferred_calls);
    while (dc) {
        DEQ_REMOVE_HEAD(conn->deferred_calls);
        DEQ_INSERT_TAIL(calls, dc);
        dc = DEQ_HEAD(conn->deferred_calls);
    }
    sys_mutex_unlock(conn->deferred_call_lock);

    //
    // Invoke the calls outside of the critical section.
    //
    dc = DEQ_HEAD(calls);
    while (dc) {
        DEQ_REMOVE_HEAD(calls);
        dc->call(dc->context, discard);
        free_qd_deferred_call_t(dc);
        dc = DEQ_HEAD(calls);
    }
}


static int process_connector(qd_server_t *qd_server, qdpn_connector_t *cxtr)
{
    qd_connection_t *ctx = qdpn_connector_context(cxtr);
    int events = 0;
    int passes = 0;

    if (ctx->closed)
        return 0;

    //
    // If this is a user connection, bypass the AMQP processing and invoke the
    // UserFD handler instead.
    //
    if (ctx->ufd) {
        qd_server->ufd_handler(ctx->ufd->context, ctx->ufd);
        return 1;
    }

    do {
        passes++;

        //
        // Step the engine for pre-handler processing
        //
        qdpn_connector_process(cxtr);

        //
        // If the connector has closed, notify the client via callback.
        //
        if (qdpn_connector_closed(cxtr)) {
            if (ctx->opened)
                qd_server->conn_handler(qd_server->conn_handler_context, ctx->context,
                                        QD_CONN_EVENT_CLOSE,
                                        (qd_connection_t*) qdpn_connector_context(cxtr));
            ctx->closed = true;
            events = 0;
            break;
        }

        invoke_deferred_calls(ctx, false);

        qd_connection_t *qd_conn   = (qd_connection_t*) qdpn_connector_context(cxtr);
        pn_collector_t  *collector = qd_connection_collector(qd_conn);
        pn_event_t      *event;

        events = 0;
        if (!ctx->event_stall) {
            event = pn_collector_peek(collector);
            while (event) {
                //
                // If we are transitioning to the open state, notify the client via callback.
                //
                if (!ctx->opened && pn_event_type(event) == PN_CONNECTION_REMOTE_OPEN) {
                    ctx->opened = true;
                    qd_conn_event_t ce = QD_CONN_EVENT_LISTENER_OPEN;

                    if (ctx->connector) {
                        ce = QD_CONN_EVENT_CONNECTOR_OPEN;
                        ctx->connector->delay = 2000;  // Delay on re-connect in case there is a recurring error
                    } else
                        assert(ctx->listener);

                    qd_server->conn_handler(qd_server->conn_handler_context,
                                            ctx->context, ce, (qd_connection_t*) qdpn_connector_context(cxtr));
                    events = 1;
                } else if (pn_event_type(event) == PN_TRANSPORT_ERROR) {
                    if (ctx->connector) {
                        const qd_server_config_t *config = ctx->connector->config;
                        qd_log(qd_server->log_source, QD_LOG_TRACE, "Connection to %s:%s failed", config->host, config->port);
                    }
                }

                events += qd_server->pn_event_handler(qd_server->conn_handler_context, ctx->context, event, qd_conn);
                pn_collector_pop(collector);

                event = ctx->event_stall ? 0 : pn_collector_peek(collector);
            }
            events += qd_server->conn_handler(qd_server->conn_handler_context, ctx->context, QD_CONN_EVENT_WRITABLE, qd_conn);
        }
    } while (events > 0);

    return passes > 1;
}


//
// TEMPORARY FUNCTION PROTOTYPES
//
void qdpn_driver_wait_1(qdpn_driver_t *d);
int  qdpn_driver_wait_2(qdpn_driver_t *d, int timeout);
void qdpn_driver_wait_3(qdpn_driver_t *d);
//
// END TEMPORARY
//

static void *thread_run(void *arg)
{
    qd_thread_t      *thread    = (qd_thread_t*) arg;
    qd_work_item_t   *work;
    qdpn_connector_t *cxtr;
    qd_connection_t  *ctx;
    int               error;
    int               poll_result;

    if (!thread)
        return 0;

    qd_server_t      *qd_server = thread->qd_server;
    thread_server   = qd_server;
    thread->running = 1;

    if (thread->canceled)
        return 0;

    //
    // Invoke the start handler if the application supplied one.
    // This handler can be used to set NUMA or processor affinnity for the thread.
    //
    if (qd_server->start_handler)
        qd_server->start_handler(qd_server->start_context, thread->thread_id);

    //
    // Main Loop
    //
    while (thread->running) {
        sys_mutex_lock(qd_server->lock);

        //
        // Check for pending signals to process
        //
        handle_signals_LH(qd_server);
        if (!thread->running) {
            sys_mutex_unlock(qd_server->lock);
            break;
        }

        //
        // Check to see if the server is pausing.  If so, block here.
        //
        block_if_paused_LH(qd_server);
        if (!thread->running) {
            sys_mutex_unlock(qd_server->lock);
            break;
        }

        //
        // Service pending timers.
        //
        qd_timer_t *timer = DEQ_HEAD(qd_server->pending_timers);
        if (timer) {
            DEQ_REMOVE_HEAD(qd_server->pending_timers);

            //
            // Mark the timer as idle in case it reschedules itself.
            //
            qd_timer_idle_LH(timer);

            //
            // Release the lock and invoke the connection handler.
            //
            sys_mutex_unlock(qd_server->lock);
            timer->handler(timer->context);
            qdpn_driver_wakeup(qd_server->driver);
            continue;
        }

        //
        // Check the work queue for connectors scheduled for processing.
        //
        work = DEQ_HEAD(qd_server->work_queue);
        if (!work) {
            //
            // There is no pending work to do
            //
            if (qd_server->a_thread_is_waiting) {
                //
                // Another thread is waiting on the proton driver, this thread must
                // wait on the condition variable until signaled.
                //
                sys_cond_wait(qd_server->cond, qd_server->lock);
            } else {
                //
                // This thread elects itself to wait on the proton driver.  Set the
                // thread-is-waiting flag so other idle threads will not interfere.
                //
                qd_server->a_thread_is_waiting = true;

                //
                // Ask the timer module when its next timer is scheduled to fire.  We'll
                // use this value in driver_wait as the timeout.  If there are no scheduled
                // timers, the returned value will be -1.
                //
                qd_timestamp_t duration = qd_timer_next_duration_LH();

                //
                // Invoke the proton driver's wait sequence.  This is a bit of a hack for now
                // and will be improved in the future.  The wait process is divided into three parts,
                // the first and third of which need to be non-reentrant, and the second of which
                // must be reentrant (and blocks).
                //
                qdpn_driver_wait_1(qd_server->driver);
                sys_mutex_unlock(qd_server->lock);

                do {
                    error = 0;
                    poll_result = qdpn_driver_wait_2(qd_server->driver, duration);
                    if (poll_result == -1)
                        error = errno;
                } while (error == EINTR);
                if (error) {
                    exit(-1);
                }

                sys_mutex_lock(qd_server->lock);
                qdpn_driver_wait_3(qd_server->driver);

                if (!thread->running) {
                    sys_mutex_unlock(qd_server->lock);
                    break;
                }

                //
                // Visit the timer module.
                //
                struct timespec tv;
                clock_gettime(CLOCK_REALTIME, &tv);
                qd_timestamp_t milliseconds = ((qd_timestamp_t)tv.tv_sec) * 1000 + tv.tv_nsec / 1000000;
                qd_timer_visit_LH(milliseconds);

                //
                // Process listeners (incoming connections).
                //
                thread_process_listeners_LH(qd_server);

                //
                // Traverse the list of connectors-needing-service from the proton driver.
                // If the connector is not already in the work queue and it is not currently
                // being processed by another thread, put it in the work queue and signal the
                // condition variable.
                //
                cxtr = qdpn_driver_connector(qd_server->driver);
                while (cxtr) {
                    ctx = qdpn_connector_context(cxtr);
                    if (!ctx->enqueued && ctx->owner_thread == CONTEXT_NO_OWNER) {
                        ctx->enqueued = 1;
                        qd_work_item_t *workitem = new_qd_work_item_t();
                        DEQ_ITEM_INIT(workitem);
                        workitem->cxtr = cxtr;
                        DEQ_INSERT_TAIL(qd_server->work_queue, workitem);
                        sys_cond_signal(qd_server->cond);
                    }
                    cxtr = qdpn_driver_connector(qd_server->driver);
                }

                //
                // Release our exclusive claim on qdpn_driver_wait.
                //
                qd_server->a_thread_is_waiting = false;
            }
        }

        //
        // If we were given a connector to work on from the work queue, mark it as
        // owned by this thread and as no longer enqueued.
        //
        cxtr = 0;
        if (work) {
            DEQ_REMOVE_HEAD(qd_server->work_queue);
            ctx = qdpn_connector_context(work->cxtr);
            if (ctx->owner_thread == CONTEXT_NO_OWNER) {
                ctx->owner_thread = thread->thread_id;
                ctx->enqueued = 0;
                qd_server->threads_active++;
                cxtr = work->cxtr;
                free_qd_work_item_t(work);
            } else {
                //
                // This connector is being processed by another thread, re-queue it.
                //
                DEQ_INSERT_TAIL(qd_server->work_queue, work);
            }
        }
        sys_mutex_unlock(qd_server->lock);

        //
        // Process the connector that we now have exclusive access to.
        //
        if (cxtr) {
            int work_done = 1;

            if (qdpn_connector_failed(cxtr))
                qdpn_connector_close(cxtr);

            //
            // Even if the connector has failed there are still events that 
            // must be processed so that associated links will be cleaned up.
            //
            work_done = process_connector(qd_server, cxtr);

            //
            // Check to see if the connector was closed during processing
            //
            if (qdpn_connector_closed(cxtr)) {
                qd_entity_cache_remove(QD_CONNECTION_TYPE, ctx);
                //
                // Connector is closed.  Free the context and the connector.
                // If this is a dispatch connector, schedule the re-connect timer
                //
                if (ctx->connector) {
                    ctx->connector->ctx = 0;
                    ctx->connector->state = CXTR_STATE_CONNECTING;
                    qd_timer_schedule(ctx->connector->timer, ctx->connector->delay);
                }

                sys_mutex_lock(qd_server->lock);
                DEQ_REMOVE(qd_server->connections, ctx);

                if (ctx->policy_counted) {
                    qd_policy_socket_close(qd_server->qd->policy, ctx);
                }

                qdpn_connector_free(cxtr);
                invoke_deferred_calls(ctx, true);  // Discard any pending deferred calls
                sys_mutex_free(ctx->deferred_call_lock);
                free_qd_connection(ctx);
                qd_server->threads_active--;
                sys_mutex_unlock(qd_server->lock);
            } else {
                //
                // The connector lives on.  Mark it as no longer owned by this thread.
                //
                sys_mutex_lock(qd_server->lock);
                ctx->owner_thread = CONTEXT_NO_OWNER;
                qd_server->threads_active--;
                sys_mutex_unlock(qd_server->lock);
            }

            //
            // Wake up the proton driver to force it to reconsider its set of FDs
            // in light of the processing that just occurred.
            //
            if (work_done)
                qdpn_driver_wakeup(qd_server->driver);
        }
    }

    return 0;
}


static void thread_start(qd_thread_t *thread)
{
    if (!thread)
        return;

    thread->using_thread = 1;
    thread->thread = sys_thread(thread_run, (void*) thread);
}


static void thread_cancel(qd_thread_t *thread)
{
    if (!thread)
        return;

    thread->running  = 0;
    thread->canceled = 1;
}


static void thread_join(qd_thread_t *thread)
{
    if (!thread)
        return;

    if (thread->using_thread) {
        sys_thread_join(thread->thread);
        sys_thread_free(thread->thread);
    }
}


static void thread_free(qd_thread_t *thread)
{
    if (!thread)
        return;

    free(thread);
}


static void cxtr_try_open(void *context)
{
    qd_connector_t *ct = (qd_connector_t*) context;
    if (ct->state != CXTR_STATE_CONNECTING)
        return;

    qd_connection_t *ctx = qd_connection_allocate();
    ctx->server       = ct->server;
    ctx->owner_thread = CONTEXT_UNSPECIFIED_OWNER;
    ctx->pn_conn      = pn_connection();
    ctx->collector    = pn_collector();
    ctx->connector    = ct;
    ctx->context      = ct->context;

    // Copy the role from the connector config
    int role_length    = strlen(ctx->connector->config->role) + 1;
    ctx->role          = (char*) malloc(role_length);
    strcpy(ctx->role, ctx->connector->config->role);

    qd_log(ct->server->log_source, QD_LOG_TRACE, "Connecting to %s:%s", ct->config->host, ct->config->port);

    pn_connection_collect(ctx->pn_conn, ctx->collector);
    decorate_connection(ctx->server, ctx->pn_conn, ct->config);

    //
    // qdpn_connector is not thread safe
    //
    sys_mutex_lock(ct->server->lock);
    // Increment the connection id so the next connection can use it
    ctx->connection_id = ct->server->next_connection_id++;
    ctx->pn_cxtr = qdpn_connector(ct->server->driver, ct->config->host, ct->config->port, ct->config->protocol_family, (void*) ctx);
    if (ctx->pn_cxtr) {
        DEQ_INSERT_TAIL(ct->server->connections, ctx);
        qd_entity_cache_add(QD_CONNECTION_TYPE, ctx);
    }
    sys_mutex_unlock(ct->server->lock);

    const qd_server_config_t *config = ct->config;

    if (ctx->pn_cxtr == 0) {
        sys_mutex_free(ctx->deferred_call_lock);
        free_qd_connection(ctx);
        ct->delay = 10000;
        qd_timer_schedule(ct->timer, ct->delay);
        return;
    }

    //
    // Set the hostname on the pn_connection. This hostname will be used by proton as the hostname in the open frame.
    //
    pn_connection_set_hostname(ctx->pn_conn, config->host);

    // Set the sasl user name and password on the proton connection object. This has to be done before the call to qdpn_connector_transport() which
    // binds the transport to the connection
    if(config->sasl_username)
        pn_connection_set_user(ctx->pn_conn, config->sasl_username);
    if (config->sasl_password)
        pn_connection_set_password(ctx->pn_conn, config->sasl_password);

    qdpn_connector_set_connection(ctx->pn_cxtr, ctx->pn_conn);
    pn_connection_set_context(ctx->pn_conn, ctx);

    ctx->connector->state = CXTR_STATE_OPEN;

    ct->ctx   = ctx;
    ct->delay = 5000;

    //
    // Set up the transport, SASL, and SSL for the connection.
    //
    pn_transport_t           *tport  = qdpn_connector_transport(ctx->pn_cxtr);

    //
    // Configure the transport
    //
    pn_transport_set_max_frame(tport, config->max_frame_size);
    pn_transport_set_idle_timeout(tport, config->idle_timeout_seconds * 1000);

    //
    // Proton pushes out its trace to qd_transport_tracer() which in turn writes a trace message to the qdrouter log
    //
    // If trace level logging is enabled on the router set PN_TRACE_DRV | PN_TRACE_FRM | PN_TRACE_RAW on the proton transport
    pn_transport_set_context(tport, ctx);
    if (qd_log_enabled(ct->server->log_source, QD_LOG_TRACE)) {
        pn_transport_trace(tport, PN_TRACE_DRV | PN_TRACE_FRM | PN_TRACE_RAW);
        pn_transport_set_tracer(tport, qd_transport_tracer);
    }

    //
    // Set up SSL if appropriate
    //
    if (config->ssl_profile) {
        pn_ssl_domain_t *domain = pn_ssl_domain(PN_SSL_MODE_CLIENT);

        if (!domain) {
            qd_error(QD_ERROR_RUNTIME, "SSL domain failed for connection to %s:%s",
                     ct->config->host, ct->config->port);
            /* TODO aconway 2014-07-15: Close the connection, clean up. */
            return;
        }

        /* TODO aconway 2014-07-15: error handling on all SSL calls. */

        // set our trusted database for checking the peer's cert:
        if (config->ssl_trusted_certificate_db) {
            if (pn_ssl_domain_set_trusted_ca_db(domain, config->ssl_trusted_certificate_db)) {
                qd_log(ct->server->log_source, QD_LOG_ERROR,
                       "SSL CA configuration failed for %s:%s",
                       ct->config->host, ct->config->port);
            }
        }
        // should we force the peer to provide a cert?
        if (config->ssl_require_peer_authentication) {
            const char *trusted = (config->ssl_trusted_certificates)
                ? config->ssl_trusted_certificates
                : config->ssl_trusted_certificate_db;
            if (pn_ssl_domain_set_peer_authentication(domain,
                                                      PN_SSL_VERIFY_PEER,
                                                      trusted)) {
                qd_log(ct->server->log_source, QD_LOG_ERROR,
                       "SSL peer auth configuration failed for %s:%s",
                       ct->config->host, ct->config->port);
            }
        }

        // configure our certificate if the peer requests one:
        if (config->ssl_certificate_file) {
            if (pn_ssl_domain_set_credentials(domain,
                                              config->ssl_certificate_file,
                                              config->ssl_private_key_file,
                                              config->ssl_password)) {
                qd_log(ct->server->log_source, QD_LOG_ERROR,
                       "SSL local configuration failed for %s:%s",
                       ct->config->host, ct->config->port);
            }
        }

        //If ssl is enabled and verify_host_name is true, instruct proton to verify peer name
        if (config->verify_host_name) {
            if (pn_ssl_domain_set_peer_authentication(domain, PN_SSL_VERIFY_PEER_NAME, NULL)) {
                    qd_log(ct->server->log_source, QD_LOG_ERROR,
                           "SSL peer host name verification failed for %s:%s",
                           ct->config->host, ct->config->port);
            }
        }

        ctx->ssl = pn_ssl(tport);
        pn_ssl_init(ctx->ssl, domain, 0);
        pn_ssl_domain_free(domain);
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

    ctx->owner_thread = CONTEXT_NO_OWNER;
}


static void heartbeat_cb(void *context)
{
    qd_server_t *qd_server = (qd_server_t*) context;
    qdpn_activate_all(qd_server->driver);
    qd_timer_schedule(qd_server->heartbeat_timer, HEARTBEAT_INTERVAL);
}


qd_server_t *qd_server(qd_dispatch_t *qd, int thread_count, const char *container_name,
                       const char *sasl_config_path, const char *sasl_config_name)
{
    int i;

    qd_server_t *qd_server = NEW(qd_server_t);
    if (qd_server == 0)
        return 0;

    DEQ_INIT(qd_server->connections);
    qd_server->qd               = qd;
    qd_server->log_source       = qd_log_source("SERVER");
    qd_server->thread_count     = thread_count;
    qd_server->container_name   = container_name;
    qd_server->sasl_config_path = sasl_config_path;
    qd_server->sasl_config_name = sasl_config_name;
    qd_server->driver           = qdpn_driver();
    qd_server->start_handler    = 0;
    qd_server->conn_handler     = 0;
    qd_server->pn_event_handler = 0;
    qd_server->signal_handler   = 0;
    qd_server->ufd_handler      = 0;
    qd_server->start_context    = 0;
    qd_server->signal_context   = 0;
    qd_server->lock             = sys_mutex();
    qd_server->cond             = sys_cond();

    qd_timer_initialize(qd_server->lock);

    qd_server->threads = NEW_PTR_ARRAY(qd_thread_t, thread_count);
    for (i = 0; i < thread_count; i++)
        qd_server->threads[i] = thread(qd_server, i);

    DEQ_INIT(qd_server->work_queue);
    DEQ_INIT(qd_server->pending_timers);
    qd_server->a_thread_is_waiting    = false;
    qd_server->threads_active         = 0;
    qd_server->pause_requests         = 0;
    qd_server->threads_paused         = 0;
    qd_server->pause_next_sequence    = 0;
    qd_server->pause_now_serving      = 0;
    qd_server->pending_signal         = 0;
    qd_server->signal_handler_running = false;
    qd_server->heartbeat_timer        = 0;
    qd_server->next_connection_id     = 1;
    qd_server->py_displayname_module  = 0;
    qd_server->py_displayname_obj     = 0;

    qd_log(qd_server->log_source, QD_LOG_INFO, "Container Name: %s", qd_server->container_name);

    return qd_server;
}


void qd_server_free(qd_server_t *qd_server)
{
    if (!qd_server) return;
    for (int i = 0; i < qd_server->thread_count; i++)
        thread_free(qd_server->threads[i]);
    qd_timer_finalize();
    qdpn_driver_free(qd_server->driver);
    sys_mutex_free(qd_server->lock);
    sys_cond_free(qd_server->cond);
    free(qd_server->threads);
    free(qd_server);
}


void qd_server_set_conn_handler(qd_dispatch_t            *qd,
                                qd_conn_handler_cb_t      handler,
                                qd_pn_event_handler_cb_t  pn_event_handler,
                                void                     *handler_context)
{
    qd->server->conn_handler         = handler;
    qd->server->pn_event_handler     = pn_event_handler;
    qd->server->conn_handler_context = handler_context;
}


void qd_server_set_signal_handler(qd_dispatch_t *qd, qd_signal_handler_cb_t handler, void *context)
{
    qd->server->signal_handler = handler;
    qd->server->signal_context = context;
}


void qd_server_set_start_handler(qd_dispatch_t *qd, qd_thread_start_cb_t handler, void *context)
{
    qd->server->start_handler = handler;
    qd->server->start_context = context;
}


void qd_server_set_user_fd_handler(qd_dispatch_t *qd, qd_user_fd_handler_cb_t ufd_handler)
{
    qd->server->ufd_handler = ufd_handler;
}


static void qd_server_announce(qd_server_t* qd_server)
{
    qd_log(qd_server->log_source, QD_LOG_INFO, "Operational, %d Threads Running", qd_server->thread_count);
#ifndef NDEBUG
    qd_log(qd_server->log_source, QD_LOG_INFO, "Running in DEBUG Mode");
#endif
}


void qd_server_run(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;

    int i;
    if (!qd_server)
        return;

    assert(qd_server->conn_handler); // Server can't run without a connection handler.

    for (i = 1; i < qd_server->thread_count; i++)
        thread_start(qd_server->threads[i]);

    qd_server->heartbeat_timer = qd_timer(qd, heartbeat_cb, qd_server);
    qd_timer_schedule(qd_server->heartbeat_timer, HEARTBEAT_INTERVAL);

    qd_server_announce(qd_server);

    thread_run((void*) qd_server->threads[0]);

    for (i = 1; i < qd_server->thread_count; i++)
        thread_join(qd_server->threads[i]);

    for (i = 0; i < qd_server->thread_count; i++)
        qd_server->threads[i]->canceled = 0;

    qd_log(qd_server->log_source, QD_LOG_INFO, "Shut Down");
}


void qd_server_start(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;
    int i;

    if (!qd_server)
        return;

    assert(qd_server->conn_handler); // Server can't run without a connection handler.

    for (i = 0; i < qd_server->thread_count; i++)
        thread_start(qd_server->threads[i]);

    qd_server->heartbeat_timer = qd_timer(qd, heartbeat_cb, qd_server);
    qd_timer_schedule(qd_server->heartbeat_timer, HEARTBEAT_INTERVAL);

    qd_server_announce(qd_server);
}


void qd_server_stop(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;
    int idx;

    sys_mutex_lock(qd_server->lock);
    for (idx = 0; idx < qd_server->thread_count; idx++)
        thread_cancel(qd_server->threads[idx]);
    sys_cond_signal_all(qd_server->cond);
    qdpn_driver_wakeup(qd_server->driver);
    sys_mutex_unlock(qd_server->lock);

    if (thread_server != qd_server) {
        for (idx = 0; idx < qd_server->thread_count; idx++)
            thread_join(qd_server->threads[idx]);
        qd_log(qd_server->log_source, QD_LOG_INFO, "Shut Down");
    }
}


void qd_server_signal(qd_dispatch_t *qd, int signum)
{
    if (!qd)
        return;

    qd_server_t *qd_server = qd->server;

    qd_server->pending_signal = signum;
    sys_cond_signal_all(qd_server->cond);
    qdpn_driver_wakeup(qd_server->driver);
}


void qd_server_pause(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;

    sys_mutex_lock(qd_server->lock);

    //
    // Bump the request count to stop all the threads.
    //
    qd_server->pause_requests++;
    int my_sequence = qd_server->pause_next_sequence++;

    //
    // Awaken all threads that are currently blocking.
    //
    sys_cond_signal_all(qd_server->cond);
    qdpn_driver_wakeup(qd_server->driver);

    //
    // Wait for the paused thread count plus the number of threads requesting a pause to equal
    // the total thread count.  Also, don't exit the blocking loop until now_serving equals our
    // sequence number.  This ensures that concurrent pausers don't run at the same time.
    //
    while ((qd_server->threads_paused + qd_server->pause_requests < qd_server->thread_count) ||
           (my_sequence != qd_server->pause_now_serving))
        sys_cond_wait(qd_server->cond, qd_server->lock);

    sys_mutex_unlock(qd_server->lock);
}


void qd_server_resume(qd_dispatch_t *qd)
{
    qd_server_t *qd_server = qd->server;

    sys_mutex_lock(qd_server->lock);
    qd_server->pause_requests--;
    qd_server->pause_now_serving++;
    sys_cond_signal_all(qd_server->cond);
    sys_mutex_unlock(qd_server->lock);
}


void qd_server_activate(qd_connection_t *ctx, bool awaken)
{
    if (!ctx)
        return;

    qdpn_connector_t *ctor = ctx->pn_cxtr;
    if (!ctor)
        return;

    if (!qdpn_connector_closed(ctor)) {
        qdpn_connector_activate(ctor, QDPN_CONNECTOR_WRITABLE);
        if (awaken)
            qdpn_driver_wakeup(ctx->server->driver);
    }
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


pn_collector_t *qd_connection_collector(qd_connection_t *conn)
{
    return conn->collector;
}

uint64_t qd_connection_connection_id(qd_connection_t *conn)
{
    return conn->connection_id;
}


const qd_server_config_t *qd_connection_config(const qd_connection_t *conn)
{
    if (conn->listener)
        return conn->listener->config;
    return conn->connector->config;
}


void qd_connection_invoke_deferred(qd_connection_t *conn, qd_deferred_t call, void *context)
{
    qd_deferred_call_t *dc = new_qd_deferred_call_t();
    DEQ_ITEM_INIT(dc);
    dc->call    = call;
    dc->context = context;

    sys_mutex_lock(conn->deferred_call_lock);
    DEQ_INSERT_TAIL(conn->deferred_calls, dc);
    sys_mutex_unlock(conn->deferred_call_lock);

    qd_server_activate(conn, true);
}


void qd_connection_set_event_stall(qd_connection_t *conn, bool stall)
{
    conn->event_stall = stall;
     if (!stall)
         qd_server_activate(conn, true);
}


qd_listener_t *qd_server_listen(qd_dispatch_t *qd, const qd_server_config_t *config, void *context)
{
    qd_server_t   *qd_server = qd->server;
    qd_listener_t *li        = new_qd_listener_t();

    if (!li)
        return 0;

    li->server      = qd_server;
    li->config      = config;
    li->context     = context;
    li->pn_listener = qdpn_listener(qd_server->driver, config->host, config->port, config->protocol_family, (void*) li);

    if (!li->pn_listener) {
        free_qd_listener_t(li);
        return 0;
    }
    qd_log(qd_server->log_source, QD_LOG_TRACE, "Listening on %s:%s", config->host, config->port);

    return li;
}


void qd_server_listener_free(qd_listener_t* li)
{
    if (!li)
        return;

    qdpn_listener_free(li->pn_listener);
    free_qd_listener_t(li);
}


void qd_server_listener_close(qd_listener_t* li)
{
    if (li)
        qdpn_listener_close(li->pn_listener);
}


qd_connector_t *qd_server_connect(qd_dispatch_t *qd, const qd_server_config_t *config, void *context)
{
    qd_server_t    *qd_server = qd->server;
    qd_connector_t *ct        = new_qd_connector_t();

    if (!ct)
        return 0;

    ct->server  = qd_server;
    ct->state   = CXTR_STATE_CONNECTING;
    ct->config  = config;
    ct->context = context;
    ct->ctx     = 0;
    ct->timer   = qd_timer(qd, cxtr_try_open, (void*) ct);
    ct->delay   = 0;

    qd_timer_schedule(ct->timer, ct->delay);
    return ct;
}


void qd_server_connector_free(qd_connector_t* ct)
{
    // Don't free the proton connector.  This will be done by the connector
    // processing/cleanup.

    if (!ct)
        return;

    if (ct->ctx) {
        qdpn_connector_close(ct->ctx->pn_cxtr);
        ct->ctx->connector = 0;
    }

    qd_timer_free(ct->timer);
    free_qd_connector_t(ct);
}


qd_user_fd_t *qd_user_fd(qd_dispatch_t *qd, int fd, void *context)
{
    qd_server_t  *qd_server = qd->server;
    qd_user_fd_t *ufd       = new_qd_user_fd_t();

    if (!ufd)
        return 0;

    qd_connection_t *ctx = qd_connection_allocate();
    ctx->server       = qd_server;
    ctx->owner_thread = CONTEXT_NO_OWNER;
    ctx->ufd          = ufd;

    // Copy the role from the connector config
    if (ctx->connector && ctx->connector->config) {
        int role_length    = strlen(ctx->connector->config->role) + 1;
        ctx->role          = (char*) malloc(role_length);
        strcpy(ctx->role, ctx->connector->config->role);
    }

    ufd->context = context;
    ufd->server  = qd_server;
    ufd->fd      = fd;
    ufd->pn_conn = qdpn_connector_fd(qd_server->driver, fd, (void*) ctx);
    qdpn_driver_wakeup(qd_server->driver);

    return ufd;
}


void qd_user_fd_free(qd_user_fd_t *ufd)
{
    if (!ufd) return;
    qdpn_connector_close(ufd->pn_conn);
    free_qd_user_fd_t(ufd);
}


void qd_user_fd_activate_read(qd_user_fd_t *ufd)
{
    qdpn_connector_activate(ufd->pn_conn, QDPN_CONNECTOR_READABLE);
    qdpn_driver_wakeup(ufd->server->driver);
}


void qd_user_fd_activate_write(qd_user_fd_t *ufd)
{
    qdpn_connector_activate(ufd->pn_conn, QDPN_CONNECTOR_WRITABLE);
    qdpn_driver_wakeup(ufd->server->driver);
}


bool qd_user_fd_is_readable(qd_user_fd_t *ufd)
{
    return qdpn_connector_activated(ufd->pn_conn, QDPN_CONNECTOR_READABLE);
}


bool qd_user_fd_is_writeable(qd_user_fd_t *ufd)
{
    return qdpn_connector_activated(ufd->pn_conn, QDPN_CONNECTOR_WRITABLE);
}


void qd_server_timer_pending_LH(qd_timer_t *timer)
{
    DEQ_INSERT_TAIL(timer->server->pending_timers, timer);
    qdpn_driver_wakeup(timer->server->driver);
}


void qd_server_timer_cancel_LH(qd_timer_t *timer)
{
    DEQ_REMOVE(timer->server->pending_timers, timer);
}
