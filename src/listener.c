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

#include "listener.h"

#include <proton/condition.h>
#include <proton/proactor.h>

// Listening backlog
static const int BACKLOG = 50;

ALLOC_DEFINE(qd_listener_t);

static bool listener_listen_pn(qd_listener_t* listener);
static bool listener_listen_http(qd_listener_t* listener);

bool qd_listener_listen(qd_listener_t* listener) {
    if (listener->pn_listener || listener->http) {
        // Already listening
        return true;
    }

    if (listener->config.http) {
        return listener_listen_http(listener);
    } else {
        return listener_listen_pn(listener);
    }
}

void qd_listener_decref(qd_listener_t* listener) {
    if (listener && sys_atomic_dec(&listener->ref_count) == 1) {
        qd_server_config_free(&listener->config);
        free_qd_listener_t(listener);
    }
}

// XXX Functions like this one tend not to be used much in the router
// source code
qd_http_listener_t* qd_listener_http(qd_listener_t* listener) {
    return listener->http;
}

static bool listener_listen_pn(qd_listener_t* listener) {
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
        qd_log(listener->server->log_source, QD_LOG_CRITICAL, "No memory listening on %s", listener->config.host_port);
    }

    return listener->pn_listener;
}

static bool listener_listen_http(qd_listener_t* listener) {
    if (listener->server->http) {
        // qd_http_listener holds a reference to listener.  It will
        // decref when closed.
        qd_http_server_listen(listener->server->http, listener);
        return listener->http;
    } else {
        qd_log(listener->server->log_source, QD_LOG_ERROR, "No HTTP support to listen on %s",
               listener->config.host_port);
        return false;
    }
}
