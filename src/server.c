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

#define HEARTBEAT_INTERVAL 1000

ALLOC_DEFINE(qd_listener_t);
ALLOC_DEFINE(qd_connector_t);
ALLOC_DEFINE(qd_deferred_call_t);
ALLOC_DEFINE(qd_connection_t);

const char *MECH_EXTERNAL = "EXTERNAL";

// Allowed uidFormat fields
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

//
// Tracer functions
//

// This function is set as the pn_transport->tracer and is invoked
// when proton tries to write the log message to pn_transport->tracer
void transport_tracer(pn_transport_t *transport, const char *message) {
    qd_connection_t *conn = (qd_connection_t*) pn_transport_get_context(transport);

    if (conn) {
        // The PROTOCOL module is used exclusively for logging
        // protocol related tracing. The protocol could be AMQP, HTTP,
        // TCP, etc.
        qd_log(conn->server->protocol_log_source, QD_LOG_TRACE, "[C%"PRIu64"]:%s", conn->connection_id, message);
    }
}

void connection_transport_tracer(pn_transport_t *transport, const char *message) {
    qd_connection_t *conn = (qd_connection_t*) pn_transport_get_context(transport);

    if (conn) {
        // Unconditionally write the log at TRACE level to the log
        // file
        qd_log_impl_v1(conn->server->protocol_log_source, QD_LOG_TRACE,  __FILE__, __LINE__, "[C%"PRIu64"]:%s",
                       conn->connection_id, message);
    }
}

// Save displayNameService object instance and ImportModule address.
// Called with qd_python_lock held.
//
// XXX But it doesn't use the _lh convention?
qd_error_t qd_register_display_name_service(qd_dispatch_t *qd, void *displaynameservice) {
    if (displaynameservice) {
        qd->server->py_displayname_obj = displaynameservice;
        Py_XINCREF((PyObject *)qd->server->py_displayname_obj);
        return QD_ERROR_NONE;
    } else {
        return qd_error(QD_ERROR_VALUE, "displaynameservice is not set");
    }
}

qd_error_t qd_entity_refresh_sslProfile(qd_entity_t* entity, void *impl) {
    return QD_ERROR_NONE;
}

qd_error_t qd_entity_refresh_authServicePlugin(qd_entity_t* entity, void *impl) {
    return QD_ERROR_NONE;
}

//
// Connection functions
//

static void decorate_connection(qd_server_t *server, qd_connection_t *conn, const qd_server_config_t *config);
static void connection_wake(qd_connection_t *conn);

// Construct a new qd_connection. Thread safe.
qd_connection_t *qd_server_connection(qd_server_t *server, qd_server_config_t *config) {
    qd_connection_t *conn = new_qd_connection_t();

    if (!conn) {
        return NULL;
    }

    ZERO(conn);

    conn->pn_conn = pn_connection();
    conn->deferred_call_lock = sys_mutex();
    conn->role = strdup(config->role);

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

    decorate_connection(conn->server, conn, config);

    return conn;
}

void invoke_deferred_calls(qd_connection_t *conn, bool discard);
bool qd_connector_has_failover_info(qd_connector_t* connector);

void qd_connection_free(qd_connection_t *conn) {
    qd_server_t *server = conn->server;

    // If this is a dispatch connector, schedule the re-connect timer
    if (conn->connector) {
        long delay = conn->connector->delay;

        sys_mutex_lock(conn->connector->lock);

        conn->connector->ctx = NULL;
        // Increment the connection index by so that we can try connecting to the failover url (if any).
        bool has_failover = qd_connector_has_failover_info(conn->connector);

        if (has_failover) {
            // Go thru the failover list round robin.
            // IMPORTANT: Note here that we set the re-try timer to 1 second.
            // We want to quickly keep cycling thru the failover urls every second.
            delay = 1000;
        }

        conn->connector->state = CXTR_STATE_CONNECTING;

        sys_mutex_unlock(conn->connector->lock);

        // Increment the ref-count to account for the timer's reference to the connector.
        sys_atomic_inc(&conn->connector->ref_count);
        qd_timer_schedule(conn->connector->timer, delay);
    }

    sys_mutex_lock(server->lock);

    DEQ_REMOVE(server->conn_list, conn);

    // If counted for policy enforcement, notify it has closed
    if (conn->policy_counted) {
        qd_policy_socket_close(server->qd->policy, conn);
    }

    sys_mutex_unlock(server->lock);

    // Discard any pending deferred calls
    invoke_deferred_calls(conn, true);

    sys_mutex_free(conn->deferred_call_lock);
    qd_policy_settings_free(conn->policy_settings);
    if (conn->free_user_id) free((char*)conn->user_id);
    if (conn->timer) qd_timer_free(conn->timer);
    free(conn->name);
    free(conn->role);

    sys_mutex_lock(server->conn_activation_lock);
    free_qd_connection_t(conn);
    sys_mutex_unlock(server->conn_activation_lock);

    // Note: pn_conn is freed by the proactor
}

static const char *transport_get_user(qd_connection_t *conn, pn_transport_t *transport);

void qd_connection_set_user(qd_connection_t *conn) {
    pn_transport_t *transport = pn_connection_transport(conn->pn_conn);
    pn_sasl_t *sasl  = pn_sasl(transport);

    if (sasl) {
        const char *mech = pn_sasl_get_mech(sasl);
        conn->user_id = pn_transport_get_user(transport);

        // We want to set the user name only if it is not already set and the selected sasl mechanism is EXTERNAL
        if (mech && strcmp(mech, MECH_EXTERNAL) == 0) {
            const char *user_id = transport_get_user(conn, transport);

            if (user_id) {
                conn->user_id = user_id;
            }
        }
    }
}

void qd_connection_set_context(qd_connection_t *conn, void *context) {
    conn->user_context = context;
}

void *qd_connection_get_context(qd_connection_t *conn) {
    return conn->user_context;
}

void *qd_connection_get_config_context(qd_connection_t *conn) {
    return conn->context;
}

void qd_connection_set_link_context(qd_connection_t *conn, void *context) {
    conn->link_context = context;
}

void *qd_connection_get_link_context(qd_connection_t *conn) {
    return conn->link_context;
}

pn_connection_t *qd_connection_pn(qd_connection_t *conn) {
    return conn->pn_conn;
}

bool qd_connection_inbound(qd_connection_t *conn) {
    return conn->listener != NULL;
}

uint64_t qd_connection_connection_id(qd_connection_t *conn) {
    return conn->connection_id;
}

const qd_server_config_t *qd_connection_config(const qd_connection_t *conn) {
    if (conn->listener) {
        return &conn->listener->config;
    }

    if (conn->connector) {
        return &conn->connector->config;
    }

    return NULL;
}

void qd_connection_invoke_deferred(qd_connection_t *conn, qd_deferred_t call, void *context) {
    // XXX Is this really what we want?
    if (!conn) {
        return;
    }

    qd_deferred_call_t *dc = new_qd_deferred_call_t();
    DEQ_ITEM_INIT(dc);
    dc->call = call;
    dc->context = context;

    sys_mutex_lock(conn->deferred_call_lock);
    DEQ_INSERT_TAIL(conn->deferred_calls, dc);
    sys_mutex_unlock(conn->deferred_call_lock);

    qd_server_activate(conn);
}

const char *qd_connection_remote_ip(const qd_connection_t *conn) {
    return conn->rhost;
}

const char *qd_connection_name(const qd_connection_t *conn) {
    if (conn->connector) {
        return conn->connector->config.host_port;
    } else {
        return conn->rhost_port;
    }
}

qd_connector_t *qd_connection_connector(const qd_connection_t *conn) {
    return conn->connector;
}

// The entry point for the handlers defined in server_event_handlers.c
bool handle_event(qd_server_t *server, pn_event_t *event);

// Expose event handling for HTTP connections
bool qd_connection_handle(qd_connection_t *conn, pn_event_t *event) {
    // XXX What's going on with conn versus qd_conn here?

    if (!conn) {
        return false;
    }

    pn_connection_t *pn_conn = pn_event_connection(event);
    qd_connection_t *qd_conn = !!pn_conn ? (qd_connection_t*) pn_connection_get_context(pn_conn) : NULL;

    handle_event(conn->server, event);

    if (qd_conn && pn_event_type(event) == PN_TRANSPORT_CLOSED) {
        pn_connection_set_context(pn_conn, NULL);
        qd_connection_free(qd_conn);
        return false;
    }

    return true;
}

bool qd_connection_strip_annotations_in(const qd_connection_t *conn) {
    return conn->strip_annotations_in;
}

uint64_t qd_connection_max_message_size(const qd_connection_t *conn) {
    if  (conn && conn->policy_settings) {
        return conn->policy_settings->maxMessageSize;
    }

    return 0;
}

static void decorate_connection(qd_server_t *server, qd_connection_t *conn, const qd_server_config_t *config) {
    pn_connection_t *pn_conn = conn->pn_conn;

    // Set the container name
    pn_connection_set_container(pn_conn, server->container_name);

    // Offer ANONYMOUS_RELAY capability
    size_t clen = strlen(QD_CAPABILITY_ANONYMOUS_RELAY);
    pn_data_put_symbol(pn_connection_offered_capabilities(pn_conn), pn_bytes(clen, (char*) QD_CAPABILITY_ANONYMOUS_RELAY));

    // Create the connection properties map

    pn_data_t *props = pn_connection_properties(pn_conn);

    pn_data_put_map(props);
    pn_data_enter(props);

    pn_data_put_symbol(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_PRODUCT_KEY), QD_CONNECTION_PROPERTY_PRODUCT_KEY));
    pn_data_put_string(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_PRODUCT_VALUE), QD_CONNECTION_PROPERTY_PRODUCT_VALUE));

    pn_data_put_symbol(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_VERSION_KEY), QD_CONNECTION_PROPERTY_VERSION_KEY));
    pn_data_put_string(props, pn_bytes(strlen(QPID_DISPATCH_VERSION), QPID_DISPATCH_VERSION));

    pn_data_put_symbol(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_CONN_ID), QD_CONNECTION_PROPERTY_CONN_ID));
    pn_data_put_int(props, conn->connection_id);

    // XXX When is config going to be null?
    if (config && config->inter_router_cost > 1) {
        pn_data_put_symbol(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_COST_KEY), QD_CONNECTION_PROPERTY_COST_KEY));
        pn_data_put_int(props, config->inter_router_cost);
    }

    if (config) {
        qd_failover_list_t *fol = config->failover_list;

        if (fol) {
            pn_data_put_symbol(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY),
                                               QD_CONNECTION_PROPERTY_FAILOVER_LIST_KEY));
            pn_data_put_list(props);

            pn_data_enter(props);

            int fol_count = qd_failover_list_size(fol);

            for (int i = 0; i < fol_count; i++) {
                pn_data_put_map(props);
                pn_data_enter(props);

                pn_data_put_symbol(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_NETHOST_KEY),
                                                   QD_CONNECTION_PROPERTY_FAILOVER_NETHOST_KEY));
                pn_data_put_string(props, pn_bytes(strlen(qd_failover_list_host(fol, i)), qd_failover_list_host(fol, i)));

                pn_data_put_symbol(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_PORT_KEY),
                                                   QD_CONNECTION_PROPERTY_FAILOVER_PORT_KEY));
                pn_data_put_string(props, pn_bytes(strlen(qd_failover_list_port(fol, i)), qd_failover_list_port(fol, i)));

                if (qd_failover_list_scheme(fol, i)) {
                    pn_data_put_symbol(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_SCHEME_KEY),
                                                       QD_CONNECTION_PROPERTY_FAILOVER_SCHEME_KEY));
                    pn_data_put_string(props, pn_bytes(strlen(qd_failover_list_scheme(fol, i)), qd_failover_list_scheme(fol, i)));
                }

                if (qd_failover_list_hostname(fol, i)) {
                    pn_data_put_symbol(props, pn_bytes(strlen(QD_CONNECTION_PROPERTY_FAILOVER_HOSTNAME_KEY),
                                                       QD_CONNECTION_PROPERTY_FAILOVER_HOSTNAME_KEY));
                    pn_data_put_string(props, pn_bytes(strlen(qd_failover_list_hostname(fol, i)), qd_failover_list_hostname(fol, i)));
                }

                pn_data_exit(props);
            }

            pn_data_exit(props);
        }
    }

    pn_data_exit(props);
}

// Wake function for proactor-managed connections
static void connection_wake(qd_connection_t *conn) {
    assert(conn);

    pn_connection_wake(conn->pn_conn);
}

// Returns a char pointer to a user id which is constructed from
// components specified in the config->ssl_uid_format.  Parses through
// each component and builds a semi-colon delimited string which is
// returned as the user id.
static const char *transport_get_user(qd_connection_t *conn, pn_transport_t *transport) {
    const qd_server_config_t *config =
            conn->connector ? &conn->connector->config : &conn->listener->config;

    if (config->ssl_uid_format) {
        // The ssl_uid_format length cannot be greater that 7
        assert(strlen(config->ssl_uid_format) < 8);

        // The tokens in the uidFormat strings are delimited by
        // comma. Load the individual components of the uidFormat into
        // the components[] array. The maximum number of components
        // that are allowed are 7 namely, c, s, l, o, u, n, (1 or 2 or
        // 5).
        char components[8];

        // The strcpy() function copies the string pointed to by src,
        // including the terminating null byte ('\0'), to the buffer
        // pointed to by dest
        strncpy(components, config->ssl_uid_format, 7);

        const char *country_code = 0;
        const char *state = 0;
        const char *locality_city = 0;
        const char *organization = 0;
        const char *org_unit = 0;
        const char *common_name = 0;

        // SHA1 is 20 octets (40 hex characters); SHA256 is 32 octets (64 hex characters).
        // SHA512 is 64 octets (128 hex characters)
        char fingerprint[129] = "\0";

        int uid_length = 0;
        int semi_colon_count = -1;
        int component_count = strlen(components);

        for (int x = 0; x < component_count; x++) {
            // accumulate the length into uid_length on each pass so we definitively know the number of octets to malloc.
            if (components[x] == CERT_COUNTRY_CODE) {
                country_code =  pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_COUNTRY_NAME);

                if (country_code) {
                    uid_length += strlen((const char *)country_code);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_STATE) {
                state =  pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_STATE_OR_PROVINCE);

                if (state) {
                    uid_length += strlen((const char *)state);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_CITY_LOCALITY) {
                locality_city =  pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_CITY_OR_LOCALITY);
                if (locality_city) {
                    uid_length += strlen((const char *)locality_city);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_ORGANIZATION_NAME) {
                organization =  pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_ORGANIZATION_NAME);
                if (organization) {
                    uid_length += strlen((const char *)organization);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_ORGANIZATION_UNIT) {
                org_unit =  pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_ORGANIZATION_UNIT);
                if (org_unit) {
                    uid_length += strlen((const char *)org_unit);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_COMMON_NAME) {
                common_name =  pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_COMMON_NAME);
                if (common_name) {
                    uid_length += strlen((const char *)common_name);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_FINGERPRINT_SHA1 || components[x] == CERT_FINGERPRINT_SHA256 ||
                       components[x] == CERT_FINGERPRINT_SHA512) {
                // Allocate the memory for message digest
                int out = 0;
                int fingerprint_length = 0;

                if (components[x] == CERT_FINGERPRINT_SHA1) {
                    fingerprint_length = 40;
                    out = pn_ssl_get_cert_fingerprint(pn_ssl(transport), fingerprint, fingerprint_length + 1, PN_SSL_SHA1);
                } else if (components[x] == CERT_FINGERPRINT_SHA256) {
                    fingerprint_length = 64;
                    out = pn_ssl_get_cert_fingerprint(pn_ssl(transport), fingerprint, fingerprint_length + 1, PN_SSL_SHA256);
                } else if (components[x] == CERT_FINGERPRINT_SHA512) {
                    fingerprint_length = 128;
                    out = pn_ssl_get_cert_fingerprint(pn_ssl(transport), fingerprint, fingerprint_length + 1, PN_SSL_SHA512);
                }

                // Avoid 'out unused' compiler warnings if NDEBUG undef'ed
                (void) out;

                assert (out != PN_ERR);

                uid_length += fingerprint_length;
                semi_colon_count++;
            } else {
                // This is an unrecognized component.  Log a critical
                // error.
                qd_log(conn->server->log_source, QD_LOG_CRITICAL, "Unrecognized component '%c' in uidFormat ", components[x]);
                return 0;
            }
        }

        if (uid_length > 0) {
            char *user_id = malloc((uid_length + semi_colon_count + 1) * sizeof(char)); // the +1 is for the '\0' character
            //
            // We have allocated memory for user_id. We are responsible for freeing this memory. Set conn->free_user_id
            // to true so that we know that we have to free the user_id
            //
            conn->free_user_id = true;
            memset(user_id, 0, uid_length + semi_colon_count + 1);

            // The components in the user id string must appear in the same order as it appears in the component string. that is
            // why we have this loop
            for (int x = 0; x < component_count ; x++) {
                if (components[x] == CERT_COUNTRY_CODE) {
                    if (country_code) {
                        if (*user_id != '\0') {
                            strcat(user_id, COMPONENT_SEPARATOR);
                        }

                        strcat(user_id, (char *) country_code);
                    }
                } else if (components[x] == CERT_STATE) {
                    if (state) {
                        if (*user_id != '\0') {
                            strcat(user_id, COMPONENT_SEPARATOR);
                        }

                        strcat(user_id, (char *) state);
                    }
                } else if (components[x] == CERT_CITY_LOCALITY) {
                    if (locality_city) {
                        if (*user_id != '\0') {
                            strcat(user_id, COMPONENT_SEPARATOR);
                        }

                        strcat(user_id, (char *) locality_city);
                    }
                } else if (components[x] == CERT_ORGANIZATION_NAME) {
                    if (organization) {
                        if (*user_id != '\0') {
                            strcat(user_id, COMPONENT_SEPARATOR);
                        }

                        strcat(user_id, (char *) organization);
                    }
                } else if (components[x] == CERT_ORGANIZATION_UNIT) {
                    if (org_unit) {
                        if (*user_id != '\0') {
                            strcat(user_id, COMPONENT_SEPARATOR);
                        }

                        strcat(user_id, (char *) org_unit);
                    }
                } else if (components[x] == CERT_COMMON_NAME) {
                    if (common_name) {
                        if (*user_id != '\0') {
                            strcat(user_id, COMPONENT_SEPARATOR);
                        }

                        strcat(user_id, (char *) common_name);
                    }
                } else if (components[x] == CERT_FINGERPRINT_SHA1 || components[x] == CERT_FINGERPRINT_SHA256 || components[x] == CERT_FINGERPRINT_SHA512) {
                    if (strlen((char *) fingerprint) > 0) {
                        if (*user_id != '\0') {
                            strcat(user_id, COMPONENT_SEPARATOR);
                        }

                        strcat(user_id, (char *) fingerprint);
                    }
                }
            }

            if (config->ssl_uid_name_mapping_file) {
                // Translate extracted id into display name

                qd_python_lock_state_t lock_state = qd_python_lock();
                PyObject *result = PyObject_CallMethod((PyObject *) conn->server->py_displayname_obj,
                                                       "query", "(ss)", config->ssl_profile, user_id );

                if (result) {
                    free(user_id);
                    user_id = py_string_2_c(result);
                    Py_XDECREF(result);
                } else {
                    qd_log(conn->server->log_source, QD_LOG_DEBUG, "Internal: failed to read displaynameservice query result");
                }

                qd_python_unlock(lock_state);
            }

            qd_log(conn->server->log_source, QD_LOG_DEBUG, "User id is '%s' ", user_id);

            return user_id;
        }
    } else {
        // config->ssl_uid_format not specified.  Just return the
        // username provided by the proton transport.
        return pn_transport_get_user(transport);
    }

    return 0;
}

//
// Server functions
//

qd_server_t *qd_server(qd_dispatch_t *qd, int thread_count, const char *container_name,
                       const char *sasl_config_path, const char *sasl_config_name) {
    // Initialize const members. Zero initialize all others.

    qd_server_t tmp = { .thread_count = thread_count };
    // XXX This type contructor is different from what I see elsewhere.
    qd_server_t *server = NEW(qd_server_t);

    if (!server) {
        return NULL;
    }

    memcpy(server, &tmp, sizeof(tmp));

    server->qd                     = qd;
    server->log_source             = qd_log_source("SERVER");
    server->protocol_log_source    = qd_log_source("PROTOCOL");
    server->container_name         = container_name;
    server->sasl_config_path       = sasl_config_path;
    server->sasl_config_name       = sasl_config_name;
    server->proactor               = pn_proactor();
    server->container              = NULL;
    server->start_context          = 0; // XXX Should be NULL?
    server->lock                   = sys_mutex();
    server->conn_activation_lock   = sys_mutex();
    server->cond                   = sys_cond();

    DEQ_INIT(server->conn_list);
    qd_timer_initialize(server->lock);

    server->pause_requests         = 0;
    server->threads_paused         = 0;
    server->pause_next_sequence    = 0;
    server->pause_now_serving      = 0;
    server->next_connection_id     = 1;
    server->py_displayname_obj     = 0;

    server->http = qd_http_server(server, server->log_source);

    qd_log(server->log_source, QD_LOG_INFO, "Container Name: %s", server->container_name);

    return server;
}

void qd_server_free(qd_server_t *server) {
    if (!server) {
        return;
    }

    qd_connection_t *conn = DEQ_HEAD(server->conn_list);

    while (conn) {
        qd_log(server->log_source, QD_LOG_INFO,
               "[C%"PRIu64"] Closing connection on shutdown",
               conn->connection_id);

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
            free((char*)conn->user_id);
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
    Py_XDECREF((PyObject *)server->py_displayname_obj);

    free(server);
}

void qd_server_set_container(qd_dispatch_t *qd, qd_container_t *container) {
    qd->server->container = container;
}

void qd_server_trace_all_connections() {
    qd_dispatch_t *qd = qd_dispatch_get_dispatch();

    if (qd->server) {
        sys_mutex_lock(qd->server->lock);

        qd_connection_list_t conn_list = qd->server->conn_list;
        qd_connection_t *conn = DEQ_HEAD(conn_list);

        while(conn) {
            // If there is already a tracer on the transport, nothing
            // to do, move on to the next connection.
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

static double normalize_memory_size(const uint64_t bytes, const char **suffix);
static void *thread_run(void *arg);

void qd_server_run(qd_dispatch_t *qd) {
    qd_server_t *server = qd->server;
    int i;

    assert(server);
    // Server can't run without a container
    assert(server->container);

    qd_log(server->log_source,
           QD_LOG_NOTICE, "Operational, %d Threads Running (process ID %ld)",
           server->thread_count, (long) getpid());

    const uintmax_t ram_size = qd_platform_memory_size();
    const uint64_t  vm_size = qd_router_memory_usage();

    if (ram_size && vm_size) {
        const char *suffix_vm = 0;
        const char *suffix_ram = 0;
        double vm = normalize_memory_size(vm_size, &suffix_vm);
        double ram = normalize_memory_size(ram_size, &suffix_ram);

        qd_log(server->log_source, QD_LOG_NOTICE,
               "Process VmSize %.2f %s (%.2f %s available memory)",
               vm, suffix_vm, ram, suffix_ram);
    }

#ifndef NDEBUG
    qd_log(server->log_source, QD_LOG_INFO, "Running in DEBUG Mode");
#endif

    // Start count - 1 threads and use the current thread

    int n = server->thread_count - 1;
    sys_thread_t **threads = (sys_thread_t**) calloc(n, sizeof(sys_thread_t*));

    for (i = 0; i < n; i++) {
        threads[i] = sys_thread(thread_run, server);
    }

    // Use the current thread
    thread_run(server);

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

void qd_server_stop(qd_dispatch_t *qd) {
    // Interrupt the proactor.  Async-signal safe.
    pn_proactor_interrupt(qd->server->proactor);
}

void qd_server_activate(qd_connection_t *conn) {
    if (conn) {
        conn->wake(conn);
    }
}

qd_dispatch_t *qd_server_dispatch(qd_server_t *server) {
    return server->qd;
}

// Permit replacement by dummy implementation in unit_tests
__attribute__((noinline))
void qd_server_timeout(qd_server_t *server, qd_duration_t duration) {
    pn_proactor_set_timeout(server->proactor, duration);
}

sys_mutex_t *qd_server_get_activation_lock(qd_server_t *server) {
    return server->conn_activation_lock;
}

static double normalize_memory_size(const uint64_t bytes, const char **suffix) {
    static const char * const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    const int units_ct = 5;
    const double base = 1024.0;

    double value = (double) bytes;

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

static void *thread_run(void *arg) {
    qd_server_t *server = (qd_server_t*) arg;
    bool running = true;

    while (running) {
        pn_event_batch_t *batch = pn_proactor_wait(server->proactor);
        pn_event_t *event = NULL;

        while (running && (event = pn_event_batch_next(batch))) {
            running = handle_event(server, event);
        }

        // XXX Note the absence of any call to
        // qd_conn_event_batch_complete() here.  I don't really want
        // to because it complicates things, and it makes my
        // connections hang on close.

        pn_proactor_done(server->proactor, batch);
    }

    return NULL;
}

// Timer callback to try or retry connection open
static void try_open_lh(qd_connector_t *connector) {
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

    conn->connector = connector;
    const qd_server_config_t *config = &connector->config;

    // Set the hostname on the pn_connection. This hostname will be
    // used by proton as the hostname in the open frame.

    qd_failover_item_t *item = qd_connector_get_conn_info(connector);
    char *current_host = item->host;
    char *host_port = item->host_port;

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
    connector->ctx = conn;
    connector->delay = 5000;

    qd_log(log, QD_LOG_TRACE, "[C%"PRIu64"] Connecting to %s", conn->connection_id, host_port);

    // Note: The transport is configured in the PN_CONNECTION_BOUND
    // event
    pn_proactor_connect(connector->server->proactor, conn->pn_conn, host_port);
}

static void try_open_handler(void *context) {
    qd_connector_t *connector = (qd_connector_t*) context;

    if (!qd_connector_decref(connector)) {
        // TODO aconway 2017-05-09: this lock looks too big
        sys_mutex_lock(connector->lock);
        try_open_lh(connector);
        sys_mutex_unlock(connector->lock);
    }
}

//
// Listener functions
//

qd_listener_t *qd_server_listener(qd_server_t *server) {
    qd_listener_t *listener = new_qd_listener_t();

    if (!listener) {
        return 0;
    }

    ZERO(listener);
    sys_atomic_init(&listener->ref_count, 1);

    listener->server = server;
    listener->http = NULL;

    return listener;
}

static bool listener_listen_pn(qd_listener_t *listener);
static bool listener_listen_http(qd_listener_t *listener);

bool qd_listener_listen(qd_listener_t *listener) {
    if (listener->pn_listener || listener->http) /* Already listening */
        return true;
    return listener->config.http ? listener_listen_http(listener) : listener_listen_pn(listener);
}

void qd_listener_decref(qd_listener_t* listener) {
    if (listener && sys_atomic_dec(&listener->ref_count) == 1) {
        qd_server_config_free(&listener->config);
        free_qd_listener_t(listener);
    }
}

qd_http_listener_t *qd_listener_http(qd_listener_t *listener) {
    return listener->http;
}

static bool listener_listen_pn(qd_listener_t *listener) {
    listener->pn_listener = pn_listener();

    if (listener->pn_listener) {
        pn_listener_set_context(listener->pn_listener, listener);
        pn_proactor_listen(listener->server->proactor, listener->pn_listener, listener->config.host_port, BACKLOG);

        // In use by the proactor.  PN_LISTENER_CLOSE will decrement
        // this.
        sys_atomic_inc(&listener->ref_count);

        // Listen is asynchronous.  Log the "listening" message on
        // PN_LISTENER_OPEN.
    } else {
        qd_log(listener->server->log_source, QD_LOG_CRITICAL, "No memory listening on %s",
               listener->config.host_port);
    }

    return listener->pn_listener;
}

static bool listener_listen_http(qd_listener_t *listener) {
    if (listener->server->http) {
        /* qd_http_listener holds a reference to li, will decref when closed */
        qd_http_server_listen(listener->server->http, listener);
        return listener->http;
    } else {
        qd_log(listener->server->log_source, QD_LOG_ERROR, "No HTTP support to listen on %s",
               listener->config.host_port);
        return false;
    }
}

//
// Connector functions
//

qd_connector_t *qd_server_connector(qd_server_t *server) {
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
    connector->conn_index = 1;
    connector->state = CXTR_STATE_INIT;
    connector->lock = sys_mutex();
    connector->conn_msg = (char*) malloc(300);
    memset(connector->conn_msg, 0, 300);
    connector->timer = qd_timer(connector->server->qd, try_open_handler, connector);

    if (!connector->lock || !connector->timer) {
        qd_connector_decref(connector);
        return NULL;
    }

    return connector;
}

const char *qd_connector_policy_vhost(qd_connector_t* connector) {
    return connector->policy_vhost;
}

bool qd_connector_connect(qd_connector_t *connector) {
    sys_mutex_lock(connector->lock);

    connector->ctx = NULL;
    connector->delay = 0;

    // Referenced by timer
    sys_atomic_inc(&connector->ref_count);
    qd_timer_schedule(connector->timer, connector->delay);

    sys_mutex_unlock(connector->lock);

    return true;
}

bool qd_connector_decref(qd_connector_t* connector) {
    if (connector && sys_atomic_dec(&connector->ref_count) == 1) {
        sys_mutex_lock(connector->lock);

        if (connector->ctx) {
            connector->ctx->connector = NULL;
        }

        sys_mutex_unlock(connector->lock);

        qd_server_config_free(&connector->config);
        qd_timer_free(connector->timer);

        qd_failover_item_t *item = DEQ_HEAD(connector->conn_info_list);

        while (item) {
            DEQ_REMOVE_HEAD(connector->conn_info_list);
            free(item->scheme);
            free(item->host);
            free(item->port);
            free(item->hostname);
            free(item->host_port);
            free(item);
            item = DEQ_HEAD(connector->conn_info_list);
        }

        sys_mutex_free(connector->lock);

        if (connector->policy_vhost) {
            free(connector->policy_vhost);
        }

        free(connector->conn_msg);
        free_qd_connector_t(connector);

        return true;
    }

    return false;
}

qd_failover_item_t *qd_connector_get_conn_info(qd_connector_t *connector) {
    qd_failover_item_t *item = DEQ_HEAD(connector->conn_info_list);

    if (DEQ_SIZE(connector->conn_info_list) > 1) {
        for (int i = 1; i < connector->conn_index; i++) {
            item = DEQ_NEXT(item);
        }
    }

    return item;
}

bool qd_connector_has_failover_info(qd_connector_t* connector) {
    if (connector && DEQ_SIZE(connector->conn_info_list) > 1) {
        return true;
    }

    return false;
}

const qd_server_config_t *qd_connector_config(const qd_connector_t *connector) {
    return &connector->config;
}
