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

#include <Python.h>
#include <qpid/dispatch/python_embedded.h>
#include <qpid/dispatch.h>
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/static_assert.h>

#include "config.h"
#include "dispatch_private.h"
#include "alloc.h"
#include "log_private.h"
#include "router_private.h"
#include "message_private.h"
#include "policy.h"
#include "entity.h"
#include "entity_cache.h"
#include <dlfcn.h>

/**
 * Private Function Prototypes
 */
qd_server_t    *qd_server(qd_dispatch_t *qd, int tc, const char *container_name,
                          const char *sasl_config_path, const char *sasl_config_name);
void            qd_server_free(qd_server_t *server);
qd_container_t *qd_container(qd_dispatch_t *qd);
void            qd_container_free(qd_container_t *container);
qd_policy_t    *qd_policy(qd_dispatch_t *qd);
void            qd_policy_free(qd_policy_t *policy);
qd_router_t    *qd_router(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id);
void            qd_router_setup_late(qd_dispatch_t *qd);
void            qd_router_free(qd_router_t *router);
void            qd_error_initialize();

qd_dispatch_t *qd_dispatch(const char *python_pkgdir)
{
    qd_dispatch_t *qd = NEW(qd_dispatch_t);
    memset(qd, 0, sizeof(qd_dispatch_t));

    qd_entity_cache_initialize();   /* Must be first */
    qd_alloc_initialize();
    qd_log_initialize();
    qd_error_initialize();
    if (qd_error_code()) { qd_dispatch_free(qd); return 0; }

    qd->router_area = strdup("0");
    qd->router_id   = strdup("0");
    qd->router_mode = QD_ROUTER_MODE_ENDPOINT;

    qd_python_initialize(qd, python_pkgdir);
    if (qd_error_code()) { qd_dispatch_free(qd); return 0; }
    qd_message_initialize();
    if (qd_error_code()) { qd_dispatch_free(qd); return 0; }
    qd->dl_handle = 0;
    return qd;
}


// We pass pointers as longs via the python interface, make sure this is safe.
STATIC_ASSERT(sizeof(long) >= sizeof(void*), pointer_is_bigger_than_long);

qd_error_t qd_dispatch_load_config(qd_dispatch_t *qd, const char *config_path)
{
    qd->dl_handle = dlopen(QPID_DISPATCH_LIB, RTLD_LAZY | RTLD_NOLOAD);
    if (!qd->dl_handle)
        return qd_error(QD_ERROR_RUNTIME, "Cannot locate library %s", QPID_DISPATCH_LIB);

    qd_python_lock_state_t lock_state = qd_python_lock();
    PyObject *module = PyImport_ImportModule("qpid_dispatch_internal.management.config");
    PyObject *configure_dispatch = module ? PyObject_GetAttrString(module, "configure_dispatch") : NULL;
    Py_XDECREF(module);
    PyObject *result = configure_dispatch ? PyObject_CallFunction(configure_dispatch, "(lls)", (long)qd, qd->dl_handle, config_path) : NULL;
    Py_XDECREF(configure_dispatch);
    if (!result) qd_error_py();
    Py_XDECREF(result);
    qd_python_unlock(lock_state);
    return qd_error_code();
}

/**
 * The Container Entity has been deprecated and will be removed in the future. Use the RouterEntity instead.
 */
qd_error_t qd_dispatch_configure_container(qd_dispatch_t *qd, qd_entity_t *entity)
{
    // Add a log warning. Check to see if too early
    qd->thread_count   = qd_entity_opt_long(entity, "workerThreads", 4); QD_ERROR_RET();
    qd->sasl_config_path = qd_entity_opt_string(entity, "saslConfigPath", 0); QD_ERROR_RET();
    qd->sasl_config_name = qd_entity_opt_string(entity, "saslConfigName", 0); QD_ERROR_RET();
    char *dump_file = qd_entity_opt_string(entity, "debugDump", 0); QD_ERROR_RET();
    if (dump_file) {
        qd_alloc_debug_dump(dump_file); QD_ERROR_RET();
        free(dump_file);
    }

    return QD_ERROR_NONE;
}


qd_error_t qd_dispatch_configure_router(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd->router_id = qd_entity_opt_string(entity, "routerId", 0); QD_ERROR_RET();
    if (! qd->router_id)
        qd->router_id = qd_entity_opt_string(entity, "id", 0); QD_ERROR_RET();
    assert(qd->router_id);
    qd->router_mode = qd_entity_get_long(entity, "mode"); QD_ERROR_RET();
    qd->thread_count = qd_entity_opt_long(entity, "workerThreads", 4); QD_ERROR_RET();

    if (! qd->sasl_config_path)
        qd->sasl_config_path = qd_entity_opt_string(entity, "saslConfigPath", 0); QD_ERROR_RET();
    if (! qd->sasl_config_name)
        qd->sasl_config_name = qd_entity_opt_string(entity, "saslConfigName", "qdrouterd"); QD_ERROR_RET();

    char *dump_file = qd_entity_opt_string(entity, "debugDump", 0); QD_ERROR_RET();
    if (dump_file) {
        qd_alloc_debug_dump(dump_file); QD_ERROR_RET();
        free(dump_file);
    }

    return QD_ERROR_NONE;

}

qd_error_t qd_dispatch_configure_fixed_address(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_fixed_address(qd->router, entity);
    return qd_error_code();
}

qd_error_t qd_dispatch_configure_waypoint(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_waypoint(qd->router, entity);
    return qd_error_code();
}

qd_error_t qd_dispatch_configure_lrp(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_lrp(qd->router, entity);
    return qd_error_code();
}

qd_error_t qd_dispatch_configure_address(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_address(qd->router, entity);
    return qd_error_code();
}

qd_error_t qd_dispatch_configure_link_route(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_link_route(qd->router, entity);
    return qd_error_code();
}

qd_error_t qd_dispatch_configure_auto_link(qd_dispatch_t *qd, qd_entity_t *entity) {
    if (!qd->router) return qd_error(QD_ERROR_NOT_FOUND, "No router available");
    qd_router_configure_auto_link(qd->router, entity);
    return qd_error_code();
}

qd_error_t qd_dispatch_configure_policy(qd_dispatch_t *qd, qd_entity_t *entity)
{
    qd_error_t err;
    err = qd_entity_configure_policy(qd->policy, entity);
    if (err)
        return err;
    return QD_ERROR_NONE;
}


qd_error_t qd_dispatch_register_policy_manager(qd_dispatch_t *qd, qd_entity_t *entity)
{
    return qd_register_policy_manager(qd->policy, entity);
}


qd_error_t qd_dispatch_register_display_name_service(qd_dispatch_t *qd, void *object)
{
    return qd_register_display_name_service(qd, object);
}


long qd_dispatch_policy_c_counts_alloc()
{
    return qd_policy_c_counts_alloc();
}


void qd_dispatch_policy_c_counts_free(long ccounts)
{
    qd_policy_c_counts_free(ccounts);
}

void qd_dispatch_policy_c_counts_refresh(long ccounts, qd_entity_t *entity)
{
    qd_policy_c_counts_refresh(ccounts, entity);
}

qd_error_t qd_dispatch_prepare(qd_dispatch_t *qd)
{
    qd->server             = qd_server(qd, qd->thread_count, qd->router_id, qd->sasl_config_path, qd->sasl_config_name);
    qd->container          = qd_container(qd);
    qd->router             = qd_router(qd, qd->router_mode, qd->router_area, qd->router_id);
    qd->connection_manager = qd_connection_manager(qd);
    qd->policy             = qd_policy(qd);
    return qd_error_code();
}

void qd_dispatch_set_agent(qd_dispatch_t *qd, void *agent) {
    assert(agent);
    assert(!qd->agent);
    qd->agent = agent;
}

void qd_dispatch_free(qd_dispatch_t *qd)
{
    if (!qd) return;
    free(qd->router_id);
    free(qd->router_area);
    qd_connection_manager_free(qd->connection_manager);
    qd_policy_free(qd->policy);
    Py_XDECREF((PyObject*) qd->agent);
    qd_router_free(qd->router);
    qd_container_free(qd->container);
    qd_server_free(qd->server);
    qd_log_finalize();
    qd_alloc_finalize();
    qd_python_finalize();
}


void qd_dispatch_router_lock(qd_dispatch_t *qd) { sys_mutex_lock(qd->router->lock); }
void qd_dispatch_router_unlock(qd_dispatch_t *qd) { sys_mutex_unlock(qd->router->lock); }
