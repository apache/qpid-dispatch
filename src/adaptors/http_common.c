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

#include "http_common.h"

#include <proton/listener.h>

#include <stdio.h>

ALLOC_DECLARE(qd_http_lsnr_t);
ALLOC_DEFINE(qd_http_lsnr_t);
ALLOC_DECLARE(qd_http_connector_t);
ALLOC_DEFINE(qd_http_connector_t);


static qd_error_t load_bridge_config(qd_dispatch_t *qd, qd_http_bridge_config_t *config, qd_entity_t* entity)
{
    char *version_str = 0;

    qd_error_clear();
    ZERO(config);

#define CHECK() if (qd_error_code()) goto error
    config->name    = qd_entity_get_string(entity, "name");            CHECK();
    config->host    = qd_entity_get_string(entity, "host");            CHECK();
    config->port    = qd_entity_get_string(entity, "port");            CHECK();
    config->address = qd_entity_get_string(entity, "address");         CHECK();
    version_str     = qd_entity_get_string(entity, "protocolVersion");  CHECK();

    if (strcmp(version_str, "HTTP2") == 0) {
        config->version = VERSION_HTTP2;
    } else {
        config->version = VERSION_HTTP1;
    }
    free(version_str);
    version_str = 0;

    int hplen = strlen(config->host) + strlen(config->port) + 2;
    config->host_port = malloc(hplen);
    snprintf(config->host_port, hplen, "%s:%s", config->host, config->port);

    return QD_ERROR_NONE;

error:
    qd_http_free_bridge_config(config);
    free(version_str);
    return qd_error_code();
}


void qd_http_free_bridge_config(qd_http_bridge_config_t *config)
{
    if (!config) {
        return;
    }
    free(config->host);
    free(config->port);
    free(config->name);
    free(config->address);
    free(config->host_port);
}


//
// HTTP Listener Management (HttpListenerEntity)
//


qd_http_lsnr_t *qd_dispatch_configure_http_lsnr(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_http_lsnr_t *listener = 0;
    qd_http_bridge_config_t config;

    if (load_bridge_config(qd, &config, entity) != QD_ERROR_NONE) {
        qd_log(qd_log_source(QD_HTTP_LOG_SOURCE), QD_LOG_ERROR,
               "Unable to create http listener: %s", qd_error_message());
        return 0;
    }

    switch (config.version) {
    case VERSION_HTTP1:
        listener = qd_http1_configure_listener(qd, &config, entity);
        break;
    case VERSION_HTTP2:
        listener = qd_http2_configure_listener(qd, &config, entity);
        break;
    }

    if (!listener)
        qd_http_free_bridge_config(&config);

    return listener;
}


void qd_dispatch_delete_http_listener(qd_dispatch_t *qd, void *impl)
{
    qd_http_lsnr_t *listener = (qd_http_lsnr_t*) impl;
    if (listener) {
        switch (listener->config.version) {
        case VERSION_HTTP1:
            qd_http1_delete_listener(qd, listener);
            break;
        case VERSION_HTTP2:
            qd_http2_delete_listener(qd, listener);
            break;
        }
    }
}


qd_error_t qd_entity_refresh_httpListener(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}


//
// HTTP Connector Management (HttpConnectorEntity)
//


qd_http_connector_t *qd_dispatch_configure_http_connector(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_http_connector_t *conn = 0;
    qd_http_bridge_config_t config;

    if (load_bridge_config(qd, &config, entity) != QD_ERROR_NONE) {
        qd_log(qd_log_source(QD_HTTP_LOG_SOURCE), QD_LOG_ERROR,
               "Unable to create http connector: %s", qd_error_message());
        return 0;
    }

    switch (config.version) {
    case VERSION_HTTP1:
        conn = qd_http1_configure_connector(qd, &config, entity);
        break;
    case VERSION_HTTP2:
        conn = qd_http2_configure_connector(qd, &config, entity);
        break;
    }

    if (!conn)
        qd_http_free_bridge_config(&config);

    return conn;
}


void qd_dispatch_delete_http_connector(qd_dispatch_t *qd, void *impl)
{
    qd_http_connector_t *conn = (qd_http_connector_t*) impl;

    if (conn) {
        switch (conn->config.version) {
        case VERSION_HTTP1:
            qd_http1_delete_connector(qd, conn);
            break;
        case VERSION_HTTP2:
            qd_http2_delete_connector(qd, conn);
            break;
        }
    }
}

qd_error_t qd_entity_refresh_httpConnector(qd_entity_t* entity, void *impl)
{
    return QD_ERROR_NONE;
}

//
// qd_http_lsnr_t constructor
//

qd_http_lsnr_t *qd_http_lsnr(qd_server_t *server, qd_server_event_handler_t handler)
{
    qd_http_lsnr_t *li = new_qd_http_lsnr_t();
    if (!li)
        return 0;
    ZERO(li);

    li->pn_listener = pn_listener();
    if (!li->pn_listener) {
        free_qd_http_lsnr_t(li);
        return 0;
    }

    sys_atomic_init(&li->ref_count, 1);
    li->server = server;
    li->context.context = li;
    li->context.handler = handler;
    pn_listener_set_context(li->pn_listener, &li->context);

    return li;
}

void qd_http_listener_decref(qd_http_lsnr_t* li)
{
    if (li && sys_atomic_dec(&li->ref_count) == 1) {
        qd_http_free_bridge_config(&li->config);
        free_qd_http_lsnr_t(li);
    }
}

//
// qd_http_connector_t constructor
//

qd_http_connector_t *qd_http_connector(qd_server_t *server)
{
    qd_http_connector_t *c = new_qd_http_connector_t();
    if (!c) return 0;
    ZERO(c);
    sys_atomic_init(&c->ref_count, 1);
    c->server      = server;
    return c;
}

void qd_http_connector_decref(qd_http_connector_t* c)
{
    if (c && sys_atomic_dec(&c->ref_count) == 1) {
        qd_http_free_bridge_config(&c->config);
        free_qd_http_connector_t(c);
    }
}


