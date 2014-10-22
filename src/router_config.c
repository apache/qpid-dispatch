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
#include "entity_private.h"
#include "schema_enum.h"

qd_error_t qd_router_configure_address(qd_router_t *router, qd_entity_t *entity) {
    qd_error_clear();
    int   phase  = qd_entity_opt_long(entity, "phase", 0); QD_ERROR_RET();
    qd_schema_fixedAddress_fanout_t fanout = qd_entity_long(entity, "fanout"); QD_ERROR_RET();
    qd_schema_fixedAddress_bias_t bias   = qd_entity_long(entity, "bias"); QD_ERROR_RET();
    char *prefix = qd_entity_string(entity, "prefix"); QD_ERROR_RET();

    if (phase < 0 || phase > 9) {
        free(prefix);
	return qd_error(QD_ERROR_CONFIG, "Invalid phase %d for prefix '%s' must be between 0 and 9.  Ignoring", phase, prefix);
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
    switch(fanout) {
      case QD_SCHEMA_FIXEDADDRESS_FANOUT_MULTIPLE: semantics |= QD_FANOUT_MULTIPLE; break;
      case QD_SCHEMA_FIXEDADDRESS_FANOUT_SINGLE: semantics |= QD_FANOUT_SINGLE; break;
      default:
        free(prefix);
        return qd_error(QD_ERROR_CONFIG, "Invalid fanout value %d", fanout);
    }

    if ((semantics & QD_FANOUTMASK) == QD_FANOUT_SINGLE) {
        switch(bias) {
          case QD_SCHEMA_FIXEDADDRESS_BIAS_CLOSEST: semantics |= QD_BIAS_CLOSEST; break;
          case QD_SCHEMA_FIXEDADDRESS_BIAS_SPREAD: semantics |= QD_BIAS_SPREAD; break;
          default:
            free(prefix);
            return qd_error(QD_ERROR_CONFIG, "Invalid bias value %d", fanout);
        }
	qd_log(router->log_source, QD_LOG_INFO,
		   "Configured Address: prefix=%s phase=%d fanout=%s bias=%s",
		   prefix, phase,
		   qd_schema_fixedAddress_fanout_names[fanout],
		   qd_schema_fixedAddress_bias_names[bias]);
    } else {
	semantics |= QD_BIAS_NONE;
	qd_log(router->log_source, QD_LOG_INFO, "Configured Address: prefix=%s phase=%d fanout=%s",
	       prefix, phase, qd_schema_fixedAddress_fanout_names[fanout]);
    }

    addr_phase->semantics = semantics;
    addr->last_phase      = addr_phase->phase;
    DEQ_INSERT_TAIL(addr->phases, addr_phase);
    free(prefix);
    return qd_error_code();
}

qd_error_t qd_router_configure_waypoint(qd_router_t *router, qd_entity_t *entity)
{

    char *address = qd_entity_string(entity, "address"); QD_ERROR_RET();
    char *connector = qd_entity_string(entity, "connector"); QD_ERROR_RET();
    int   in_phase  = qd_entity_opt_long(entity, "inPhase", 0); QD_ERROR_RET();
    int   out_phase = qd_entity_opt_long(entity, "outPhase", 0);  QD_ERROR_RET();

    if (in_phase > 9 || out_phase > 9) {
        free(address);
        free(connector);
	return qd_error(QD_ERROR_CONFIG, "Phases for waypoint '%s' must be between 0 and 9.", address);
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
    return qd_error_code();
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
