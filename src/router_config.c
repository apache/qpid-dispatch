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
#include <qpid/dispatch/log.h>
#include "dispatch_private.h"
#include "router_private.h"
#include "entity.h"
#include "entity_cache.h"
#include "schema_enum.h"

static void qdi_router_configure_body(qdr_core_t              *core,
                                      qd_composed_field_t     *body,
                                      qd_router_entity_type_t  type,
                                      char                    *name)
{
    qd_buffer_list_t buffers;
    qd_compose_take_buffers(body, &buffers);

    qd_iterator_t *iter = qd_iterator_buffer(DEQ_HEAD(buffers), 0, qd_buffer_list_length(&buffers), ITER_VIEW_ALL);
    qd_parsed_field_t   *in_body = qd_parse(iter);
    qd_iterator_free(iter);

    qd_iterator_t *name_iter = 0;
    if (name)
        name_iter = qd_iterator_string(name, ITER_VIEW_ALL);

    qdr_manage_create(core, 0, type, name_iter, in_body, 0, buffers);

    qd_iterator_free(name_iter);
}


qd_error_t qd_router_configure_address(qd_router_t *router, qd_entity_t *entity)
{
    char *name    = 0;
    char *pattern = 0;
    char *distrib = 0;
    char *prefix  = 0;

    do {
        name = qd_entity_opt_string(entity, "name", 0);             QD_ERROR_BREAK();
        distrib = qd_entity_opt_string(entity, "distribution", 0);  QD_ERROR_BREAK();

        pattern = qd_entity_opt_string(entity, "pattern", 0);
        prefix = qd_entity_opt_string(entity, "prefix", 0);

        if (prefix && pattern) {
            qd_log(router->log_source, QD_LOG_WARNING,
                   "Cannot set both 'prefix' and 'pattern': ignoring"
                   " configured address %s, %s",
                   prefix, pattern);
            break;
        } else if (!prefix && !pattern) {
            qd_log(router->log_source, QD_LOG_WARNING,
                   "Must set either 'prefix' or 'pattern' attribute:"
                   " ignoring configured address");
            break;
        }

        bool  waypoint  = qd_entity_opt_bool(entity, "waypoint", false);
        long  in_phase  = qd_entity_opt_long(entity, "ingressPhase", -1);
        long  out_phase = qd_entity_opt_long(entity, "egressPhase", -1);

        //
        // Formulate this configuration create it through the core management API.
        //
        qd_composed_field_t *body = qd_compose_subfield(0);
        qd_compose_start_map(body);

        if (name) {
            qd_compose_insert_string(body, "name");
            qd_compose_insert_string(body, name);
        }

        if (prefix) {
            qd_compose_insert_string(body, "prefix");
            qd_compose_insert_string(body, prefix);
        }

        if (pattern) {
            qd_compose_insert_string(body, "pattern");
            qd_compose_insert_string(body, pattern);
        }

        if (distrib) {
            qd_compose_insert_string(body, "distribution");
            qd_compose_insert_string(body, distrib);
        }

        qd_compose_insert_string(body, "waypoint");
        qd_compose_insert_bool(body, waypoint);

        if (in_phase >= 0) {
            qd_compose_insert_string(body, "ingressPhase");
            qd_compose_insert_int(body, in_phase);
        }

        if (out_phase >= 0) {
            qd_compose_insert_string(body, "egressPhase");
            qd_compose_insert_int(body, out_phase);
        }

        qd_compose_end_map(body);

        qdi_router_configure_body(router->router_core, body, QD_ROUTER_CONFIG_ADDRESS, name);
        qd_compose_free(body);
    } while(0);

    free(name);
    free(prefix);
    free(distrib);
    free(pattern);

    return qd_error_code();
}


qd_error_t qd_router_configure_link_route(qd_router_t *router, qd_entity_t *entity)
{

    char *name      = 0;
    char *prefix    = 0;
    char *pattern   = 0;
    char *container = 0;
    char *c_name    = 0;
    char *distrib   = 0;
    char *dir       = 0;

    do {
        name      = qd_entity_opt_string(entity, "name", 0);         QD_ERROR_BREAK();
        container = qd_entity_opt_string(entity, "containerId", 0);  QD_ERROR_BREAK();
        c_name    = qd_entity_opt_string(entity, "connection", 0);   QD_ERROR_BREAK();
        distrib   = qd_entity_opt_string(entity, "distribution", 0); QD_ERROR_BREAK();
        dir       = qd_entity_opt_string(entity, "dir", 0);          QD_ERROR_BREAK();

        prefix    = qd_entity_opt_string(entity, "prefix", 0);
        pattern   = qd_entity_opt_string(entity, "pattern", 0);

        if (prefix && pattern) {
            qd_log(router->log_source, QD_LOG_WARNING,
                   "Cannot set both 'prefix' and 'pattern': ignoring link route %s, %s",
                   prefix, pattern);
            break;
        } else if (!prefix && !pattern) {
            qd_log(router->log_source, QD_LOG_WARNING,
                   "Must set either 'prefix' or 'pattern' attribute:"
                   " ignoring link route address");
            break;
        }

        //
        // Formulate this configuration as a route and create it through the core management API.
        //
        qd_composed_field_t *body = qd_compose_subfield(0);
        qd_compose_start_map(body);

        if (name) {
            qd_compose_insert_string(body, "name");
            qd_compose_insert_string(body, name);
        }

        if (prefix) {
            qd_compose_insert_string(body, "prefix");
            qd_compose_insert_string(body, prefix);
        }

        if (pattern) {
            qd_compose_insert_string(body, "pattern");
            qd_compose_insert_string(body, pattern);
        }

        if (container) {
            qd_compose_insert_string(body, "containerId");
            qd_compose_insert_string(body, container);
        }

        if (c_name) {
            qd_compose_insert_string(body, "connection");
            qd_compose_insert_string(body, c_name);
        }

        if (distrib) {
            qd_compose_insert_string(body, "distribution");
            qd_compose_insert_string(body, distrib);
        }

        if (dir) {
            qd_compose_insert_string(body, "dir");
            qd_compose_insert_string(body, dir);
        }

        qd_compose_end_map(body);

        qdi_router_configure_body(router->router_core, body, QD_ROUTER_CONFIG_LINK_ROUTE, name);
        qd_compose_free(body);
    } while(0);

    free(name);
    free(prefix);
    free(container);
    free(c_name);
    free(distrib);
    free(dir);
    free(pattern);

    return qd_error_code();
}


qd_error_t qd_router_configure_auto_link(qd_router_t *router, qd_entity_t *entity)
{
    char *name      = 0;
    char *addr      = 0;
    char *dir       = 0;
    char *container = 0;
    char *c_name    = 0;
    char *ext_addr  = 0;

    do {
        name      = qd_entity_opt_string(entity, "name", 0);         QD_ERROR_BREAK();
        addr      = qd_entity_get_string(entity, "addr");            QD_ERROR_BREAK();
        dir       = qd_entity_get_string(entity, "dir");             QD_ERROR_BREAK();
        container = qd_entity_opt_string(entity, "containerId", 0);  QD_ERROR_BREAK();
        c_name    = qd_entity_opt_string(entity, "connection", 0);   QD_ERROR_BREAK();
        ext_addr  = qd_entity_opt_string(entity, "externalAddr", 0); QD_ERROR_BREAK();
        long  phase     = qd_entity_opt_long(entity, "phase", -1);   QD_ERROR_BREAK();

        //
        // Formulate this configuration as a route and create it through the core management API.
        //
        qd_composed_field_t *body = qd_compose_subfield(0);
        qd_compose_start_map(body);

        if (name) {
            qd_compose_insert_string(body, "name");
            qd_compose_insert_string(body, name);
        }

        if (addr) {
            qd_compose_insert_string(body, "addr");
            qd_compose_insert_string(body, addr);
        }

        if (dir) {
            qd_compose_insert_string(body, "dir");
            qd_compose_insert_string(body, dir);
        }

        if (phase >= 0) {
            qd_compose_insert_string(body, "phase");
            qd_compose_insert_int(body, phase);
        }

        if (container) {
            qd_compose_insert_string(body, "containerId");
            qd_compose_insert_string(body, container);
        }

        if (c_name) {
            qd_compose_insert_string(body, "connection");
            qd_compose_insert_string(body, c_name);
        }

        if (ext_addr) {
            qd_compose_insert_string(body, "externalAddr");
            qd_compose_insert_string(body, ext_addr);
        }

        qd_compose_end_map(body);

        qdi_router_configure_body(router->router_core, body, QD_ROUTER_CONFIG_AUTO_LINK, name);
        qd_compose_free(body);
    } while (0);

    free(name);
    free(addr);
    free(dir);
    free(container);
    free(c_name);
    free(ext_addr);

    return qd_error_code();
}


void qd_router_configure_free(qd_router_t *router)
{
    if (!router) return;

    //
    // All configuration to be freed is now in the router core.
    //
}

