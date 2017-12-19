/*
 *
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
 *
 */

#include "remote_sasl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proton/engine.h>
#include <proton/proactor.h>
#include <proton/sasl.h>
#include <proton/sasl-plugin.h>
#include <qpid/dispatch/log.h>

static qd_log_source_t* auth_service_log;

typedef struct
{
    size_t size;
    char *start;
} qdr_owned_bytes_t;

const int8_t UPSTREAM_INIT_RECEIVED = 1;
const int8_t UPSTREAM_RESPONSE_RECEIVED = 2;
const int8_t DOWNSTREAM_MECHANISMS_RECEIVED = 3;
const int8_t DOWNSTREAM_CHALLENGE_RECEIVED = 4;
const int8_t DOWNSTREAM_OUTCOME_RECEIVED = 5;
const int8_t DOWNSTREAM_CLOSED = 6;

typedef struct
{
    char* authentication_service_address;
    char* sasl_init_hostname;
    pn_ssl_domain_t* ssl_domain;

    pn_connection_t* downstream;
    char* selected_mechanism;
    qdr_owned_bytes_t response;
    int8_t downstream_state;
    bool downstream_released;

    pn_connection_t* upstream;
    char* mechlist;
    qdr_owned_bytes_t challenge;
    int8_t upstream_state;
    bool upstream_released;

    bool complete;
    char* username;
    pn_sasl_outcome_t outcome;
} qdr_sasl_relay_t;

static void copy_bytes(const pn_bytes_t* from, qdr_owned_bytes_t* to)
{
    if (to->start) {
        free(to->start);
    }
    to->start = (char*) malloc(from->size);
    to->size = from->size;
    memcpy(to->start, from->start, from->size);
}

static qdr_sasl_relay_t* new_qdr_sasl_relay_t(const char* address, const char* sasl_init_hostname)
{
    qdr_sasl_relay_t* instance = (qdr_sasl_relay_t*) malloc(sizeof(qdr_sasl_relay_t));
    instance->authentication_service_address = strdup(address);
    if (sasl_init_hostname) {
        instance->sasl_init_hostname = strdup(sasl_init_hostname);
    } else {
        instance->sasl_init_hostname = 0;
    }
    instance->selected_mechanism = 0;
    instance->response.start = 0;
    instance->response.size = 0;
    instance->mechlist = 0;
    instance->challenge.start = 0;
    instance->challenge.size = 0;
    instance->upstream_state = 0;
    instance->downstream_state = 0;
    instance->upstream_released = false;
    instance->downstream_released = false;
    instance->complete = false;
    instance->upstream = 0;
    instance->downstream = 0;
    instance->username = 0;
    return instance;
}

static void delete_qdr_sasl_relay_t(qdr_sasl_relay_t* instance)
{
    if (instance->authentication_service_address) free(instance->authentication_service_address);
    if (instance->sasl_init_hostname) free(instance->sasl_init_hostname);
    if (instance->mechlist) free(instance->mechlist);
    if (instance->selected_mechanism) free(instance->selected_mechanism);
    if (instance->response.start) free(instance->response.start);
    if (instance->challenge.start) free(instance->challenge.start);
    if (instance->username) free(instance->username);
    free(instance);
}

PN_HANDLE(REMOTE_SASL_CTXT)

bool qdr_is_authentication_service_connection(pn_connection_t* conn)
{
    if (conn) {
        pn_record_t *r = pn_connection_attachments(conn);
        return pn_record_has(r, REMOTE_SASL_CTXT);
    } else {
        return false;
    }
}

static qdr_sasl_relay_t* get_sasl_relay_context(pn_connection_t* conn)
{
    if (conn) {
        pn_record_t *r = pn_connection_attachments(conn);
        if (pn_record_has(r, REMOTE_SASL_CTXT)) {
            return (qdr_sasl_relay_t*) pn_record_get(r, REMOTE_SASL_CTXT);
        } else {
            return NULL;
        }
    } else {
        return NULL;
    }
}

static void set_sasl_relay_context(pn_connection_t* conn, qdr_sasl_relay_t* context)
{
    pn_record_t *r = pn_connection_attachments(conn);
    pn_record_def(r, REMOTE_SASL_CTXT, PN_VOID);
    pn_record_set(r, REMOTE_SASL_CTXT, context);
}

static bool remote_sasl_init_server(pn_transport_t* transport)
{
    pn_connection_t* upstream = pn_transport_connection(transport);
    if (upstream && pnx_sasl_get_context(transport)) {
        qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
        if (impl->upstream) return true;
        impl->upstream = upstream;
        pn_proactor_t* proactor = pn_connection_proactor(upstream);
        if (!proactor) return false;
        impl->downstream = pn_connection();
        pn_connection_set_hostname(impl->downstream, pn_connection_get_hostname(upstream));
        pn_connection_set_user(impl->downstream, "dummy");//force sasl
        set_sasl_relay_context(impl->downstream, impl);

        pn_proactor_connect(proactor, impl->downstream, impl->authentication_service_address);
        return true;
    } else {
        return false;
    }
}

static bool remote_sasl_init_client(pn_transport_t* transport)
{
    //for the client side of the connection to the authentication
    //service, need to use the same context as the server side of the
    //connection it is authenticating on behalf of
    pn_connection_t* conn = pn_transport_connection(transport);
    qdr_sasl_relay_t* impl = get_sasl_relay_context(conn);
    if (impl) {
        pnx_sasl_set_context(transport, impl);
        return true;
    } else {
        return false;
    }
}

static void remote_sasl_free(pn_transport_t *transport)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl) {
        if (pnx_sasl_is_client(transport)) {
            impl->downstream_released = true;
            if (impl->upstream_released) {
                delete_qdr_sasl_relay_t(impl);
            } else {
                pn_connection_wake(impl->upstream);
            }
        } else {
            impl->upstream_released = true;
            if (impl->downstream_released) {
                delete_qdr_sasl_relay_t(impl);
            } else {
                pn_connection_wake(impl->downstream);
            }
        }
    }
}

static void remote_sasl_prepare(pn_transport_t *transport)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (!impl) return;
    if (pnx_sasl_is_client(transport)) {
        if (impl->downstream_state == UPSTREAM_INIT_RECEIVED) {
            pnx_sasl_set_selected_mechanism(transport, impl->selected_mechanism);
            pnx_sasl_set_local_hostname(transport, impl->sasl_init_hostname);
            pnx_sasl_set_bytes_out(transport, pn_bytes(impl->response.size, impl->response.start));
            pnx_sasl_set_desired_state(transport, SASL_POSTED_INIT);
        } else if (impl->downstream_state == UPSTREAM_RESPONSE_RECEIVED) {
            pnx_sasl_set_bytes_out(transport, pn_bytes(impl->response.size, impl->response.start));
            pnx_sasl_set_desired_state(transport, SASL_POSTED_RESPONSE);
        }
        impl->downstream_state = 0;
    } else {
        if (impl->upstream_state == DOWNSTREAM_MECHANISMS_RECEIVED) {
            pnx_sasl_set_desired_state(transport, SASL_POSTED_MECHANISMS);
        } else if (impl->upstream_state == DOWNSTREAM_CHALLENGE_RECEIVED) {
            pnx_sasl_set_bytes_out(transport, pn_bytes(impl->challenge.size, impl->challenge.start));
            pnx_sasl_set_desired_state(transport, SASL_POSTED_CHALLENGE);
        } else if (impl->upstream_state == DOWNSTREAM_OUTCOME_RECEIVED) {
            switch (impl->outcome) {
            case PN_SASL_OK:
                pnx_sasl_succeed_authentication(transport, impl->username);
                break;
            default:
                pnx_sasl_fail_authentication(transport);
            }
            pnx_sasl_set_desired_state(transport, SASL_POSTED_OUTCOME);
        } else if (impl->upstream_state == DOWNSTREAM_CLOSED) {
            pnx_sasl_fail_authentication(transport);
            pnx_sasl_set_desired_state(transport, SASL_POSTED_OUTCOME);
        }
        impl->upstream_state = 0;
    }
}

static bool notify_upstream(qdr_sasl_relay_t* impl, uint8_t state)
{
    if (!impl->upstream_released) {
        impl->upstream_state = state;
        pn_connection_wake(impl->upstream);
        return true;
    } else {
        return false;
    }
}

static bool notify_downstream(qdr_sasl_relay_t* impl, uint8_t state)
{
    if (!impl->downstream_released && impl->downstream) {
        impl->downstream_state = state;
        pn_connection_wake(impl->downstream);
        return true;
    } else {
        return false;
    }
}

// Client / Downstream
static bool remote_sasl_process_mechanisms(pn_transport_t *transport, const char *mechs)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl) {
        impl->mechlist = strdup(mechs);
        if (notify_upstream(impl, DOWNSTREAM_MECHANISMS_RECEIVED)) {
            return true;
        } else {
            pnx_sasl_set_desired_state(transport, SASL_ERROR);
            return false;
        }
    } else {
        return false;
    }
}

// Client / Downstream
static void remote_sasl_process_challenge(pn_transport_t *transport, const pn_bytes_t *recv)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl) {
        copy_bytes(recv, &(impl->challenge));
        if (!notify_upstream(impl, DOWNSTREAM_CHALLENGE_RECEIVED)) {
            pnx_sasl_set_desired_state(transport, SASL_ERROR);
        }
    }
}

// Client / Downstream
static void remote_sasl_process_outcome(pn_transport_t *transport)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl) {
        pn_sasl_t* sasl = pn_sasl(transport);
        if (sasl) {
            impl->outcome = pn_sasl_outcome(sasl);
            impl->username = strdup(pn_sasl_get_user(sasl));
            impl->complete = true;
            if (!notify_upstream(impl, DOWNSTREAM_OUTCOME_RECEIVED)) {
                pnx_sasl_set_desired_state(transport, SASL_ERROR);
            }
        }
    }
}

// Server / Upstream
static const char* remote_sasl_list_mechs(pn_transport_t *transport)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl && impl->mechlist) {
        return impl->mechlist;
    } else {
        return NULL;
    }
}

// Server / Upstream
static void remote_sasl_process_init(pn_transport_t *transport, const char *mechanism, const pn_bytes_t *recv)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl) {
        impl->selected_mechanism = strdup(mechanism);
        copy_bytes(recv, &(impl->response));
        if (!notify_downstream(impl, UPSTREAM_INIT_RECEIVED)) {
            pnx_sasl_set_desired_state(transport, SASL_ERROR);
        }
    }
}

// Server / Upstream
static void remote_sasl_process_response(pn_transport_t *transport, const pn_bytes_t *recv)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl) {
        copy_bytes(recv, &(impl->response));
        if (!notify_downstream(impl, UPSTREAM_RESPONSE_RECEIVED)) {
            pnx_sasl_set_desired_state(transport, SASL_ERROR);
        }
    }
}

static bool remote_sasl_can_encrypt(pn_transport_t *transport)
{
    return false;
}

static ssize_t remote_sasl_max_encrypt_size(pn_transport_t *transport)
{
    return 0;
}
static ssize_t remote_sasl_encode(pn_transport_t *transport, pn_bytes_t in, pn_bytes_t *out)
{
    return 0;
}
static ssize_t remote_sasl_decode(pn_transport_t *transport, pn_bytes_t in, pn_bytes_t *out)
{
    return 0;
}


static const pnx_sasl_implementation remote_sasl_impl = {
    remote_sasl_free,
    remote_sasl_list_mechs,
    remote_sasl_init_server,
    remote_sasl_init_client,
    remote_sasl_prepare,
    remote_sasl_process_init,
    remote_sasl_process_response,
    remote_sasl_process_mechanisms,
    remote_sasl_process_challenge,
    remote_sasl_process_outcome,
    remote_sasl_can_encrypt,
    remote_sasl_max_encrypt_size,
    remote_sasl_encode,
    remote_sasl_decode
};

static void set_remote_impl(pn_transport_t *transport, qdr_sasl_relay_t* context)
{
    pnx_sasl_set_implementation(transport, &remote_sasl_impl, context);
}

void qdr_use_remote_authentication_service(pn_transport_t *transport, const char* address, const char* sasl_init_hostname, pn_ssl_domain_t* ssl_domain)
{
    auth_service_log = qd_log_source("AUTHSERVICE");
    qdr_sasl_relay_t* context = new_qdr_sasl_relay_t(address, sasl_init_hostname);
    context->ssl_domain = ssl_domain;
    set_remote_impl(transport, context);
}

void qdr_handle_authentication_service_connection_event(pn_event_t *e)
{
    pn_connection_t *conn = pn_event_connection(e);
    pn_transport_t *transport = pn_event_transport(e);
    if (pn_event_type(e) == PN_CONNECTION_BOUND) {
        qd_log(auth_service_log, QD_LOG_DEBUG, "Handling connection bound event for authentication service connection");
        qdr_sasl_relay_t* context = get_sasl_relay_context(conn);
        if (context->ssl_domain) {
            pn_ssl_t* ssl = pn_ssl(transport);
            if (!ssl || pn_ssl_init(ssl, context->ssl_domain, 0)) {
                qd_log(auth_service_log, QD_LOG_WARNING, "Cannot initialise SSL");
            } else {
                qd_log(auth_service_log, QD_LOG_DEBUG, "Successfully initialised SSL");
            }
        }
        set_remote_impl(pn_event_transport(e), context);
    } else if (pn_event_type(e) == PN_CONNECTION_REMOTE_OPEN) {
        qd_log(auth_service_log, QD_LOG_DEBUG, "authentication against service complete; closing connection");
        pn_connection_close(conn);
    } else if (pn_event_type(e) == PN_CONNECTION_REMOTE_CLOSE) {
        qd_log(auth_service_log, QD_LOG_DEBUG, "authentication service closed connection");
        pn_connection_close(conn);
        pn_transport_close_head(transport);
    } else if (pn_event_type(e) == PN_TRANSPORT_CLOSED) {
        qd_log(auth_service_log, QD_LOG_DEBUG, "disconnected from authentication service");
        qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
        if (!impl->complete) {
            notify_upstream(impl, DOWNSTREAM_CLOSED);
            pn_condition_t* condition = pn_transport_condition(transport);
            if (condition) {
                qd_log(auth_service_log, QD_LOG_WARNING, "Downstream disconnected: %s %s", pn_condition_get_name(condition), pn_condition_get_description(condition));
            } else {
                qd_log(auth_service_log, QD_LOG_WARNING, "Downstream disconnected, no details available");
            }
        }
    } else {
        qd_log(auth_service_log, QD_LOG_DEBUG, "Ignoring event for authentication service connection: %s", pn_event_type_name(pn_event_type(e)));
    }
}
