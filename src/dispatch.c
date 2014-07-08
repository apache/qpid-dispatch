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
#include "entity_private.h"
#include "static_assert.h"

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
void            qd_error_initialize();

qd_dispatch_t *qd_dispatch(const char *python_pkgdir, const char *qpid_dispatch_lib)
{
    qd_error_clear();
    qd_dispatch_t *qd = NEW(qd_dispatch_t);
    memset(qd, 0, sizeof(qd_dispatch_t));

    // alloc, log and error have to be initialized before any module.
    qd_alloc_initialize();
    qd_log_initialize();
    qd_error_initialize();
    if (qd_error_code()) return 0;

    qd->router_area = strdup("0");
    qd->router_id   = strdup("0");
    qd->router_mode = QD_ROUTER_MODE_ENDPOINT;

    qd_python_initialize(qd, python_pkgdir, qpid_dispatch_lib);
    if (qd_error_code()) { qd_dispatch_free(qd); return 0; }
    qd_message_initialize();
    if (qd_error_code()) { qd_dispatch_free(qd); return 0; }
    return qd;
}


// We pass pointers as longs via the python interface, make sure this is safe.
STATIC_ASSERT(sizeof(long) >= sizeof(void*), pointer_is_bigger_than_long);

qd_error_t qd_dispatch_load_config(qd_dispatch_t *qd, const char *config_path)
{
    PyObject *module = 0, *configure_dispatch = 0, *result = 0;
    bool ok =
        (module = PyImport_ImportModule("qpid_dispatch_internal.management")) &&
	(configure_dispatch = PyObject_GetAttrString(module, "configure_dispatch")) &&
	(result = PyObject_CallFunction(configure_dispatch, "(ls)", (long)qd, config_path));
    Py_XDECREF(module);
    Py_XDECREF(configure_dispatch);
    Py_XDECREF(result);
    return ok ? QD_ERROR_NONE : qd_error_py();
}


qd_error_t qd_dispatch_configure_container(qd_dispatch_t *qd, qd_entity_t *entity)
{
    const char *default_name = "00000000-0000-0000-0000-000000000000";
    qd->thread_count   = qd_entity_opt_long(entity, "worker-threads", 1); QD_ERROR_RET();
    qd->container_name = qd_entity_opt_string(entity, "container-name", default_name); QD_ERROR_RET();
    return QD_ERROR_NONE;
}


qd_error_t qd_dispatch_configure_router(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_error_clear();
    free(qd->router_id);
    qd->router_id   = qd_entity_opt_string(entity, "router-id", qd->container_name);
    QD_ERROR_RET();
    qd->router_mode = qd_entity_long(entity, "mode");
    return qd_error_code();
}

qd_error_t qd_dispatch_configure_address(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_address(qd->router, entity);
    return qd_error_code();
}

qd_error_t qd_dispatch_configure_waypoint(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_waypoint(qd->router, entity);
    return qd_error_code();
}

qd_error_t qd_dispatch_prepare(qd_dispatch_t *qd)
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
    return qd_error_code();
}


void qd_dispatch_free(qd_dispatch_t *qd)
{
    if (!qd) return;
    free(qd->router_id);
    free(qd->container_name);
    free(qd->router_area);
    qd_connection_manager_free(qd->connection_manager);
    qd_agent_free(qd->agent);
    qd_router_free(qd->router);
    qd_container_free(qd->container);
    qd_server_free(qd->server);
    qd_log_finalize();
    qd_alloc_finalize();
    qd_python_finalize();
}
