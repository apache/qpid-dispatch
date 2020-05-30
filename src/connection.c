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

#include "connection.h"

#include <proton/connection.h>
#include <proton/proactor.h>
#include <proton/sasl.h>
#include <qpid/dispatch/python_embedded.h>

#include "connector.h"
#include "listener.h"

const char *MECH_EXTERNAL = "EXTERNAL";

// Allowed uidFormat fields
const char CERT_COUNTRY_CODE       = 'c';
const char CERT_STATE              = 's';
const char CERT_CITY_LOCALITY      = 'l';
const char CERT_ORGANIZATION_NAME  = 'o';
const char CERT_ORGANIZATION_UNIT  = 'u';
const char CERT_COMMON_NAME        = 'n';
const char CERT_FINGERPRINT_SHA1   = '1';
const char CERT_FINGERPRINT_SHA256 = '2';
const char CERT_FINGERPRINT_SHA512 = '5';
char *COMPONENT_SEPARATOR          = ";";  // XXX Should be const?

ALLOC_DEFINE(qd_connection_t);

void connection_invoke_deferred_calls(qd_connection_t *conn, bool discard);

void qd_connection_free(qd_connection_t *conn)
{
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
    connection_invoke_deferred_calls(conn, true);

    sys_mutex_free(conn->deferred_call_lock);
    qd_policy_settings_free(conn->policy_settings);

    if (conn->free_user_id) {
        free((char *) conn->user_id);
    }

    if (conn->timer) {
        qd_timer_free(conn->timer);
    }

    free(conn->name);
    free(conn->role);

    sys_mutex_lock(server->conn_activation_lock);
    free_qd_connection_t(conn);
    sys_mutex_unlock(server->conn_activation_lock);

    // Note: pn_conn is freed by the proactor
}

static const char *connection_get_user(qd_connection_t *conn, pn_transport_t *transport);

void qd_connection_set_user(qd_connection_t *conn)
{
    pn_transport_t *transport = pn_connection_transport(conn->pn_conn);
    pn_sasl_t *sasl           = pn_sasl(transport);

    if (sasl) {
        const char *mech = pn_sasl_get_mech(sasl);
        conn->user_id    = pn_transport_get_user(transport);

        // We want to set the user name only if it is not already set and the selected sasl mechanism is EXTERNAL
        if (mech && strcmp(mech, MECH_EXTERNAL) == 0) {
            const char *user_id = connection_get_user(conn, transport);

            if (user_id) {
                conn->user_id = user_id;
            }
        }
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
    return conn->listener != NULL;
}

uint64_t qd_connection_connection_id(qd_connection_t *conn)
{
    return conn->connection_id;
}

const qd_server_config_t *qd_connection_config(const qd_connection_t *conn)
{
    if (conn->listener) {
        return &conn->listener->config;
    }

    if (conn->connector) {
        return &conn->connector->config;
    }

    return NULL;
}

void qd_connection_invoke_deferred(qd_connection_t *conn, qd_deferred_t call, void *context)
{
    assert(conn);

    qd_deferred_call_t *dc = new_qd_deferred_call_t();
    DEQ_ITEM_INIT(dc);
    dc->call    = call;
    dc->context = context;

    sys_mutex_lock(conn->deferred_call_lock);
    DEQ_INSERT_TAIL(conn->deferred_calls, dc);
    sys_mutex_unlock(conn->deferred_call_lock);

    qd_server_activate(conn);
}

const char *qd_connection_remote_ip(const qd_connection_t *conn)
{
    return conn->rhost;
}

const char *qd_connection_name(const qd_connection_t *conn)
{
    if (conn->connector) {
        return conn->connector->config.host_port;
    } else {
        return conn->rhost_port;
    }
}

qd_connector_t *qd_connection_connector(const qd_connection_t *conn)
{
    return conn->connector;
}

bool qd_connection_strip_annotations_in(const qd_connection_t *conn)
{
    return conn->strip_annotations_in;
}

uint64_t qd_connection_max_message_size(const qd_connection_t *conn)
{
    if (conn && conn->policy_settings) {
        return conn->policy_settings->maxMessageSize;
    }

    return 0;
}

// Returns a char pointer to a user id which is constructed from
// components specified in the config->ssl_uid_format.  Parses through
// each component and builds a semi-colon delimited string which is
// returned as the user id.
static const char *connection_get_user(qd_connection_t *conn, pn_transport_t *transport)
{
    const qd_server_config_t *config = conn->connector ? &conn->connector->config : &conn->listener->config;

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

        const char *country_code  = 0;
        const char *state         = 0;
        const char *locality_city = 0;
        const char *organization  = 0;
        const char *org_unit      = 0;
        const char *common_name   = 0;

        // SHA1 is 20 octets (40 hex characters); SHA256 is 32 octets (64 hex characters).
        // SHA512 is 64 octets (128 hex characters)
        char fingerprint[129] = "\0";

        int uid_length       = 0;
        int semi_colon_count = -1;
        int component_count  = strlen(components);

        for (int x = 0; x < component_count; x++) {
            // accumulate the length into uid_length on each pass so we definitively know the number of octets to
            // malloc.
            if (components[x] == CERT_COUNTRY_CODE) {
                country_code = pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_COUNTRY_NAME);

                if (country_code) {
                    uid_length += strlen((const char *) country_code);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_STATE) {
                state = pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_STATE_OR_PROVINCE);

                if (state) {
                    uid_length += strlen((const char *) state);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_CITY_LOCALITY) {
                locality_city =
                    pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_CITY_OR_LOCALITY);

                if (locality_city) {
                    uid_length += strlen((const char *) locality_city);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_ORGANIZATION_NAME) {
                organization =
                    pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_ORGANIZATION_NAME);

                if (organization) {
                    uid_length += strlen((const char *) organization);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_ORGANIZATION_UNIT) {
                org_unit = pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_ORGANIZATION_UNIT);

                if (org_unit) {
                    uid_length += strlen((const char *) org_unit);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_COMMON_NAME) {
                common_name = pn_ssl_get_remote_subject_subfield(pn_ssl(transport), PN_SSL_CERT_SUBJECT_COMMON_NAME);

                if (common_name) {
                    uid_length += strlen((const char *) common_name);
                    semi_colon_count++;
                }
            } else if (components[x] == CERT_FINGERPRINT_SHA1 || components[x] == CERT_FINGERPRINT_SHA256 ||
                       components[x] == CERT_FINGERPRINT_SHA512) {
                // Allocate the memory for message digest
                int out                = 0;
                int fingerprint_length = 0;

                if (components[x] == CERT_FINGERPRINT_SHA1) {
                    fingerprint_length = 40;
                    out = pn_ssl_get_cert_fingerprint(pn_ssl(transport), fingerprint, fingerprint_length + 1,
                                                      PN_SSL_SHA1);
                } else if (components[x] == CERT_FINGERPRINT_SHA256) {
                    fingerprint_length = 64;
                    out = pn_ssl_get_cert_fingerprint(pn_ssl(transport), fingerprint, fingerprint_length + 1,
                                                      PN_SSL_SHA256);
                } else if (components[x] == CERT_FINGERPRINT_SHA512) {
                    fingerprint_length = 128;
                    out = pn_ssl_get_cert_fingerprint(pn_ssl(transport), fingerprint, fingerprint_length + 1,
                                                      PN_SSL_SHA512);
                }

                // Avoid 'out unused' compiler warnings if NDEBUG undef'ed
                (void) out;

                assert(out != PN_ERR);

                uid_length += fingerprint_length;
                semi_colon_count++;
            } else {
                // This is an unrecognized component.  Log a critical
                // error.
                qd_log(conn->server->log_source, QD_LOG_CRITICAL, "Unrecognized component '%c' in uidFormat ",
                       components[x]);
                return 0;
            }
        }

        if (uid_length > 0) {
            // The +1 is for the '\0' character
            char *user_id = malloc((uid_length + semi_colon_count + 1) * sizeof(char));

            // We have allocated memory for user_id. We are
            // responsible for freeing this memory. Set
            // conn->free_user_id to true so that we know that we have
            // to free the user_id.
            conn->free_user_id = true;
            memset(user_id, 0, uid_length + semi_colon_count + 1);

            // The components in the user id string must appear in the same order as it appears in the component string.
            // that is why we have this loop
            for (int x = 0; x < component_count; x++) {
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
                } else if (components[x] == CERT_FINGERPRINT_SHA1 || components[x] == CERT_FINGERPRINT_SHA256 ||
                           components[x] == CERT_FINGERPRINT_SHA512) {
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
                PyObject *result = PyObject_CallMethod((PyObject *) conn->server->py_displayname_obj, "query", "(ss)",
                                                       config->ssl_profile, user_id);

                if (result) {
                    free(user_id);
                    user_id = py_string_2_c(result);
                    Py_XDECREF(result);
                } else {
                    qd_log(conn->server->log_source, QD_LOG_DEBUG,
                           "Internal: failed to read displaynameservice query result");
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
