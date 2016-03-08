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
#include "lrp_private.h"
#include "entity.h"
#include "entity_cache.h"
#include "schema_enum.h"

qd_error_t qd_router_configure_address(qd_router_t *router, qd_entity_t *entity)
{
    qd_error_clear();
    int                             phase  = qd_entity_opt_long(entity, "phase", -1); QD_ERROR_RET();
    qd_schema_fixedAddress_fanout_t fanout = qd_entity_get_long(entity, "fanout");    QD_ERROR_RET();
    qd_schema_fixedAddress_bias_t   bias   = qd_entity_get_long(entity, "bias");      QD_ERROR_RET();
    char                           *prefix = qd_entity_get_string(entity, "prefix");  QD_ERROR_RET();

    if (phase != -1) {
        qd_log(router->log_source, QD_LOG_WARNING,
               "Address phases deprecated: Ignoring address configuration for '%s', phase %d", prefix, phase);
        free(prefix);
        return qd_error_code();
    }

    if (prefix[0] == '/' && prefix[1] == '\0') {
        qd_log(router->log_source, QD_LOG_WARNING, "Ignoring address configuration for '/'");
        free(prefix);
        return qd_error_code();
    }

    //
    // Convert fanout + bias to treatment
    //
    const char *trt;

    if (fanout == QD_SCHEMA_FIXEDADDRESS_FANOUT_MULTIPLE)
        trt = "multicast";
    else {
        if (bias == QD_SCHEMA_FIXEDADDRESS_BIAS_CLOSEST)
            trt = "closest";
        else
            trt = "balanced";
    }

    //
    // Formulate this configuration as a router.route and create it through the core management API.
    //
    qd_composed_field_t *body = qd_compose_subfield(0);
    qd_compose_start_map(body);
    qd_compose_insert_string(body, "address");
    qd_compose_insert_string(body, prefix);

    qd_compose_insert_string(body, "treatment");
    qd_compose_insert_string(body, trt);
    qd_compose_end_map(body);

    int              length = 0;
    qd_buffer_list_t buffers;

    qd_compose_take_buffers(body, &buffers);
    qd_compose_free(body);

    qd_buffer_t *buf = DEQ_HEAD(buffers);
    while (buf) {
        length += qd_buffer_size(buf);
        buf = DEQ_NEXT(buf);
    }

    qd_field_iterator_t *iter = qd_field_iterator_buffer(DEQ_HEAD(buffers), 0, length);
    qd_parsed_field_t   *in_body = qd_parse(iter);

    qdr_manage_create(router->router_core, 0, QD_ROUTER_ROUTE, 0, in_body, 0);

    free(prefix);
    return qd_error_code();
}

qd_error_t qd_router_configure_waypoint(qd_router_t *router, qd_entity_t *entity)
{
    /*
    char *address = qd_entity_get_string(entity, "address"); QD_ERROR_RET();
    char *connector = qd_entity_get_string(entity, "connector"); QD_ERROR_RET();
    int   in_phase  = qd_entity_opt_long(entity, "inPhase", 0); QD_ERROR_RET();
    int   out_phase = qd_entity_opt_long(entity, "outPhase", 0);  QD_ERROR_RET();

    if (in_phase > 9 || out_phase > 9) {
        qd_error_t err = qd_error(QD_ERROR_CONFIG,
                                  "Phases for waypoint '%s' must be between 0 and 9.", address);
        free(address);
        free(connector);
        return err;
    }
    qd_waypoint_t *waypoint = NEW(qd_waypoint_t);
    memset(waypoint, 0, sizeof(qd_waypoint_t));
    DEQ_ITEM_INIT(waypoint);
    waypoint->address        = address;
    waypoint->in_phase       = in_phase >= 0  ? (char) in_phase  + '0' : '\0';
    waypoint->out_phase      = out_phase >= 0 ? (char) out_phase + '0' : '\0';
    waypoint->connector_name = connector;

    DEQ_INSERT_TAIL(router->waypoints, waypoint);

    qd_log(router->log_source, QD_LOG_INFO,
           "Configured Waypoint: address=%s in_phase=%d out_phase=%d connector=%s",
           address, in_phase, out_phase, connector);
    */
    return qd_error_code();
}


qd_error_t qd_router_configure_lrp(qd_router_t *router, qd_entity_t *entity)
{
    char *prefix    = qd_entity_get_string(entity, "prefix");    QD_ERROR_RET();
    char *connector = qd_entity_get_string(entity, "connector"); QD_ERROR_RET();
    char *direction = qd_entity_get_string(entity, "dir");       QD_ERROR_RET();

    //
    // Formulate this configuration as a router.route and create it through the core management API.
    //
    qd_composed_field_t *body = qd_compose_subfield(0);
    qd_compose_start_map(body);
    qd_compose_insert_string(body, "address");
    qd_compose_insert_string(body, prefix);

    qd_compose_insert_string(body, "path");
    if      (strcmp("in", direction) == 0)
        qd_compose_insert_string(body, "sink");
    else if (strcmp("out", direction) == 0)
        qd_compose_insert_string(body, "source");
    else
        qd_compose_insert_string(body, "waypoint");

    qd_compose_insert_string(body, "treatment");
    qd_compose_insert_string(body, "linkBalanced");

    if (connector) {
        qd_compose_insert_string(body, "connectors");
        qd_compose_start_list(body);
        qd_compose_insert_string(body, connector);
        qd_compose_end_list(body);
    }

    qd_compose_end_map(body);

    int              length = 0;
    qd_buffer_list_t buffers;

    qd_compose_take_buffers(body, &buffers);
    qd_compose_free(body);

    qd_buffer_t *buf = DEQ_HEAD(buffers);
    while (buf) {
        length += qd_buffer_size(buf);
        buf = DEQ_NEXT(buf);
    }

    qd_field_iterator_t *iter    = qd_field_iterator_buffer(DEQ_HEAD(buffers), 0, length);
    qd_parsed_field_t   *in_body = qd_parse(iter);

    qdr_manage_create(router->router_core, 0, QD_ROUTER_ROUTE, 0, in_body, 0);

    free(prefix);
    free(connector);
    free(direction);
    return qd_error_code();
}


static void qd_router_insert_items(qd_composed_field_t *body, char *list)
{
    char *saveptr;
    char *token;
    char *delim = ", ";

    token = strtok_r(list, delim, &saveptr);
    while (token) {
        qd_compose_insert_string(body, token);
        token = strtok_r(0, delim, &saveptr);
    }
}


qd_error_t qd_router_configure_route(qd_router_t *router, qd_entity_t *entity)
{
    char *name          = qd_entity_opt_string(entity, "name", 0);          QD_ERROR_RET();
    char *address       = qd_entity_opt_string(entity, "address", 0);       QD_ERROR_RET();
    char *path          = qd_entity_opt_string(entity, "path", 0);          QD_ERROR_RET();
    char *treatment     = qd_entity_opt_string(entity, "treatment", 0);     QD_ERROR_RET();
    char *connectors    = qd_entity_opt_string(entity, "connectors", 0);    QD_ERROR_RET();
    char *containers    = qd_entity_opt_string(entity, "containers", 0);    QD_ERROR_RET();
    char *route_address = qd_entity_opt_string(entity, "route_address", 0); QD_ERROR_RET();

    //
    // Formulate this configuration as a route and create it through the core management API.
    //
    qd_composed_field_t *body = qd_compose_subfield(0);
    qd_compose_start_map(body);

    if (name) {
        qd_compose_insert_string(body, "name");
        qd_compose_insert_string(body, name);
    }

    if (address) {
        qd_compose_insert_string(body, "address");
        qd_compose_insert_string(body, address);
    }

    if (path) {
        qd_compose_insert_string(body, "path");
        qd_compose_insert_string(body, path);
    }

    if (treatment) {
        qd_compose_insert_string(body, "treatment");
        qd_compose_insert_string(body, treatment);
    }

    if (connectors) {
        qd_compose_insert_string(body, "connectors");
        qd_compose_start_list(body);
        qd_router_insert_items(body, connectors);
        qd_compose_end_list(body);
    }

    if (containers) {
        qd_compose_insert_string(body, "containers");
        qd_compose_start_list(body);
        qd_router_insert_items(body, containers);
        qd_compose_end_list(body);
    }

    if (route_address) {
        qd_compose_insert_string(body, "routeAddress");
        qd_compose_insert_string(body, route_address);
    }

    qd_compose_end_map(body);

    int              length = 0;
    qd_buffer_list_t buffers;

    qd_compose_take_buffers(body, &buffers);
    qd_compose_free(body);

    qd_buffer_t *buf = DEQ_HEAD(buffers);
    while (buf) {
        length += qd_buffer_size(buf);
        buf = DEQ_NEXT(buf);
    }

    qd_field_iterator_t *iter    = qd_field_iterator_buffer(DEQ_HEAD(buffers), 0, length);
    qd_parsed_field_t   *in_body = qd_parse(iter);

    qdr_manage_create(router->router_core, 0, QD_ROUTER_ROUTE, 0, in_body, 0);

    free(name);
    free(address);
    free(path);
    free(treatment);
    free(connectors);
    free(containers);
    free(route_address);

    return qd_error_code();
}


void qd_router_configure_free(qd_router_t *router)
{
    if (!router) return;

    //
    // All configuration to be freed is now in the router core.
    //
}

