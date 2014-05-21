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
        qd_log(cm->log_source, QD_LOG_ERROR, "Cannot configure listeners%s%s",
	       (qd->config ? "" : ", no configuration"),
	       (cm ? "" : ", no connection manager"));
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


static void qd_connection_manager_config_free(qd_server_config_t *cf)
{
    free(cf->host);
    free(cf->port);
    free(cf->role);
    free(cf->sasl_mechanisms);
    if (cf->ssl_enabled) {
        free(cf->ssl_certificate_file);
        free(cf->ssl_private_key_file);
        free(cf->ssl_password);
        free(cf->ssl_trusted_certificate_db);
        free(cf->ssl_trusted_certificates);
    }
}


void qd_connection_manager_free(qd_connection_manager_t *cm)
{
    qd_config_listener_t *cl = DEQ_HEAD(cm->config_listeners);
    while (cl) {
        DEQ_REMOVE_HEAD(cm->config_listeners);
        qd_server_listener_free(cl->listener);
        qd_connection_manager_config_free(&cl->configuration);
        free(cl);
        cl = DEQ_HEAD(cm->config_listeners);
    }

    qd_config_connector_t *cc = DEQ_HEAD(cm->config_connectors);
    while(cc) {
        DEQ_REMOVE_HEAD(cm->config_connectors);
        qd_server_connector_free(cc->connector);
        qd_connection_manager_config_free(&cc->configuration);
        free(cc);
        cc = DEQ_HEAD(cm->config_connectors);
    }

    qd_config_connector_t *odc = DEQ_HEAD(cm->on_demand_connectors);
    while(odc) {
        DEQ_REMOVE_HEAD(cm->on_demand_connectors);
        if (odc->connector)
            qd_server_connector_free(odc->connector);
        qd_connection_manager_config_free(&odc->configuration);
        free(odc);
        odc = DEQ_HEAD(cm->on_demand_connectors);
    }
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

static void cm_attr_name(void *object_handle, void *cor, void *unused)
{
    qd_connection_t *conn = (qd_connection_t*) object_handle;
    qd_agent_value_string(cor, 0, pn_connector_name(conn->pn_cxtr));
}


static void cm_attr_state(void *object_handle, void *cor, void *unused)
{
    qd_connection_t *conn = (qd_connection_t*) object_handle;
    switch (conn->state) {
    case CONN_STATE_CONNECTING:  qd_agent_value_string(cor, 0, "Connecting");  break;
    case CONN_STATE_OPENING:     qd_agent_value_string(cor, 0, "Opening");     break;
    case CONN_STATE_OPERATIONAL: qd_agent_value_string(cor, 0, "Operational"); break;
    case CONN_STATE_FAILED:      qd_agent_value_string(cor, 0, "Failed");      break;
    case CONN_STATE_USER:        qd_agent_value_string(cor, 0, "User");        break;
    default:                     qd_agent_value_string(cor, 0, "undefined");   break;
    }
}


static void cm_attr_container(void *object_handle, void *cor, void *unused)
{
    qd_connection_t *conn = (qd_connection_t*) object_handle;
    // get remote container name using proton connection
    const char *container_name = pn_connection_remote_container(conn->pn_conn);
    if (container_name)
        qd_agent_value_string(cor, 0, container_name);
    else
        qd_agent_value_null(cor, 0);
}


static void cm_attr_host(void *object_handle, void *cor, void *unused)
{
    qd_connection_t *conn = (qd_connection_t*) object_handle;
    const qd_server_config_t *config;
    if (conn->connector)
        config = conn->connector->config;
    else
        config = conn->listener->config;
    if (conn->connector) {
        char host[1000];
        strcpy(host, config->host);
        strcat(host, ":");
        strcat(host, config->port);
        qd_agent_value_string(cor, 0, host);
    } else
        qd_agent_value_string(cor, 0, pn_connector_name(conn->pn_cxtr));
}


static void cm_attr_sasl(void *object_handle, void *cor, void *unused)
{
    qd_connection_t *conn = (qd_connection_t*) object_handle;
    const qd_server_config_t *config;
    if (conn->connector)
        config = conn->connector->config;
    else
        config = conn->listener->config;
    qd_agent_value_string(cor, 0, config->sasl_mechanisms);
}


static void cm_attr_role(void *object_handle, void *cor, void *unused)
{
    qd_connection_t *conn = (qd_connection_t*) object_handle;
    const qd_server_config_t *config;
    if (conn->connector)
        config = conn->connector->config;
    else
        config = conn->listener->config;
    qd_agent_value_string(cor, 0, config->role);
}


static void cm_attr_dir(void *object_handle, void *cor, void *unused)
{
    qd_connection_t *conn = (qd_connection_t*) object_handle;
    if (conn->connector)
        qd_agent_value_string(cor, 0, "out");
    else
        qd_agent_value_string(cor, 0, "in");
}


static const char *CONN_TYPE = "org.apache.qpid.dispatch.connection";
static const qd_agent_attribute_t CONN_ATTRIBUTES[] =
    {{"name", cm_attr_name, 0},
     {"identity", cm_attr_name, 0},
     {"state", cm_attr_state, 0},
     {"container", cm_attr_container, 0},
     {"host", cm_attr_host, 0},
     {"sasl", cm_attr_sasl, 0},
     {"role", cm_attr_role, 0},
     {"dir", cm_attr_dir, 0},
     {0, 0, 0}};


static void server_query_handler(void* context, void *cor)
{
    qd_dispatch_t *qd        = (qd_dispatch_t*) context;
    qd_server_t   *qd_server = qd->server;
    sys_mutex_lock(qd_server->lock);

    qd_connection_t *conn = DEQ_HEAD(qd_server->connections);
    while (conn) {
        if (!qd_agent_object(cor, (void*) conn))
            break;
        conn = DEQ_NEXT(conn);
    }
    sys_mutex_unlock(qd_server->lock);
}


void qd_connection_manager_setup_agent(qd_dispatch_t *qd)
{
    qd_agent_register_class(qd, CONN_TYPE, qd, CONN_ATTRIBUTES, server_query_handler);
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

