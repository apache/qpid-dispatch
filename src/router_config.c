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
#include <qpid/dispatch/config.h>
#include <qpid/dispatch/log.h>
#include "dispatch_private.h"
#include "router_private.h"

static const char *CONF_ADDRESS  = "fixed-address";
static const char *CONF_WAYPOINT = "waypoint";


static void qd_router_configure_addresses(qd_router_t *router)
{
    int count = qd_config_item_count(router->qd, CONF_ADDRESS);

    for (int idx = 0; idx < count; idx++) {
        char *prefix = qd_config_item_value_string(router->qd, CONF_ADDRESS, idx, "prefix");
        int   phase  = qd_config_item_value_int(router->qd,    CONF_ADDRESS, idx, "phase");
        char *fanout = qd_config_item_value_string(router->qd, CONF_ADDRESS, idx, "fanout");
        char *bias   = qd_config_item_value_string(router->qd, CONF_ADDRESS, idx, "bias");

        if (phase < 0 || phase > 9) {
            qd_log(router->log_source, QD_LOG_ERROR, "Invalid phase %d for prefix '%s' must be between 0 and 9.  Ignoring", phase, prefix);
            free(prefix);
            free(fanout);
            free(bias);
            continue;
        }

        //
        // Search for a matching prefix in the list.
        //
        qd_config_address_t *addr = DEQ_HEAD(router->config_addrs);
        while (addr) {
            if (strcmp(addr->prefix, prefix) == 0)
                break;
            addr = DEQ_NEXT(addr);
        }

        if (addr == 0) {
            //
            // Create a new prefix
            //
            addr = NEW(qd_config_address_t);
            DEQ_ITEM_INIT(addr);
            addr->prefix = (char*) malloc(strlen(prefix) + 1);
            addr->last_phase = (char) phase + '0';
            DEQ_INIT(addr->phases);
            DEQ_INSERT_TAIL(router->config_addrs, addr);
            if (prefix[0] == '/')
                strcpy(addr->prefix, &prefix[1]);
            else
                strcpy(addr->prefix, prefix);
        }

        //
        // Add the phase to the prefix
        //
        qd_config_phase_t *addr_phase = NEW(qd_config_phase_t);
        DEQ_ITEM_INIT(addr_phase);
        addr_phase->phase = (char) phase + '0';

        qd_address_semantics_t semantics = 0;
        if      (strcmp("multiple", fanout) == 0) semantics |= QD_FANOUT_MULTIPLE;
        else if (strcmp("single",   fanout) == 0) semantics |= QD_FANOUT_SINGLE;
        else
            assert(0);

        if ((semantics & QD_FANOUTMASK) == QD_FANOUT_SINGLE) {
            if      (strcmp("closest", bias) == 0) semantics |= QD_BIAS_CLOSEST;
            else if (strcmp("spread",  bias) == 0) semantics |= QD_BIAS_SPREAD;
            else
                assert(0);
            qd_log(router->log_source, QD_LOG_INFO, "Configured Address: prefix=%s phase=%d fanout=%s bias=%s",
                   prefix, phase, fanout, bias);
        } else {
            semantics |= QD_BIAS_NONE;
            qd_log(router->log_source, QD_LOG_INFO, "Configured Address: prefix=%s phase=%d fanout=%s",
                   prefix, phase, fanout);
        }

        addr_phase->semantics = semantics;
        addr->last_phase      = addr_phase->phase;
        DEQ_INSERT_TAIL(addr->phases, addr_phase);

        free(prefix);
        free(fanout);
        free(bias);
    }
}


static void qd_router_configure_waypoints(qd_router_t *router)
{
    int count = qd_config_item_count(router->qd, CONF_WAYPOINT);

    for (int idx = 0; idx < count; idx++) {
        char *address      = qd_config_item_value_string(router->qd, CONF_WAYPOINT, idx, "address");
        char *connector = qd_config_item_value_string(router->qd, CONF_WAYPOINT, idx, "connector");
        int   in_phase  = qd_config_item_value_int(router->qd,    CONF_WAYPOINT, idx, "in-phase");
        int   out_phase = qd_config_item_value_int(router->qd,    CONF_WAYPOINT, idx, "out-phase");

        if (in_phase > 9 || out_phase > 9) {
            qd_log(router->log_source, QD_LOG_ERROR, "Phases for waypoint '%s' must be between 0 and 9.  Ignoring", address);
            free(address);
            free(connector);
            continue;
        }

        qd_waypoint_t *waypoint = NEW(qd_waypoint_t);
        memset(waypoint, 0, sizeof(qd_waypoint_t));
        DEQ_ITEM_INIT(waypoint);
        waypoint->address        = address;
        waypoint->in_phase       = in_phase >= 0  ? (char) in_phase  + '0' : '\0';
        waypoint->out_phase      = out_phase >= 0 ? (char) out_phase + '0' : '\0';
        waypoint->connector_name = connector;

        //
        // TODO - Look up connector
        //

        DEQ_INSERT_TAIL(router->waypoints, waypoint);

        qd_log(router->log_source, QD_LOG_INFO,
	       "Configured Waypoint: address=%s in_phase=%d out_phase=%d connector=%s",
               address, in_phase, out_phase, connector);
    }
}


void qd_router_configure(qd_router_t *router)
{
    if (!router->qd)
        return;
    qd_router_configure_addresses(router);
    qd_router_configure_waypoints(router);
}


void qd_router_configure_free(qd_router_t *router)
{
    if (!router) return;

    for (qd_config_address_t *ca = DEQ_HEAD(router->config_addrs); ca; ca = DEQ_HEAD(router->config_addrs)) {
        for (qd_config_phase_t *ap = DEQ_HEAD(ca->phases); ap; ap = DEQ_HEAD(ca->phases)) {
            DEQ_REMOVE_HEAD(ca->phases);
            free(ap);
        }
        free(ca->prefix);
        DEQ_REMOVE_HEAD(router->config_addrs);
        free(ca);
    }

    for (qd_waypoint_t *wp = DEQ_HEAD(router->waypoints); wp; wp = DEQ_HEAD(router->waypoints)) {
        DEQ_REMOVE_HEAD(router->waypoints);
        free(wp->address);
        free(wp->connector_name);
        free(wp);
    }
}


qd_address_semantics_t router_semantics_for_addr(qd_router_t *router, qd_field_iterator_t *iter,
                                                 char in_phase, char *out_phase)
{
    qd_field_iterator_reset_view(iter, ITER_VIEW_NO_HOST);

    qd_config_address_t *addr  = DEQ_HEAD(router->config_addrs);
    qd_config_phase_t   *phase = 0;

    while (addr) {
        if (qd_field_iterator_prefix(iter, addr->prefix))
            break;
        qd_field_iterator_reset(iter);
        addr = DEQ_NEXT(addr);
    }

    if (addr) {
        *out_phase = in_phase == '\0' ? addr->last_phase : in_phase;
        phase = DEQ_HEAD(addr->phases);
        while (phase) {
            if (phase->phase == *out_phase)
                break;
            phase = DEQ_NEXT(phase);
        }
    }

    return phase ? phase->semantics : QD_SEMANTICS_DEFAULT;
}
