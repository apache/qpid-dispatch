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
#include <inttypes.h>
#include <stdio.h>
#include <qpid/dispatch/iterator.h>

ALLOC_DEFINE(qdr_link_route_t);
ALLOC_DEFINE(qdr_auto_link_t);
ALLOC_DEFINE(qdr_conn_identifier_t);

const char CONTAINER_PREFIX = 'C';
const char CONNECTION_PREFIX = 'L';


static qdr_conn_identifier_t *qdr_route_declare_id_CT(qdr_core_t    *core,
                                                      qd_iterator_t *container,
                                                      qd_iterator_t *connection)
{
    qdr_conn_identifier_t *cid    = 0;

    if (container && connection) {
        qd_iterator_reset_view(container, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_prefix(container, CONTAINER_PREFIX);
        qd_hash_retrieve(core->conn_id_hash, container, (void**) &cid);

        if (!cid) {
            qd_iterator_reset_view(connection, ITER_VIEW_ADDRESS_HASH);
            qd_iterator_annotate_prefix(connection, CONNECTION_PREFIX);
            qd_hash_retrieve(core->conn_id_hash, connection, (void**) &cid);
        }

        if (!cid) {
            cid = new_qdr_conn_identifier_t();
            ZERO(cid);
            //
            // The container and the connection will represent the same connection.
            //
            qd_hash_insert(core->conn_id_hash, container, cid, &cid->container_hash_handle);
            qd_hash_insert(core->conn_id_hash, connection, cid, &cid->connection_hash_handle);
        }
    }
    else if (container) {
        qd_iterator_reset_view(container, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_prefix(container, CONTAINER_PREFIX);
        qd_hash_retrieve(core->conn_id_hash, container, (void**) &cid);
        if (!cid) {
            cid = new_qdr_conn_identifier_t();
            ZERO(cid);
            qd_hash_insert(core->conn_id_hash, container, cid, &cid->container_hash_handle);
        }
    }
    else if (connection) {
        qd_iterator_reset_view(connection, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_prefix(connection, CONNECTION_PREFIX);
        qd_hash_retrieve(core->conn_id_hash, connection, (void**) &cid);
        if (!cid) {
            cid = new_qdr_conn_identifier_t();
            ZERO(cid);
            qd_hash_insert(core->conn_id_hash, connection, cid, &cid->connection_hash_handle);
        }

    }

    return cid;
}


static void qdr_route_check_id_for_deletion_CT(qdr_core_t *core, qdr_conn_identifier_t *cid)
{
    //
    // If this connection identifier has no open connection and no referencing routes,
    // it can safely be deleted and removed from the hash index.
    //
    if (DEQ_IS_EMPTY(cid->connection_refs) && DEQ_IS_EMPTY(cid->link_route_refs) && DEQ_IS_EMPTY(cid->auto_link_refs)) {
        qd_hash_remove_by_handle(core->conn_id_hash, cid->connection_hash_handle);
        qd_hash_remove_by_handle(core->conn_id_hash, cid->container_hash_handle);
        free_qdr_conn_identifier_t(cid);
    }
}


static void qdr_route_log_CT(qdr_core_t *core, const char *text, const char *name, uint64_t id, qdr_connection_t *conn)
{
    const char *key = (const char*) qd_hash_key_by_handle(conn->conn_id->connection_hash_handle);
    if (!key)
        key = (const char*) qd_hash_key_by_handle(conn->conn_id->container_hash_handle);
    char  id_string[64];
    const char *log_name = name ? name : id_string;

    if (!name)
        snprintf(id_string, 64, "%"PRId64, id);

    qd_log(core->log, QD_LOG_INFO, "%s '%s' on %s %s",
           text, log_name, key[0] == 'L' ? "connection" : "container", &key[1]);
}


static void qdr_link_route_activate_CT(qdr_core_t *core, qdr_link_route_t *lr, qdr_connection_t *conn)
{
    const char *key;

    qdr_route_log_CT(core, "Link Route Activated", lr->name, lr->identity, conn);

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

    lr->active = true;
}


static void qdr_link_route_deactivate_CT(qdr_core_t *core, qdr_link_route_t *lr, qdr_connection_t *conn)
{
    const char *key;

    qdr_route_log_CT(core, "Link Route Deactivated", lr->name, lr->identity, conn);

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

    lr->active = false;
}


static void qdr_auto_link_activate_CT(qdr_core_t *core, qdr_auto_link_t *al, qdr_connection_t *conn)
{
    const char *key;

    qdr_route_log_CT(core, "Auto Link Activated", al->name, al->identity, conn);

    if (al->addr) {
        qdr_terminus_t *source = 0;
        qdr_terminus_t *target = 0;
        qdr_terminus_t *term   = qdr_terminus(0);

        if (al->dir == QD_INCOMING)
            source = term;
        else
            target = term;

        key = (const char*) qd_hash_key_by_handle(al->addr->hash_handle);
        if (key || al->external_addr) {
            if (al->external_addr)
                qdr_terminus_set_address(term, al->external_addr);
            else
                qdr_terminus_set_address(term, &key[2]); // truncate the "Mp" annotation (where p = phase)
            al->link = qdr_create_link_CT(core, conn, QD_LINK_ENDPOINT, al->dir, source, target);
            al->link->auto_link = al;
            al->state = QDR_AUTO_LINK_STATE_ATTACHING;
        }
    }
}


static void qdr_auto_link_deactivate_CT(qdr_core_t *core, qdr_auto_link_t *al, qdr_connection_t *conn)
{
    qdr_route_log_CT(core, "Auto Link Deactivated", al->name, al->identity, conn);

    if (al->link) {
        qdr_link_outbound_detach_CT(core, al->link, 0, QDR_CONDITION_ROUTED_LINK_LOST, true);
        al->link->auto_link = 0;
        al->link            = 0;
    }

    al->state = QDR_AUTO_LINK_STATE_INACTIVE;
}


qdr_link_route_t *qdr_route_add_link_route_CT(qdr_core_t             *core,
                                              qd_iterator_t          *name,
                                              qd_parsed_field_t      *prefix_field,
                                              qd_parsed_field_t      *container_field,
                                              qd_parsed_field_t      *connection_field,
                                              qd_address_treatment_t  treatment,
                                              qd_direction_t          dir)
{
    qdr_link_route_t *lr = new_qdr_link_route_t();

    //
    // Set up the link_route structure
    //
    ZERO(lr);
    lr->identity  = qdr_identifier(core);
    lr->name      = name ? (char*) qd_iterator_copy(name) : 0;
    lr->dir       = dir;
    lr->treatment = treatment;

    //
    // Find or create an address for link-attach routing
    //
    qd_iterator_t *iter = qd_parse_raw(prefix_field);
    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_prefix(iter, dir == QD_INCOMING ? 'C' : 'D');

    qd_hash_retrieve(core->addr_hash, iter, (void*) &lr->addr);
    if (!lr->addr) {
        lr->addr = qdr_address_CT(core, treatment);
        DEQ_INSERT_TAIL(core->addrs, lr->addr);
        qd_hash_insert(core->addr_hash, iter, lr->addr, &lr->addr->hash_handle);
    }

    lr->addr->ref_count++;

    //
    // Find or create a connection identifier structure for this link route
    //
    if (container_field || connection_field) {
        lr->conn_id = qdr_route_declare_id_CT(core, qd_parse_raw(container_field), qd_parse_raw(connection_field));
        DEQ_INSERT_TAIL_N(REF, lr->conn_id->link_route_refs, lr);
        qdr_connection_ref_t * cref = DEQ_HEAD(lr->conn_id->connection_refs);
        while (cref) {
            qdr_link_route_activate_CT(core, lr, cref->conn);
            cref = DEQ_NEXT(cref);
        }
    }

    //
    // Add the link route to the core list
    //
    DEQ_INSERT_TAIL(core->link_routes, lr);

    return lr;
}


void qdr_route_del_link_route_CT(qdr_core_t *core, qdr_link_route_t *lr)
{
    //
    // Disassociate from the connection identifier.  Check to see if the identifier
    // should be removed.
    //
    qdr_conn_identifier_t *cid = lr->conn_id;
    if (cid) {
        qdr_connection_ref_t * cref = DEQ_HEAD(cid->connection_refs);
        while (cref) {
            qdr_link_route_deactivate_CT(core, lr, cref->conn);
            cref = DEQ_NEXT(cref);
        }
        DEQ_REMOVE_N(REF, cid->link_route_refs, lr);
        qdr_route_check_id_for_deletion_CT(core, cid);
    }

    //
    // Disassociate the link route from its address.  Check to see if the address
    // should be removed.
    //
    qdr_address_t *addr = lr->addr;
    if (addr && --addr->ref_count == 0)
        qdr_check_addr_CT(core, addr, false);

    //
    // Remove the link route from the core list.
    //
    DEQ_REMOVE(core->link_routes, lr);
    free(lr->name);
    free_qdr_link_route_t(lr);
}


qdr_auto_link_t *qdr_route_add_auto_link_CT(qdr_core_t          *core,
                                            qd_iterator_t       *name,
                                            qd_parsed_field_t   *addr_field,
                                            qd_direction_t       dir,
                                            int                  phase,
                                            qd_parsed_field_t   *container_field,
                                            qd_parsed_field_t   *connection_field,
                                            qd_parsed_field_t   *external_addr)
{
    qdr_auto_link_t *al = new_qdr_auto_link_t();

    //
    // Set up the auto_link structure
    //
    ZERO(al);
    al->identity      = qdr_identifier(core);
    al->name          = name ? (char*) qd_iterator_copy(name) : 0;
    al->dir           = dir;
    al->phase         = phase;
    al->state         = QDR_AUTO_LINK_STATE_INACTIVE;
    al->external_addr = external_addr ? (char*) qd_iterator_copy(qd_parse_raw(external_addr)) : 0;

    //
    // Find or create an address for the auto_link destination
    //
    qd_iterator_t *iter = qd_parse_raw(addr_field);
    qd_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_phase(iter, (char) phase + '0');

    qd_hash_retrieve(core->addr_hash, iter, (void*) &al->addr);
    if (!al->addr) {
        al->addr = qdr_address_CT(core, qdr_treatment_for_address_CT(core, 0, iter, 0, 0));
        DEQ_INSERT_TAIL(core->addrs, al->addr);
        qd_hash_insert(core->addr_hash, iter, al->addr, &al->addr->hash_handle);
    }

    al->addr->ref_count++;

    //
    // Find or create a connection identifier structure for this auto_link
    //
    if (container_field || connection_field) {
        al->conn_id = qdr_route_declare_id_CT(core, qd_parse_raw(container_field), qd_parse_raw(connection_field));
        DEQ_INSERT_TAIL_N(REF, al->conn_id->auto_link_refs, al);
        qdr_connection_ref_t * cref = DEQ_HEAD(al->conn_id->connection_refs);
        while (cref) {
            qdr_auto_link_activate_CT(core, al, cref->conn);
            cref = DEQ_NEXT(cref);
        }
    }

    //
    // Add the auto_link to the core list
    //
    DEQ_INSERT_TAIL(core->auto_links, al);

    return al;
}


void qdr_route_del_auto_link_CT(qdr_core_t *core, qdr_auto_link_t *al)
{
    //
    // Disassociate from the connection identifier.  Check to see if the identifier
    // should be removed.
    //
    qdr_conn_identifier_t *cid = al->conn_id;
    if (cid) {
        qdr_connection_ref_t * cref = DEQ_HEAD(cid->connection_refs);
        while (cref) {
            qdr_auto_link_deactivate_CT(core, al, cref->conn);
            cref = DEQ_NEXT(cref);
        }
        DEQ_REMOVE_N(REF, cid->auto_link_refs, al);
        qdr_route_check_id_for_deletion_CT(core, cid);
    }

    //
    // Disassociate the auto link from its address.  Check to see if the address
    // should be removed.
    //
    qdr_address_t *addr = al->addr;
    if (addr && --addr->ref_count == 0)
        qdr_check_addr_CT(core, addr, false);

    //
    // Remove the auto link from the core list.
    //
    DEQ_REMOVE(core->auto_links, al);
    free(al->name);
    free(al->external_addr);
    free_qdr_auto_link_t(al);
}


void qdr_route_connection_opened_CT(qdr_core_t       *core,
                                    qdr_connection_t *conn,
                                    qdr_field_t      *container_field,
                                    qdr_field_t      *connection_field)
{
    if (conn->role != QDR_ROLE_ROUTE_CONTAINER)
        return;

    qdr_conn_identifier_t *cid = qdr_route_declare_id_CT(core,
            container_field?container_field->iterator:0, connection_field?connection_field->iterator:0);

    qdr_add_connection_ref(&cid->connection_refs, conn);

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
        qdr_auto_link_activate_CT(core, al, conn);
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
            qdr_auto_link_deactivate_CT(core, al, conn);
            al = DEQ_NEXT_N(REF, al);
        }

        //
        // Remove our own entry in the connection list
        //
        qdr_del_connection_ref(&cid->connection_refs, conn);

        conn->conn_id        = 0;

        qdr_route_check_id_for_deletion_CT(core, cid);
    }
}

