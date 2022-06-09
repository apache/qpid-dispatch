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

#include "server_private.h"

#include <stdlib.h>
#include <string.h>

#include <proton/engine.h>
#include <proton/proactor.h>
#include <proton/sasl.h>
#include <proton/sasl_plugin.h>
#include <proton/object.h>

#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/log.h"

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

typedef struct {
    size_t used;
    size_t capacity;
    char *start;
} buffer_t;


static void allocate_buffer(buffer_t* buffer)
{
    buffer->start = qd_malloc(buffer->capacity);
    memset(buffer->start, 0, buffer->capacity);
}

static void free_buffer(buffer_t* buffer)
{
    free(buffer->start);
    buffer->start = 0;
    buffer->capacity = 0;
    buffer->used = 0;
}

typedef struct {
    buffer_t sources;
    buffer_t targets;
} permissions_t;

static void init_buffer(buffer_t* buffer)
{
    buffer->used = 0;
    buffer->capacity = 0;
    buffer->start = 0;
}

static void init_permissions(permissions_t* permissions)
{
    init_buffer(&permissions->sources);
    init_buffer(&permissions->targets);
}

typedef struct
{
    char* authentication_service_address;
    char* hostname;
    char* sasl_init_hostname;
    pn_ssl_domain_t* ssl_domain;
    pn_proactor_t* proactor;

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
    permissions_t permissions;
    pn_sasl_outcome_t outcome;

    sys_mutex_t *lock;
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

static qdr_sasl_relay_t* new_qdr_sasl_relay_t(const char* address, const char* hostname, const char* sasl_init_hostname, pn_proactor_t* proactor)
{
    qdr_sasl_relay_t* instance = NEW(qdr_sasl_relay_t);
    ZERO(instance);
    instance->authentication_service_address = qd_strdup(address);
    if (hostname) {
        instance->hostname = qd_strdup(hostname);
    }
    if (sasl_init_hostname) {
        instance->sasl_init_hostname = qd_strdup(sasl_init_hostname);
    }
    instance->proactor = proactor;
    init_permissions(&instance->permissions);
    instance->lock = sys_mutex();
    return instance;
}

static void delete_qdr_sasl_relay_t(qdr_sasl_relay_t* instance)
{
    if (instance->authentication_service_address) free(instance->authentication_service_address);
    if (instance->hostname) free(instance->hostname);
    if (instance->sasl_init_hostname) free(instance->sasl_init_hostname);
    if (instance->ssl_domain) pn_ssl_domain_free(instance->ssl_domain);
    if (instance->mechlist) free(instance->mechlist);
    if (instance->selected_mechanism) free(instance->selected_mechanism);
    if (instance->response.start) free(instance->response.start);
    if (instance->challenge.start) free(instance->challenge.start);
    if (instance->username) free(instance->username);
    free_buffer(&(instance->permissions.targets));
    free_buffer(&(instance->permissions.sources));
    sys_mutex_free(instance->lock);
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
        pn_proactor_t* proactor = impl->proactor;
        if (!proactor) return false;
        impl->downstream = pn_connection();
        pn_connection_set_hostname(impl->downstream, impl->hostname);
        set_sasl_relay_context(impl->downstream, impl);
        //request permissions in response if supported by peer:
        pn_data_t* data = pn_connection_desired_capabilities(impl->downstream);
        pn_data_put_array(data, false, PN_SYMBOL);
        pn_data_enter(data);
        pn_data_put_symbol(data, pn_bytes(13, "ADDRESS-AUTHZ"));
        pn_data_exit(data);

        data = pn_connection_properties(impl->downstream);
        pn_data_put_map(data);
        pn_data_enter(data);
        pn_data_put_symbol(data, pn_bytes(strlen(QD_CONNECTION_PROPERTY_PRODUCT_KEY), QD_CONNECTION_PROPERTY_PRODUCT_KEY));
        pn_data_put_string(data, pn_bytes(strlen(QD_CONNECTION_PROPERTY_PRODUCT_VALUE), QD_CONNECTION_PROPERTY_PRODUCT_VALUE));
        pn_data_put_symbol(data, pn_bytes(strlen(QD_CONNECTION_PROPERTY_VERSION_KEY), QD_CONNECTION_PROPERTY_VERSION_KEY));
        pn_data_put_string(data, pn_bytes(strlen(QPID_DISPATCH_VERSION), QPID_DISPATCH_VERSION));
        pn_data_exit(data);

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


static void connection_wake(pn_connection_t* conn)
{
    qd_connection_t *ctx = pn_connection_get_context(conn);
    if (ctx) {
        ctx->wake(ctx);
    } else {
        pn_connection_wake(conn);
    }
}

static bool notify_upstream(qdr_sasl_relay_t* impl, uint8_t state)
{
    if (!impl->upstream_released) {
        impl->upstream_state = state;
        connection_wake(impl->upstream);
        return true;
    } else {
        return false;
    }
}

static bool notify_downstream(qdr_sasl_relay_t* impl, uint8_t state)
{
    if (!impl->downstream_released && impl->downstream) {
        impl->downstream_state = state;
        connection_wake(impl->downstream);
        return true;
    } else {
        return false;
    }
}

static bool delete_on_downstream_freed(qdr_sasl_relay_t* impl)
{
    bool result;
    sys_mutex_lock(impl->lock);
    impl->downstream_released = true;
    result = impl->upstream_released;
    sys_mutex_unlock(impl->lock);
    return result;
}

static bool delete_on_upstream_freed(qdr_sasl_relay_t* impl)
{
    bool result;
    sys_mutex_lock(impl->lock);
    impl->upstream_released = true;
    result = impl->downstream_released || impl->downstream == 0;
    sys_mutex_unlock(impl->lock);
    return result;
}

static bool can_delete(pn_transport_t *transport, qdr_sasl_relay_t* impl)
{
    if (pnx_sasl_is_client(transport)) {
        return delete_on_downstream_freed(impl);
    } else {
        return delete_on_upstream_freed(impl);
    }
}

static void remote_sasl_free(pn_transport_t *transport)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl && can_delete(transport, impl)) {
        delete_qdr_sasl_relay_t(impl);
    }
}

static void set_policy_settings(pn_connection_t* conn, permissions_t* permissions)
{
    if (permissions->targets.start || permissions->sources.start) {
        qd_connection_t *qd_conn = (qd_connection_t*) pn_connection_get_context(conn);
        qd_conn->policy_settings = new_qd_policy_settings_t();
        ZERO(qd_conn->policy_settings);

        if (permissions->targets.start && permissions->targets.capacity) {
            qd_conn->policy_settings->targets = qd_policy_compile_allowed_csv(permissions->targets.start);
        }
        if (permissions->sources.start && permissions->sources.capacity) {
            qd_conn->policy_settings->sources = qd_policy_compile_allowed_csv(permissions->sources.start);
        }
        qd_conn->policy_settings->spec.allowDynamicSource = true;
        qd_conn->policy_settings->spec.allowAnonymousSender = true;
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
                set_policy_settings(impl->upstream, &impl->permissions);
                qd_log(auth_service_log, QD_LOG_INFO, "authenticated as %s", impl->username);
                pnx_sasl_set_succeeded(transport, impl->username, NULL);
                break;
            default:
                pnx_sasl_set_failed(transport);
            }
            pnx_sasl_set_desired_state(transport, SASL_POSTED_OUTCOME);
        } else if (impl->upstream_state == DOWNSTREAM_CLOSED) {
            pnx_sasl_set_failed(transport);
            pnx_sasl_set_desired_state(transport, SASL_POSTED_OUTCOME);
        }
        impl->upstream_state = 0;
    }
}

// Client / Downstream
static bool remote_sasl_process_mechanisms(pn_transport_t *transport, const char *mechs)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl) {
        impl->mechlist = qd_strdup(mechs);
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
static void remote_sasl_process_outcome(pn_transport_t *transport, const pn_bytes_t *recv)
{
    qdr_sasl_relay_t* impl = (qdr_sasl_relay_t*) pnx_sasl_get_context(transport);
    if (impl) {
        pn_sasl_t* sasl = pn_sasl(transport);
        if (sasl) {
            impl->outcome = pn_sasl_outcome(sasl);
            impl->complete = true;
            //only consider complete if failed; if successful wait for the open frame
            if (impl->outcome != PN_SASL_OK && !notify_upstream(impl, DOWNSTREAM_OUTCOME_RECEIVED)) {
                pnx_sasl_set_desired_state(transport, SASL_ERROR);
                pn_transport_close_tail(transport);
                pn_transport_close_head(transport);
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
        impl->selected_mechanism = qd_strdup(mechanism);
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

void qdr_use_remote_authentication_service(pn_transport_t *transport, const char* address, const char* hostname, const char* sasl_init_hostname, pn_ssl_domain_t* ssl_domain, pn_proactor_t* proactor)
{
    auth_service_log = qd_log_source("AUTHSERVICE");
    qdr_sasl_relay_t* context = new_qdr_sasl_relay_t(address, hostname, sasl_init_hostname, proactor);
    context->ssl_domain = ssl_domain;
    set_remote_impl(transport, context);
}

static bool append(buffer_t* buffer, pn_bytes_t data)
{
    if (buffer->capacity > data.size + buffer->used) {
        if (buffer->used > 0) buffer->start[buffer->used++] = ',';
        strncpy(buffer->start + buffer->used, data.start, data.size);
        buffer->used += data.size;
        return true;
    } else {
        return false;
    }
}

static size_t min(size_t a, size_t b)
{
    if (a > b) return b;
    else return a;
}

typedef void* (*permission_handler)(pn_bytes_t, bool, bool, void*);

static void* compute_required_size(pn_bytes_t address, bool send, bool recv, void* context)
{
    permissions_t* permissions = (permissions_t*) context;
    if (send) permissions->targets.capacity += address.size + 1;
    if (recv) permissions->sources.capacity += address.size + 1;
    return context;
}

static void* collect_permissions(pn_bytes_t address, bool send, bool recv, void* context)
{
    permissions_t* permissions = (permissions_t*) context;
    if (send) append(&(permissions->targets), address);
    if (recv) append(&(permissions->sources), address);
    return context;
}

static void* parse_permissions(pn_data_t* data, permission_handler handler, void* initial_context)
{
    void* context = initial_context;
    size_t count = pn_data_get_map(data);
    pn_data_enter(data);
    for (size_t i = 0; i < count/2; i++) {
        if (pn_data_next(data)) {
            if (pn_data_type(data) == PN_STRING) {
                pn_bytes_t address = pn_data_get_string(data);
                if (pn_data_next(data)) {
                    if (pn_data_type(data) == PN_ARRAY && pn_data_get_array_type(data) == PN_STRING) {
                        size_t length = pn_data_get_array(data);
                        pn_data_enter(data);
                        for (size_t j = 0; j < length; j++) {
                            if (pn_data_next(data)) {
                                pn_bytes_t permission = pn_data_get_string(data);
                                //printf("in permissions map %i of %i is %.*s for %.*s\n", (int) (j+1), (int) length, (int) permission.size, permission.start, (int) address.size, address.start);
                                bool send = strncmp(permission.start, "send", min(permission.size, 4)) == 0;
                                bool recv = strncmp(permission.start, "recv", min(permission.size, 4)) == 0;

                                if (send || recv) {
                                    context = handler(address, send, recv, context);
                                }
                            }
                        }
                        pn_data_exit(data);
                    }
                }
            } else {
                //key is not string, consume value to move onto next pair
                pn_data_next(data);
            }
        }
    }
    pn_data_exit(data);
    return context;
}

static void* parse_properties(pn_data_t* data, permission_handler handler, void* initial_context)
{
    void* context = 0;
    size_t count = pn_data_get_map(data);
    pn_data_enter(data);
    for (size_t i = 0; !context && i < count/2; i++) {
        if (pn_data_next(data)) {
            if (pn_data_type(data) == PN_SYMBOL) {
                pn_bytes_t key = pn_data_get_symbol(data);
                if (key.size && key.start && strncmp(key.start, "address-authz", min(key.size, 13)) == 0) {
                    pn_data_next(data);
                    context = parse_permissions(data, handler, initial_context);
                } else {
                    //key didn't match, move to next pair
                    pn_data_next(data);
                }
            } else {
                //key was not symbol, move to next pair
                pn_data_next(data);
            }
        }
    }
    pn_data_exit(data);
    pn_data_rewind(data);
    pn_data_next(data);
    return context;
}

static pn_data_t* extract_map_entry(pn_data_t* data, const char* name)
{
    pn_data_t* result = 0;
    size_t count = pn_data_get_map(data);
    pn_data_enter(data);
    for (size_t i = 0; !result && i < count/2; i++) {
        if (pn_data_next(data)) {
            if (pn_data_type(data) == PN_SYMBOL || pn_data_type(data) == PN_STRING) {
                pn_bytes_t key = pn_data_type(data) == PN_SYMBOL ? pn_data_get_symbol(data) : pn_data_get_string(data);
                if (key.size && key.start && strncmp(key.start, name, min(key.size, strlen(name))) == 0) {
                    pn_data_next(data);
                    result = data;
                } else {
                    //key didn't match, move to next pair
                    pn_data_next(data);
                }
            } else {
                //key was not symbol, move to next pair
                pn_data_next(data);
            }
        }
    }
    return result;
}

static pn_bytes_t extract_authenticated_identity(pn_data_t* data)
{
    pn_bytes_t result = pn_bytes_null;
    pn_data_t* authid = extract_map_entry(data, "authenticated-identity");
    if (authid) {
        pn_data_t* id = extract_map_entry(authid, "sub");
        if (id) {
            result = pn_data_get_string(id);
        }
        pn_data_exit(data);
    }
    pn_data_exit(data);
    pn_data_rewind(data);
    pn_data_next(data);
    return result;
}

void qdr_handle_authentication_service_connection_event(pn_event_t *e)
{
    pn_connection_t *conn = pn_event_connection(e);
    pn_transport_t *transport = pn_event_transport(e);
    if (pn_event_type(e) == PN_CONNECTION_BOUND) {
        pn_sasl(transport);
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

        qdr_sasl_relay_t* context = get_sasl_relay_context(conn);
        //extract permissions as two comma separated lists (allowed sources and targets)
        pn_data_t* properties = pn_connection_remote_properties(conn);
        if (parse_properties(properties, compute_required_size, (void*) &(context->permissions))) {
            if (!context->permissions.sources.capacity) {
                context->permissions.sources.capacity = 1;
            }
            if (!context->permissions.targets.capacity) {
                context->permissions.targets.capacity = 1;
            }
            allocate_buffer(&(context->permissions.targets));
            allocate_buffer(&(context->permissions.sources));
            parse_properties(properties, collect_permissions, (void*) &(context->permissions));
        }
        const pn_bytes_t authid = extract_authenticated_identity(properties);
        if (authid.start && authid.size) {
            context->username = strndup(authid.start, authid.size);
        } else {
            context->username = qd_strdup("");
        }
        //notify upstream connection of successful authentication
        notify_upstream(context, DOWNSTREAM_OUTCOME_RECEIVED);

        //close downstream connection
        pn_connection_close(conn);
        pn_transport_close_tail(transport);
        pn_transport_close_head(transport);
    } else if (pn_event_type(e) == PN_CONNECTION_REMOTE_CLOSE) {
        qd_log(auth_service_log, QD_LOG_DEBUG, "authentication service closed connection");
        pn_connection_close(conn);
        pn_transport_close_head(transport);
    } else if (pn_event_type(e) == PN_TRANSPORT_HEAD_CLOSED) {
        pn_transport_close_tail(transport);
    } else if (pn_event_type(e) == PN_TRANSPORT_TAIL_CLOSED) {
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
