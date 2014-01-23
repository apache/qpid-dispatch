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

#include <qpid/dispatch/python_embedded.h>
#include <qpid/dispatch.h>
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/ctools.h>
#include "dispatch_private.h"
#include "alloc_private.h"
#include "log_private.h"
#include "router_private.h"

/**
 * Private Function Prototypes
 */
qd_server_t    *qd_server(int tc, const char *container_name);
void            qd_server_setup_agent(qd_dispatch_t *qd);
void            qd_server_free(qd_server_t *server);
qd_container_t *qd_container(qd_dispatch_t *qd);
void            qd_container_setup_agent(qd_dispatch_t *qd);
void            qd_container_free(qd_container_t *container);
qd_router_t    *qd_router(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id);
void            qd_router_setup_late(qd_dispatch_t *qd);
void            qd_router_free(qd_router_t *router);
qd_agent_t     *qd_agent(qd_dispatch_t *qd);
void            qd_agent_free(qd_agent_t *agent);

ALLOC_DEFINE(qd_config_listener_t);
ALLOC_DEFINE(qd_config_connector_t);

static const char *CONF_CONTAINER   = "container";
static const char *CONF_ROUTER      = "router";
static const char *CONF_LISTENER    = "listener";
static const char *CONF_CONNECTOR   = "connector";


qd_dispatch_t *qd_dispatch(const char *python_pkgdir)
{
    qd_dispatch_t *qd = NEW(qd_dispatch_t);

    memset(qd, 0, sizeof(qd_dispatch_t));

    DEQ_INIT(qd->config_listeners);
    DEQ_INIT(qd->config_connectors);

    qd->router_area = "0";
    qd->router_id   = "0";
    qd->router_mode = QD_ROUTER_MODE_ENDPOINT;

    qd_python_initialize(qd, python_pkgdir);
    qd_log_initialize();
    qd_alloc_initialize();
    qd_config_initialize();
    qd->config = qd_config();

    return qd;
}


void qd_dispatch_extend_config_schema(qd_dispatch_t *qd, const char* text)
{
    qd_config_extend(qd->config, text);
}


void qd_dispatch_load_config(qd_dispatch_t *qd, const char *config_path)
{
    qd_config_read(qd->config, config_path);
}


void qd_dispatch_configure_container(qd_dispatch_t *qd)
{
    if (qd->config) {
        int count = qd_config_item_count(qd->config, CONF_CONTAINER);
        if (count == 1) {
            qd->thread_count   = qd_config_item_value_int(qd->config, CONF_CONTAINER, 0, "worker-threads");
            qd->container_name = qd_config_item_value_string(qd->config, CONF_CONTAINER, 0, "container-name");
        }
    }

    if (qd->thread_count == 0)
        qd->thread_count = 1;

    if (!qd->container_name)
        qd->container_name = "00000000-0000-0000-0000-000000000000";  // TODO - gen a real uuid
}


void qd_dispatch_configure_router(qd_dispatch_t *qd)
{
    const char *router_mode_str;

    if (qd->config) {
        int count = qd_config_item_count(qd->config, CONF_ROUTER);
        if (count == 1) {
            router_mode_str = qd_config_item_value_string(qd->config, CONF_ROUTER, 0, "mode");
            qd->router_id   = qd_config_item_value_string(qd->config, CONF_ROUTER, 0, "router-id");
        }
    }

    if (router_mode_str && strcmp(router_mode_str, "standalone") == 0)
        qd->router_mode = QD_ROUTER_MODE_STANDALONE;

    if (router_mode_str && strcmp(router_mode_str, "interior") == 0)
        qd->router_mode = QD_ROUTER_MODE_INTERIOR;

    if (router_mode_str && strcmp(router_mode_str, "edge") == 0)
        qd->router_mode = QD_ROUTER_MODE_EDGE;

    if (!qd->router_id)
        qd->router_id = qd->container_name;
}


void qd_dispatch_prepare(qd_dispatch_t *qd)
{
    qd->server    = qd_server(qd->thread_count, qd->container_name);
    qd->container = qd_container(qd);
    qd->router    = qd_router(qd, qd->router_mode, qd->router_area, qd->router_id);
    qd->agent     = qd_agent(qd);

    qd_alloc_setup_agent(qd);
    qd_server_setup_agent(qd);
    qd_container_setup_agent(qd);
    qd_router_setup_late(qd);
}


void qd_dispatch_free(qd_dispatch_t *qd)
{
    qd_config_free(qd->config);
    qd_config_finalize();
    qd_agent_free(qd->agent);
    qd_router_free(qd->router);
    qd_container_free(qd->container);
    qd_server_free(qd->server);
    qd_log_finalize();
    qd_python_finalize();
}


static void load_server_config(qd_dispatch_t *qd, qd_server_config_t *config, const char *section, int i)
{
    config->host = qd_config_item_value_string(qd->config, section, i, "addr");
    config->port = qd_config_item_value_string(qd->config, section, i, "port");
    config->role = qd_config_item_value_string(qd->config, section, i, "role");
    config->sasl_mechanisms =
        qd_config_item_value_string(qd->config, section, i, "sasl-mechanisms");
    config->ssl_enabled =
        qd_config_item_value_bool(qd->config, section, i, "ssl-profile");
    if (config->ssl_enabled) {
        config->ssl_server = 1;
        config->ssl_allow_unsecured_client =
            qd_config_item_value_bool(qd->config, section, i, "allow-unsecured");
        config->ssl_certificate_file =
            qd_config_item_value_string(qd->config, section, i, "cert-file");
        config->ssl_private_key_file =
            qd_config_item_value_string(qd->config, section, i, "key-file");
        config->ssl_password =
            qd_config_item_value_string(qd->config, section, i, "password");
        config->ssl_trusted_certificate_db =
            qd_config_item_value_string(qd->config, section, i, "cert-db");
        config->ssl_require_peer_authentication =
            qd_config_item_value_bool(qd->config, section, i, "require-peer-auth");
    }
}


static void configure_listeners(qd_dispatch_t *qd)
{
    int count;

    if (!qd->config)
        return;

    count = qd_config_item_count(qd->config, CONF_LISTENER);
    for (int i = 0; i < count; i++) {
        qd_config_listener_t *cl = new_qd_config_listener_t();
        load_server_config(qd, &cl->configuration, CONF_LISTENER, i);

        printf("\nListener   : %s:%s\n", cl->configuration.host, cl->configuration.port);
        printf("       SASL: %s\n", cl->configuration.sasl_mechanisms);
        printf("        SSL: %d\n", cl->configuration.ssl_enabled);
        if (cl->configuration.ssl_enabled) {
            printf("      unsec: %d\n", cl->configuration.ssl_allow_unsecured_client);
            printf("  cert-file: %s\n", cl->configuration.ssl_certificate_file);
            printf("   key-file: %s\n", cl->configuration.ssl_private_key_file);
            printf("    cert-db: %s\n", cl->configuration.ssl_trusted_certificate_db);
            printf("  peer-auth: %d\n", cl->configuration.ssl_require_peer_authentication);
        }

        cl->listener = qd_server_listen(qd, &cl->configuration, cl);
        DEQ_ITEM_INIT(cl);
        DEQ_INSERT_TAIL(qd->config_listeners, cl);
    }
}


static void configure_connectors(qd_dispatch_t *qd)
{
    int count;

    if (!qd->config)
        return;

    count = qd_config_item_count(qd->config, CONF_CONNECTOR);
    for (int i = 0; i < count; i++) {
        qd_config_connector_t *cc = new_qd_config_connector_t();
        load_server_config(qd, &cc->configuration, CONF_CONNECTOR, i);

        printf("\nConnector  : %s:%s\n", cc->configuration.host, cc->configuration.port);
        printf("       SASL: %s\n", cc->configuration.sasl_mechanisms);
        printf("        SSL: %d\n", cc->configuration.ssl_enabled);
        if (cc->configuration.ssl_enabled) {
            printf("  cert-file: %s\n", cc->configuration.ssl_certificate_file);
            printf("   key-file: %s\n", cc->configuration.ssl_private_key_file);
            printf("    cert-db: %s\n", cc->configuration.ssl_trusted_certificate_db);
            printf("  peer-auth: %d\n", cc->configuration.ssl_require_peer_authentication);
        }

        cc->connector = qd_server_connect(qd, &cc->configuration, cc);
        DEQ_ITEM_INIT(cc);
        DEQ_INSERT_TAIL(qd->config_connectors, cc);
    }
}


void qd_dispatch_post_configure_connections(qd_dispatch_t *qd)
{
    configure_listeners(qd);
    configure_connectors(qd);
}

