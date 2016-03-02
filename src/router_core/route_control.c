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

#include "route_control.h"

ALLOC_DEFINE(qdr_route_active_t);
ALLOC_DEFINE(qdr_route_config_t);
ALLOC_DEFINE(qdr_conn_identifier_t);



static const char *qdr_configure_address_prefix_CT(qdr_core_t              *core,
                                                   qd_parsed_field_t       *addr_field,
                                                   char                     cls,
                                                   qd_address_treatment_t   treatment,
                                                   qdr_address_config_t   **_addr)
{
    if (!addr_field)
        return "Missing address field";

    qd_field_iterator_t *iter = qd_parse_raw(addr_field);
    qd_address_iterator_override_prefix(iter, cls);
    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);

    qdr_address_config_t *addr = 0;
    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    if (addr)
        return "Address prefix conflicts with existing prefix";

    addr = new_qdr_address_config_t();
    DEQ_ITEM_INIT(addr);
    addr->treatment = treatment;

    if (!!addr) {
        qd_field_iterator_reset(iter);
        qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_INSERT_TAIL(core->addr_config, addr);
    }

    *_addr = addr;
    return 0;
}

/*
static const char *qdr_configure_address_CT(qdr_core_t              *core,
                                            qd_parsed_field_t       *addr_field,
                                            char                     cls,
                                            qd_address_treatment_t   treatment,
                                            qdr_address_t          **_addr)
{
    if (!addr_field)
        return "Missing address field";

    qd_field_iterator_t *iter = qd_parse_raw(addr_field);
    qd_address_iterator_override_prefix(iter, cls);
    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);

    qdr_address_t *addr = 0;
    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    if (addr)
        return "Address conflicts with existing address";

    addr = qdr_address_CT(core, treatment);

    if (!!addr) {
        qd_field_iterator_reset(iter);
        qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_INSERT_TAIL(core->addrs, addr);
    }

    *_addr = addr;
    return 0;
}
*/

const char *qdr_route_create_CT(qdr_core_t             *core,
                                qd_field_iterator_t    *name,
                                qdr_route_path_t        path,
                                qd_address_treatment_t  treatment,
                                qd_parsed_field_t      *addr_field,
                                qd_parsed_field_t      *route_addr_field,
                                qdr_route_config_t    **_route)
{
    const char *error = 0;

    qdr_route_config_t *route = new_qdr_route_config_t();
    ZERO(route);

    if (name)
        route->name = (char*) qd_field_iterator_copy(name);
    route->identity  = qdr_identifier(core);
    route->path      = path;
    route->treatment = treatment;

    switch (path) {
    case QDR_ROUTE_PATH_DIRECT:
        error = qdr_configure_address_prefix_CT(core, addr_field, 'Z', treatment, &route->addr_config);
        break;

    case QDR_ROUTE_PATH_SOURCE:
    case QDR_ROUTE_PATH_SINK:
    case QDR_ROUTE_PATH_WAYPOINT:
        break;
    }

    if (error) {
        if (route->name) free(route->name);
        free_qdr_route_config_t(route);
    } else {
        DEQ_INSERT_TAIL(core->route_config, route);
        *_route = route;
    }

    return error;
}


void qdr_route_delete_CT(qdr_route_config_t *route)
{
}


void qdr_route_connection_add_CT(qdr_route_config_t *route,
                                 qd_parsed_field_t  *conn_id,
                                 bool                is_container)
{
}


void qdr_route_connection_delete_CT(qdr_route_config_t *route,
                                    qd_parsed_field_t  *conn_id,
                                    bool                is_container)
{
}


void qdr_route_connection_kill_CT(qdr_route_config_t *route,
                                  qd_parsed_field_t  *conn_id,
                                  bool                is_container)
{
}


void qdr_route_connection_opened_CT(qdr_core_t *core, qdr_connection_t *conn)
{
}


void qdr_route_connection_closed_CT(qdr_core_t *core, qdr_connection_t *conn)
{
}

