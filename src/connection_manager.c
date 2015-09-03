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
#include "dispatch_private.h"
#include "connection_manager_private.h"
#include "server_private.h"
#include "entity.h"
#include "entity_cache.h"
#include "schema_enum.h"
#include <string.h>
#include <stdio.h>

struct qd_config_listener_t {
    bool is_connector;
    DEQ_LINKS(qd_config_listener_t);
    qd_listener_t      *listener;
    qd_server_config_t  configuration;
};

DEQ_DECLARE(qd_config_listener_t, qd_config_listener_list_t);


struct qd_config_connector_t {
    bool is_connector;
    DEQ_LINKS(qd_config_connector_t);
    void                            *context;
    const char                      *connector_name;
    qd_connector_t                  *connector;
    qd_server_config_t               configuration;
    bool                             started;
    qd_connection_manager_handler_t  open_handler;
    qd_connection_manager_handler_t  close_handler;
    void                            *handler_context;
};

DEQ_DECLARE(qd_config_connector_t, qd_config_connector_list_t);


struct qd_connection_manager_t {
    qd_log_source_t             *log_source;
    qd_server_t                 *server;
    qd_config_listener_list_t    config_listeners;
    qd_config_connector_list_t   config_connectors;
    qd_config_connector_list_t   on_demand_connectors;
};


// True if entity has any of attributes.
static bool has_attrs(qd_entity_t *entity, const char **attributes, int n) {
    for (int i = 0; i < n; ++i)
        if (qd_entity_has(entity, attributes[i])) return true;
    return false;
}

static const char *ssl_attributes[] = {
  "certDb", "certFile", "keyFile", "passwordFile", "password"
};
static const int ssl_attributes_count = sizeof(ssl_attributes)/sizeof(ssl_attributes[0]);

static void qd_server_config_free(qd_server_config_t *cf)
{
    if (!cf) return;
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
    memset(cf, 0, sizeof(*cf));
}

#define CHECK() if (qd_error_code()) goto error
static qd_error_t load_server_config(qd_dispatch_t *qd, qd_server_config_t *config, qd_entity_t* entity)
{
    qd_error_clear();

    bool authenticatePeer   = qd_entity_opt_bool(entity, "authenticatePeer",  false); CHECK();
    bool requireEncryption  = qd_entity_opt_bool(entity, "requireEncryption", false); CHECK();
    bool requireSsl         = qd_entity_opt_bool(entity, "requireSsl",        false); CHECK();
    bool depRequirePeerAuth = qd_entity_opt_bool(entity, "requirePeerAuth",   false); CHECK();
    bool depAllowUnsecured  = qd_entity_opt_bool(entity, "allowUnsecured", !requireSsl); CHECK();

    memset(config, 0, sizeof(*config));
    config->host            = qd_entity_get_string(entity, "addr"); CHECK();
    config->port            = qd_entity_get_string(entity, "port"); CHECK();
    config->role            = qd_entity_get_string(entity, "role"); CHECK();
    config->max_frame_size  = qd_entity_get_long(entity, "maxFrameSize"); CHECK();
    config->sasl_mechanisms = qd_entity_opt_string(entity, "saslMechanisms", 0); CHECK();
    config->ssl_enabled = has_attrs(entity, ssl_attributes, ssl_attributes_count);

    //
    // For now we are hardwiring this attribute to true.  If there's an outcry from the
    // user community, we can revisit this later.
    //
    config->allowInsecureAuthentication = true;

    config->requireAuthentication = authenticatePeer || depRequirePeerAuth;
    config->requireEncryption     = requireEncryption || !depAllowUnsecured;

    if (config->ssl_enabled) {
        config->ssl_required = requireSsl || !depAllowUnsecured;
        config->ssl_require_peer_authentication = config->sasl_mechanisms &&
            strstr(config->sasl_mechanisms, "EXTERNAL") != 0;
        config->ssl_certificate_file =
            qd_entity_opt_string(entity, "certFile", 0); CHECK();
        config->ssl_private_key_file =
            qd_entity_opt_string(entity, "keyFile", 0); CHECK();
        config->ssl_password =
            qd_entity_opt_string(entity, "password", 0); CHECK();
        config->ssl_trusted_certificate_db =
            qd_entity_opt_string(entity, "certDb", 0); CHECK();
        config->ssl_trusted_certificates =
            qd_entity_opt_string(entity, "trustedCerts", 0); CHECK();
    }

    return QD_ERROR_NONE;

  error:
    qd_server_config_free(config);
    return qd_error_code();
}

void qd_dispatch_configure_listener(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_connection_manager_t *cm = qd->connection_manager;
    qd_config_listener_t *cl = NEW(qd_config_listener_t);
    cl->is_connector = false;
    cl->listener = 0;
    load_server_config(qd, &cl->configuration, entity);
    DEQ_ITEM_INIT(cl);
    DEQ_INSERT_TAIL(cm->config_listeners, cl);
    qd_log(cm->log_source, QD_LOG_INFO, "Configured Listener: %s:%s role=%s",
           cl->configuration.host, cl->configuration.port, cl->configuration.role);
}


qd_error_t qd_dispatch_configure_connector(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_error_clear();
    qd_connection_manager_t *cm = qd->connection_manager;
    qd_config_connector_t *cc = NEW(qd_config_connector_t);
    memset(cc, 0, sizeof(*cc));
    cc->is_connector = true;
    if (load_server_config(qd, &cc->configuration, entity))
        return qd_error_code();
    DEQ_ITEM_INIT(cc);
    if (strcmp(cc->configuration.role, "on-demand") == 0) {
        cc->connector_name = qd_entity_get_string(entity, "name"); QD_ERROR_RET();
        DEQ_INSERT_TAIL(cm->on_demand_connectors, cc);
        qd_log(cm->log_source, QD_LOG_INFO, "Configured on-demand connector: %s:%s name=%s",
               cc->configuration.host, cc->configuration.port, cc->connector_name);
    } else {
        DEQ_INSERT_TAIL(cm->config_connectors, cc);
        qd_log(cm->log_source, QD_LOG_INFO, "Configured Connector: %s:%s role=%s",
               cc->configuration.host, cc->configuration.port, cc->configuration.role);
    }
    return QD_ERROR_NONE;
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
    if (!cm) return;
    qd_config_listener_t *cl = DEQ_HEAD(cm->config_listeners);
    while (cl) {
        DEQ_REMOVE_HEAD(cm->config_listeners);
        qd_server_listener_free(cl->listener);
        qd_server_config_free(&cl->configuration);
        free(cl);
        cl = DEQ_HEAD(cm->config_listeners);
    }

    qd_config_connector_t *cc = DEQ_HEAD(cm->config_connectors);
    while(cc) {
        DEQ_REMOVE_HEAD(cm->config_connectors);
        qd_server_connector_free(cc->connector);
        qd_server_config_free(&cc->configuration);
        free(cc);
        cc = DEQ_HEAD(cm->config_connectors);
    }

    qd_config_connector_t *odc = DEQ_HEAD(cm->on_demand_connectors);
    while(odc) {
        DEQ_REMOVE_HEAD(cm->on_demand_connectors);
        if (odc->connector)
            qd_server_connector_free(odc->connector);
        qd_server_config_free(&odc->configuration);
        free(odc);
        odc = DEQ_HEAD(cm->on_demand_connectors);
    }
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


void qd_connection_manager_set_handlers(qd_config_connector_t *cc,
                                        qd_connection_manager_handler_t open_handler,
                                        qd_connection_manager_handler_t close_handler,
                                        void *context)
{
    if (cc) {
        cc->open_handler    = open_handler;
        cc->close_handler   = close_handler;
        cc->handler_context = context;
    }
}


void qd_connection_manager_start_on_demand(qd_dispatch_t *qd, qd_config_connector_t *cc)
{
    if (cc && cc->connector == 0) {
        qd_log(qd->connection_manager->log_source, QD_LOG_INFO, "Starting on-demand connector: %s",
               cc->connector_name);
        cc->connector = qd_server_connect(qd, &cc->configuration, cc);
    }
}


void qd_connection_manager_stop_on_demand(qd_dispatch_t *qd, qd_config_connector_t *cc)
{
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


void qd_connection_manager_connection_opened(qd_connection_t *conn)
{
    qd_config_connector_t *cc = (qd_config_connector_t*) qd_connection_get_config_context(conn);
    if (cc && cc->is_connector && cc->open_handler)
        cc->open_handler(cc->handler_context, conn);
}


void qd_connection_manager_connection_closed(qd_connection_t *conn)
{
    qd_config_connector_t *cc = (qd_config_connector_t*) qd_connection_get_config_context(conn);
    if (cc && cc->is_connector && cc->close_handler)
        cc->close_handler(cc->handler_context, conn);
}

