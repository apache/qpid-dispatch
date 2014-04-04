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

#include <qpid/dispatch/connection_manager.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/agent.h>
#include "dispatch_private.h"
#include "server_private.h"
#include <string.h>

static const char *CONF_LISTENER    = "listener";
static const char *CONF_CONNECTOR   = "connector";


struct qd_config_listener_t {
    DEQ_LINKS(qd_config_listener_t);
    qd_listener_t      *listener;
    qd_server_config_t  configuration;
};

DEQ_DECLARE(qd_config_listener_t, qd_config_listener_list_t);


struct qd_config_connector_t {
    DEQ_LINKS(qd_config_connector_t);
    void               *context;
    const char         *connector_name;
    qd_connector_t     *connector;
    qd_server_config_t  configuration;
    bool                started;
};

DEQ_DECLARE(qd_config_connector_t, qd_config_connector_list_t);


struct qd_connection_manager_t {
    qd_log_source_t             *log_source;
    qd_server_t                 *server;
    qd_config_listener_list_t    config_listeners;
    qd_config_connector_list_t   config_connectors;
    qd_config_connector_list_t   on_demand_connectors;
};


static void load_server_config(qd_dispatch_t *qd, qd_server_config_t *config, const char *section, int i)
{
    config->host           = qd_config_item_value_string(qd, section, i, "addr");
    config->port           = qd_config_item_value_string(qd, section, i, "port");
    config->role           = qd_config_item_value_string(qd, section, i, "role");
    config->max_frame_size = qd_config_item_value_int(qd, section, i, "max-frame-size");
    config->sasl_mechanisms =
        qd_config_item_value_string(qd, section, i, "sasl-mechanisms");
    config->ssl_enabled =
        qd_config_item_value_bool(qd, section, i, "ssl-profile");
    if (config->ssl_enabled) {
        config->ssl_server = 1;
        config->ssl_allow_unsecured_client =
            qd_config_item_value_bool(qd, section, i, "allow-unsecured");
        config->ssl_certificate_file =
            qd_config_item_value_string(qd, section, i, "cert-file");
        config->ssl_private_key_file =
            qd_config_item_value_string(qd, section, i, "key-file");
        config->ssl_password =
            qd_config_item_value_string(qd, section, i, "password");
        config->ssl_trusted_certificate_db =
            qd_config_item_value_string(qd, section, i, "cert-db");
        config->ssl_trusted_certificates =
            qd_config_item_value_string(qd, section, i, "trusted-certs");
        config->ssl_require_peer_authentication =
            qd_config_item_value_bool(qd, section, i, "require-peer-auth");
    }
}


static void configure_listeners(qd_dispatch_t *qd)
{
    int count;
    qd_connection_manager_t *cm = qd->connection_manager;

    if (!qd->config || !cm) {
        assert(false);
        return;
    }

    count = qd_config_item_count(qd, CONF_LISTENER);
    for (int i = 0; i < count; i++) {
        qd_config_listener_t *cl = NEW(qd_config_listener_t);
        cl->listener = 0;
        load_server_config(qd, &cl->configuration, CONF_LISTENER, i);
        DEQ_ITEM_INIT(cl);
        DEQ_INSERT_TAIL(cm->config_listeners, cl);
        qd_log(cm->log_source, QD_LOG_INFO, "Configured Listener: %s:%s role=%s",
               cl->configuration.host, cl->configuration.port, cl->configuration.role);
    }
}


static void configure_connectors(qd_dispatch_t *qd)
{
    int count;
    qd_connection_manager_t *cm = qd->connection_manager;

    if (!qd->config || !cm) {
        assert(false);
        return;
    }

    count = qd_config_item_count(qd, CONF_CONNECTOR);
    for (int i = 0; i < count; i++) {
        qd_config_connector_t *cc = NEW(qd_config_connector_t);
        cc->context        = 0;
        cc->connector      = 0;
        cc->connector_name = 0;
        cc->started        = false;
        load_server_config(qd, &cc->configuration, CONF_CONNECTOR, i);
        DEQ_ITEM_INIT(cc);
        if (strcmp(cc->configuration.role, "on-demand") == 0) {
            cc->connector_name =
                qd_config_item_value_string(qd, CONF_CONNECTOR, i, "name");
            DEQ_INSERT_TAIL(cm->on_demand_connectors, cc);
            qd_log(cm->log_source, QD_LOG_INFO, "Configured on-demand connector: %s:%s name=%s",
                   cc->configuration.host, cc->configuration.port, cc->connector_name);
        } else {
            DEQ_INSERT_TAIL(cm->config_connectors, cc);
            qd_log(cm->log_source, QD_LOG_INFO, "Configured Connector: %s:%s role=%s",
                   cc->configuration.host, cc->configuration.port, cc->configuration.role);
        }
    }
}


qd_connection_manager_t *qd_connection_manager(qd_dispatch_t *qd)
{
    qd_connection_manager_t *cm = NEW(qd_connection_manager_t);
    if (!cm)
        return 0;

    cm->log_source = qd_log_source("CONN_MGR");
    cm->server     = qd->server;
    DEQ_INIT(cm->config_listeners);
    DEQ_INIT(cm->config_connectors);
    DEQ_INIT(cm->on_demand_connectors);

    return cm;
}


void qd_connection_manager_free(qd_connection_manager_t *cm)
{
}


void qd_connection_manager_configure(qd_dispatch_t *qd)
{
    configure_listeners(qd);
    configure_connectors(qd);
}


void qd_connection_manager_start(qd_dispatch_t *qd)
{
    qd_config_listener_t  *cl = DEQ_HEAD(qd->connection_manager->config_listeners);
    qd_config_connector_t *cc = DEQ_HEAD(qd->connection_manager->config_connectors);

    while (cl) {
        if (cl->listener == 0)
            cl->listener = qd_server_listen(qd, &cl->configuration, cl);
        cl = DEQ_NEXT(cl);
    }

    while (cc) {
        if (cc->connector == 0)
            cc->connector = qd_server_connect(qd, &cc->configuration, cc);
        cc = DEQ_NEXT(cc);
    }
}


qd_config_connector_t *qd_connection_manager_find_on_demand(qd_dispatch_t *qd, const char *name)
{
    qd_config_connector_t *cc = DEQ_HEAD(qd->connection_manager->on_demand_connectors);

    while (cc) {
        if (strcmp(cc->connector_name, name) == 0)
            break;
        cc = DEQ_NEXT(cc);
    }

    return cc;
}


void qd_connection_manager_start_on_demand(qd_dispatch_t *qd, qd_config_connector_t *cc)
{
    if (cc && cc->connector == 0)
        cc->connector = qd_server_connect(qd, &cc->configuration, cc);
}


void qd_connection_manager_stop_on_demand(qd_dispatch_t *qd, qd_config_connector_t *cc)
{
}


static void server_schema_handler(void *context, void *cor)
{
    qd_agent_value_string(cor, 0, "state");
    qd_agent_value_string(cor, 0, "container");
    qd_agent_value_string(cor, 0, "host");
    qd_agent_value_string(cor, 0, "sasl");
    qd_agent_value_string(cor, 0, "role");
    qd_agent_value_string(cor, 0, "dir");
}


static void server_query_handler(void* context, const char *id, void *cor)
{
    qd_dispatch_t *qd        = (qd_dispatch_t*) context;
    qd_server_t   *qd_server = qd->server;
    sys_mutex_lock(qd_server->lock);
    const char               *conn_state;
    const qd_server_config_t *config;
    const char               *pn_container_name;
    const char               *direction;

    qd_connection_t *conn = DEQ_HEAD(qd_server->connections);
    while (conn) {
        switch (conn->state) {
        case CONN_STATE_CONNECTING:  conn_state = "Connecting";  break;
        case CONN_STATE_OPENING:     conn_state = "Opening";     break;
        case CONN_STATE_OPERATIONAL: conn_state = "Operational"; break;
        case CONN_STATE_FAILED:      conn_state = "Failed";      break;
        case CONN_STATE_USER:        conn_state = "User";        break;
        default:                     conn_state = "undefined";   break;
        }
        qd_agent_value_string(cor, "state", conn_state);
        // get remote container name using proton connection
        pn_container_name = pn_connection_remote_container(conn->pn_conn);
        if (pn_container_name)
            qd_agent_value_string(cor, "container", pn_container_name);
        else
            qd_agent_value_null(cor, "container");

        // and now for some config entries
        if (conn->connector) {
            config = conn->connector->config;
            direction = "out";
            char host[1000];
            strcpy(host, config->host);
            strcat(host, ":");
            strcat(host, config->port);
            qd_agent_value_string(cor, "host", host);
        } else {
            config = conn->listener->config;
            direction = "in";
            qd_agent_value_string(cor, "host", pn_connector_name(conn->pn_cxtr));
        }

        qd_agent_value_string(cor, "sasl", config->sasl_mechanisms);
        qd_agent_value_string(cor, "role", config->role);
        qd_agent_value_string(cor, "dir",  direction);

        conn = DEQ_NEXT(conn);
        qd_agent_value_complete(cor, conn != 0);
    }
    sys_mutex_unlock(qd_server->lock);
}


void qd_connection_manager_setup_agent(qd_dispatch_t *qd)
{
    qd_agent_register_class(qd, "org.apache.qpid.dispatch.connection", qd, server_schema_handler, server_query_handler);
}


void *qd_config_connector_context(qd_config_connector_t *cc)
{
    return cc ? cc->context : 0;
}


void qd_config_connector_set_context(qd_config_connector_t *cc, void *context)
{
    if (cc)
        cc->context = context;
}


const char *qd_config_connector_name(qd_config_connector_t *cc)
{
    return cc ? cc->connector_name : 0;
}

