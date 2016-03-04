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
#include <stdio.h>

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


static void qdr_route_free_CT(qdr_core_t *core, qdr_route_config_t *route)
{
    if (route->name)
        free(route->name);
    // TODO - Clean up address records
    free_qdr_route_config_t(route);
}


static qdr_conn_identifier_t *qdr_route_declare_id_CT(qdr_core_t          *core,
                                                      qd_field_iterator_t *conn_id,
                                                      bool                 is_container)
{
    char                   prefix = is_container ? 'C' : 'L';
    qdr_conn_identifier_t *cid    = 0;

    qd_address_iterator_reset_view(conn_id, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(conn_id, prefix);

    qd_hash_retrieve(core->conn_id_hash, conn_id, (void**) &cid);
    if (!cid) {
        cid = new_qdr_conn_identifier_t();
        ZERO(cid);
        qd_hash_insert(core->conn_id_hash, conn_id, cid, &cid->hash_handle);
    }

    return cid;
}

static void qdr_route_check_id_for_deletion_CT(qdr_core_t *core, qdr_conn_identifier_t *cid)
{
    //
    // If this connection identifier has no open connection and no referencing routes,
    // it can safely be deleted and removed from the hash index.
    //
    if (cid->open_connection == 0 && DEQ_IS_EMPTY(cid->active_refs)) {
        qd_hash_remove_by_handle(core->conn_id_hash, cid->hash_handle);
        free_qdr_conn_identifier_t(cid);
    }
}


static void qdr_route_log_CT(qdr_core_t *core, const char *text, qdr_route_config_t *route, qdr_connection_t *conn)
{
    const char *key = (const char*) qd_hash_key_by_handle(conn->conn_id->hash_handle);
    char  id_string[64];
    const char *name = route->name ? route->name : id_string;

    if (!route->name)
        snprintf(id_string, 64, "%ld", route->identity);

    qd_log(core->log, QD_LOG_INFO, "Route '%s' %s on %s %s",
           name, text, key[0] == 'L' ? "connection" : "container", &key[1]);
}


static void qdr_route_activate_CT(qdr_core_t *core, qdr_route_active_t *active, qdr_connection_t *conn)
{
    qdr_route_config_t *route = active->config;
    const char         *key;

    qdr_route_log_CT(core, "Activated", route, conn);

    if (route->treatment == QD_TREATMENT_LINK_BALANCED) {
        //
        // Activate the address(es) for link-routed destinations.  If this is the first
        // activation for this address, notify the router module of the added address.
        //
        if (route->out_addr) {
            qdr_add_connection_ref(&route->out_addr->conns, conn);
            if (DEQ_SIZE(route->out_addr->conns) == 1) {
                key = (const char*) qd_hash_key_by_handle(route->out_addr->hash_handle);
                if (key)
                    qdr_post_mobile_added_CT(core, key);
            }
        }

        if (route->in_addr) {
            qdr_add_connection_ref(&route->in_addr->conns, conn);
            if (DEQ_SIZE(route->in_addr->conns) == 1) {
                key = (const char*) qd_hash_key_by_handle(route->in_addr->hash_handle);
                if (key)
                    qdr_post_mobile_added_CT(core, key);
            }
        }
    }
}


static void qdr_route_deactivate_CT(qdr_core_t *core, qdr_route_active_t *active, qdr_connection_t *conn)
{
    qdr_route_config_t *route = active->config;
    const char         *key;

    qdr_route_log_CT(core, "Deactivated", route, conn);

    if (route->treatment == QD_TREATMENT_LINK_BALANCED) {
        //
        // Deactivate the address(es) for link-routed destinations.
        //
        if (route->out_addr) {
            qdr_del_connection_ref(&route->out_addr->conns, conn);
            if (DEQ_IS_EMPTY(route->out_addr->conns)) {
                key = (const char*) qd_hash_key_by_handle(route->out_addr->hash_handle);
                if (key)
                    qdr_post_mobile_removed_CT(core, key);
            }
        }

        if (route->in_addr) {
            qdr_del_connection_ref(&route->in_addr->conns, conn);
            if (DEQ_IS_EMPTY(route->in_addr->conns)) {
                key = (const char*) qd_hash_key_by_handle(route->in_addr->hash_handle);
                if (key)
                    qdr_post_mobile_removed_CT(core, key);
            }
        }
    }
}


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

    //
    // Direct message routing - Create a address prefix with the provided treatment.
    //
    if (path == QDR_ROUTE_PATH_DIRECT)
        error = qdr_configure_address_prefix_CT(core, addr_field, 'Z', treatment, &route->addr_config);

    //
    // Link routing - Create inbound and outbound link-route addresses based on the path.
    //
    else if (treatment == QD_TREATMENT_LINK_BALANCED) {
        if (path == QDR_ROUTE_PATH_SOURCE || path == QDR_ROUTE_PATH_WAYPOINT)
            error = qdr_configure_address_CT(core, addr_field, 'D', treatment, &route->out_addr);
        if (path == QDR_ROUTE_PATH_SINK   || path == QDR_ROUTE_PATH_WAYPOINT)
            error = qdr_configure_address_CT(core, addr_field, 'C', treatment, &route->in_addr);
    }

    //
    // Indirect message routing cases - Create a normal address with the provided treatment.
    //
    else {
        error = qdr_configure_address_CT(core, addr_field, '\0', treatment, &route->addr);
    }

    if (error)
        qdr_route_free_CT(core, route);
    else {
        DEQ_INSERT_TAIL(core->route_config, route);
        *_route = route;
    }

    return error;
}


void qdr_route_delete_CT(qdr_core_t *core, qdr_route_config_t *route)
{
}


void qdr_route_connection_add_CT(qdr_core_t         *core,
                                 qdr_route_config_t *route,
                                 qd_parsed_field_t  *conn_id,
                                 bool                is_container)
{
    //
    // Create a new active record for this route+connection and get a connection identifier
    // record (find and existing one or create a new one).
    //
    qdr_route_active_t    *active = new_qdr_route_active_t();
    qdr_conn_identifier_t *cid    = qdr_route_declare_id_CT(core, qd_parse_raw(conn_id), is_container);

    //
    // Initialize the active record in the DOWN state.
    //
    DEQ_ITEM_INIT(active);
    DEQ_ITEM_INIT_N(REF, active);
    active->in_state  = QDR_ROUTE_STATE_DOWN;
    active->out_state = QDR_ROUTE_STATE_DOWN;
    active->in_link   = 0;
    active->out_link  = 0;

    //
    // Create the linkages between the route-config, active, and connection-identifier.
    //
    active->config  = route;
    active->conn_id = cid;

    DEQ_INSERT_TAIL(route->active_list, active);
    DEQ_INSERT_TAIL_N(REF, cid->active_refs, active);

    //
    // If the connection identifier represents an already open connection, activate the route.
    //
    if (cid->open_connection)
        qdr_route_activate_CT(core, active, cid->open_connection);
}


void qdr_route_connection_delete_CT(qdr_core_t         *core,
                                    qdr_route_config_t *route,
                                    qd_parsed_field_t  *conn_id,
                                    bool                is_container)
{
}


void qdr_route_connection_kill_CT(qdr_core_t         *core,
                                  qdr_route_config_t *route,
                                  qd_parsed_field_t  *conn_id,
                                  bool                is_container)
{
}


void qdr_route_connection_opened_CT(qdr_core_t       *core,
                                    qdr_connection_t *conn,
                                    qdr_field_t      *field,
                                    bool              is_container)
{
    if (conn->role != QDR_ROLE_ROUTE_CONTAINER || !field)
        return;

    qdr_conn_identifier_t *cid = qdr_route_declare_id_CT(core, field->iterator, is_container);

    assert(!cid->open_connection);
    cid->open_connection = conn;
    conn->conn_id        = cid;

    //
    // Activate all routes associated with this remote container.
    //
    qdr_route_active_t *active = DEQ_HEAD(cid->active_refs);
    while (active) {
        qdr_route_activate_CT(core, active, conn);
        active = DEQ_NEXT_N(REF, active);
    }
}


void qdr_route_connection_closed_CT(qdr_core_t *core, qdr_connection_t *conn)
{
    if (conn->role != QDR_ROLE_ROUTE_CONTAINER)
        return;

    qdr_conn_identifier_t *cid = conn->conn_id;
    if (cid) {
        //
        // De-activate all routes associated with this remote container.
        //
        qdr_route_active_t *active = DEQ_HEAD(cid->active_refs);
        while (active) {
            qdr_route_deactivate_CT(core, active, conn);
            active = DEQ_NEXT_N(REF, active);
        }

        cid->open_connection = 0;
        conn->conn_id        = 0;

        qdr_route_check_id_for_deletion_CT(core, cid);
    }
}

