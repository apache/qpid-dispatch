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

ALLOC_DEFINE(qdr_link_route_t);
ALLOC_DEFINE(qdr_auto_link_t);
ALLOC_DEFINE(qdr_conn_identifier_t);


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
    if (cid->open_connection == 0 && DEQ_IS_EMPTY(cid->link_route_refs) && DEQ_IS_EMPTY(cid->auto_link_refs)) {
        qd_hash_remove_by_handle(core->conn_id_hash, cid->hash_handle);
        free_qdr_conn_identifier_t(cid);
    }
}


static void qdr_route_log_CT(qdr_core_t *core, const char *text, const char *name, uint64_t id, qdr_connection_t *conn)
{
    const char *key = (const char*) qd_hash_key_by_handle(conn->conn_id->hash_handle);
    char  id_string[64];
    const char *log_name = name ? name : id_string;

    if (!name)
        snprintf(id_string, 64, "%ld", id);

    qd_log(core->log, QD_LOG_INFO, "%s '%s' on %s %s",
           log_name, text, key[0] == 'L' ? "connection" : "container", &key[1]);
}


static void qdr_link_route_activate_CT(qdr_core_t *core, qdr_link_route_t *lr, qdr_connection_t *conn)
{
    const char *key;

    qdr_route_log_CT(core, "Activated Link Route", lr->name, lr->identity, conn);

    //
    // Activate the address for link-routed destinations.  If this is the first
    // activation for this address, notify the router module of the added address.
    //
    if (lr->addr) {
        qdr_add_connection_ref(&lr->addr->conns, conn);
        if (DEQ_SIZE(lr->addr->conns) == 1) {
            key = (const char*) qd_hash_key_by_handle(lr->addr->hash_handle);
            if (key)
                qdr_post_mobile_added_CT(core, key);
        }
    }
}


static void qdr_link_route_deactivate_CT(qdr_core_t *core, qdr_link_route_t *lr, qdr_connection_t *conn)
{
    const char *key;

    qdr_route_log_CT(core, "Deactivated Link Route", lr->name, lr->identity, conn);

    //
    // Deactivate the address(es) for link-routed destinations.
    //
    if (lr->addr) {
        qdr_del_connection_ref(&lr->addr->conns, conn);
        if (DEQ_IS_EMPTY(lr->addr->conns)) {
            key = (const char*) qd_hash_key_by_handle(lr->addr->hash_handle);
            if (key)
                qdr_post_mobile_removed_CT(core, key);
        }
    }
}


void qdr_route_add_link_route_CT(qdr_core_t             *core,
                                 qd_field_iterator_t    *name,
                                 qd_parsed_field_t      *prefix_field,
                                 qd_parsed_field_t      *conn_id,
                                 bool                    is_container,
                                 qd_address_treatment_t  treatment,
                                 qd_direction_t          dir)
{
    qdr_link_route_t *lr = new_qdr_link_route_t();

    //
    // Set up the link_route structure
    //
    ZERO(lr);
    lr->identity  = qdr_identifier(core);
    lr->name      = name ? (char*) qd_field_iterator_copy(name) : 0;
    lr->dir       = dir;
    lr->treatment = treatment;

    //
    // Find or create an address for link-attach routing
    //
    qd_field_iterator_t *iter = qd_parse_raw(prefix_field);
    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    qd_address_iterator_override_prefix(iter, dir == QD_INCOMING ? 'C' : 'D');

    qd_hash_retrieve(core->addr_hash, iter, (void*) &lr->addr);
    if (!lr->addr) {
        lr->addr = qdr_address_CT(core, treatment);
        DEQ_INSERT_TAIL(core->addrs, lr->addr);
        qd_hash_insert(core->addr_hash, iter, lr->addr, &lr->addr->hash_handle);
    }

    //
    // Find or create a connection identifier structure for this link route
    //
    if (conn_id) {
        lr->conn_id = qdr_route_declare_id_CT(core, qd_parse_raw(conn_id), is_container);
        DEQ_INSERT_TAIL_N(REF, lr->conn_id->link_route_refs, lr);
        if (lr->conn_id->open_connection)
            qdr_link_route_activate_CT(core, lr, lr->conn_id->open_connection);
    }

    //
    // Add the link route to the core list
    //
    DEQ_INSERT_TAIL(core->link_routes, lr);
}


void qdr_route_del_link_route_CT(qdr_core_t *core, qdr_link_route_t *lr)
{
}


void qdr_route_add_auto_link_CT(qdr_core_t             *core,
                                qd_field_iterator_t    *name,
                                qd_parsed_field_t      *addr_field,
                                qd_direction_t          dir,
                                int                     phase,
                                qd_parsed_field_t      *conn_id,
                                bool                    is_container)
{
}


void qdr_route_del_auto_link_CT(qdr_core_t *core, qdr_auto_link_t *auto_link)
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
    // Activate all link-routes associated with this remote container.
    //
    qdr_link_route_t *lr = DEQ_HEAD(cid->link_route_refs);
    while (lr) {
        qdr_link_route_activate_CT(core, lr, conn);
        lr = DEQ_NEXT_N(REF, lr);
    }

    //
    // Activate all auto-links associated with this remote container.
    //
    qdr_auto_link_t *al = DEQ_HEAD(cid->auto_link_refs);
    while (al) {
        //qdr_link_route_activate_CT(core, lr, conn);
        al = DEQ_NEXT_N(REF, al);
    }
}


void qdr_route_connection_closed_CT(qdr_core_t *core, qdr_connection_t *conn)
{
    if (conn->role != QDR_ROLE_ROUTE_CONTAINER)
        return;

    qdr_conn_identifier_t *cid = conn->conn_id;
    if (cid) {
        //
        // Deactivate all link-routes associated with this remote container.
        //
        qdr_link_route_t *lr = DEQ_HEAD(cid->link_route_refs);
        while (lr) {
            qdr_link_route_deactivate_CT(core, lr, conn);
            lr = DEQ_NEXT_N(REF, lr);
        }

        //
        // Deactivate all auto-links associated with this remote container.
        //
        qdr_auto_link_t *al = DEQ_HEAD(cid->auto_link_refs);
        while (al) {
            //qdr_link_route_deactivate_CT(core, lr, conn);
            al = DEQ_NEXT_N(REF, al);
        }

        cid->open_connection = 0;
        conn->conn_id        = 0;

        qdr_route_check_id_for_deletion_CT(core, cid);
    }
}

