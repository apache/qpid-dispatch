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
#include "waypoint_private.h"
#include "message_private.h"

/**
 * Private Function Prototypes
 */
qd_server_t    *qd_server(int tc, const char *container_name);
void            qd_connection_manager_setup_agent(qd_dispatch_t *qd);
void            qd_server_free(qd_server_t *server);
qd_container_t *qd_container(qd_dispatch_t *qd);
void            qd_container_setup_agent(qd_dispatch_t *qd);
void            qd_container_free(qd_container_t *container);
qd_router_t    *qd_router(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id);
void            qd_router_setup_late(qd_dispatch_t *qd);
void            qd_router_free(qd_router_t *router);
qd_agent_t     *qd_agent(qd_dispatch_t *qd);
void            qd_agent_free(qd_agent_t *agent);


static const char *CONF_CONTAINER   = "container";
static const char *CONF_ROUTER      = "router";


qd_dispatch_t *qd_dispatch(const char *python_pkgdir)
{
    qd_dispatch_t *qd = NEW(qd_dispatch_t);

    memset(qd, 0, sizeof(qd_dispatch_t));

    // alloc and log has to be initialized before any module.
    qd_alloc_initialize();
    qd_log_initialize(); 

    qd->router_area = strdup("0");
    qd->router_id   = strdup("0");
    qd->router_mode = QD_ROUTER_MODE_ENDPOINT;

    qd_python_initialize(qd, python_pkgdir);
    qd_config_initialize();
    qd_message_initialize();
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
        int count = qd_config_item_count(qd, CONF_CONTAINER);
        if (count == 1) {
            qd->thread_count   = qd_config_item_value_int(qd, CONF_CONTAINER, 0, "worker-threads");
            qd->container_name = qd_config_item_value_string(qd, CONF_CONTAINER, 0, "container-name");
        }
    }

    if (qd->thread_count == 0)
        qd->thread_count = 1;

    if (!qd->container_name)
        qd->container_name = "00000000-0000-0000-0000-000000000000";  // TODO - gen a real uuid
}


void qd_dispatch_configure_router(qd_dispatch_t *qd)
{
    char *router_mode_str = 0;

    if (qd->config) {
        int count = qd_config_item_count(qd, CONF_ROUTER);
        if (count == 1) {
            router_mode_str = qd_config_item_value_string(qd, CONF_ROUTER, 0, "mode");
            qd->router_id   = qd_config_item_value_string(qd, CONF_ROUTER, 0, "router-id");
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

    free(router_mode_str);
}


void qd_dispatch_prepare(qd_dispatch_t *qd)
{
    qd->server             = qd_server(qd->thread_count, qd->container_name);
    qd->container          = qd_container(qd);
    qd->router             = qd_router(qd, qd->router_mode, qd->router_area, qd->router_id);
    qd->agent              = qd_agent(qd);
    qd->connection_manager = qd_connection_manager(qd);

    qd_alloc_setup_agent(qd);
    qd_connection_manager_setup_agent(qd);
    qd_container_setup_agent(qd);
    qd_router_setup_late(qd);
}


void qd_dispatch_free(qd_dispatch_t *qd)
{
    free(qd->router_id);
    free(qd->router_area);
    qd_config_free(qd->config);
    qd_config_finalize();
    qd_connection_manager_free(qd->connection_manager);
    qd_agent_free(qd->agent);
    qd_router_free(qd->router);
    qd_container_free(qd->container);
    qd_server_free(qd->server);
    qd_log_finalize();
    qd_alloc_finalize();
    qd_python_finalize();
}


void qd_dispatch_post_configure_connections(qd_dispatch_t *qd)
{
    qd_connection_manager_configure(qd);
    qd_connection_manager_start(qd);
    qd_waypoint_activate_all(qd);
}
