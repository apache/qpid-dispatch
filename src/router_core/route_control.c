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
#include "router_core_private.h"
#include <inttypes.h>
#include <stdio.h>
#include <qpid/dispatch/iterator.h>

ALLOC_DEFINE(qdr_link_route_t);
ALLOC_DEFINE(qdr_auto_link_t);
ALLOC_DEFINE(qdr_conn_identifier_t);

//
// Note that these hash prefixes are in a different space than those listed in iterator.h.
// These are used in a different hash table than the address hash.
//
const char CONTAINER_PREFIX = 'C';
const char CONNECTION_PREFIX = 'L';

const int AUTO_LINK_FIRST_RETRY_INTERVAL = 2;
const int AUTO_LINK_RETRY_INTERVAL = 5;


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
        qd_hash_handle_free(cid->connection_hash_handle);
        qd_hash_handle_free(cid->container_hash_handle);
        free_qdr_conn_identifier_t(cid);
    }
}


static void qdr_route_log_CT(qdr_core_t *core, const char *text, const char *name, uint64_t id, qdr_connection_t *conn)
{
    const char *key = NULL;
    const char *type = "<unknown>";
    if (conn->conn_id) {
        key = (const char*) qd_hash_key_by_handle(conn->conn_id->connection_hash_handle);
        if (!key) {
            key = (const char*) qd_hash_key_by_handle(conn->conn_id->container_hash_handle);
        }
        if (key)
            type = (*key++ == 'L') ? "connection" : "container";
    }
    if (!key && conn->connection_info) {
        type = "container";
        key = conn->connection_info->container;
    }

    char  id_string[64];
    const char *log_name = name ? name : id_string;

    if (!name)
        snprintf(id_string, 64, "%"PRId64, id);

    qd_log(core->log, QD_LOG_INFO, "%s '%s' on %s %s",
           text, log_name, type, key ? key : "<unknown>");
}


// true if pattern is equivalent to an old style prefix match
// (e.g.  non-wildcard-prefix.#
static bool qdr_link_route_pattern_is_prefix(const char *pattern)
{
    const int len = (int) strlen(pattern);
    return (!strchr(pattern, '*') &&
            (strchr(pattern, '#') == &pattern[len - 1]));

}


// convert a link route pattern to a mobile address hash string
// e.g. "a.b.c.#" --> "Ca.b.c"; "a.*.b.#" --> "Ea.*.b.#"
// Caller must free returned string
static char *qdr_link_route_pattern_to_address(const char *pattern,
                                               qd_direction_t dir)
{
    unsigned char *addr;
    qd_iterator_t *iter;
    const int len = (int) strlen(pattern);

    assert(len > 0);
    if (qdr_link_route_pattern_is_prefix(pattern)) {
        // a pattern that is compatible with the old prefix config
        // (i.e. only wildcard is '#' at the end), strip the trailing '#' and
        // advertise them as 'C' or 'D' for backwards compatibility
        iter = qd_iterator_binary(pattern, len - 1, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_prefix(iter, QDR_LINK_ROUTE_HASH(dir, true));
    } else {
        // a true link route pattern
        iter = qd_iterator_string(pattern, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_prefix(iter, QDR_LINK_ROUTE_HASH(dir, false));
    }
    addr = qd_iterator_copy(iter);
    qd_iterator_free(iter);

    // caller must free() result
    return (char *)addr;
}


// convert a link route address in hash format back to a proper pattern.
// e.g. "Ca.b.c" --> "a.b.c.#"; "Ea.*.#.b --> "a.*.#.b"
// Caller responsible for calling free() on returned string
static char *qdr_address_to_link_route_pattern(qd_iterator_t *addr_hash,
                                               qd_direction_t *dir)
{
    int len = qd_iterator_length(addr_hash);
    char *pattern = malloc(len + 3);   // append ".#" if prefix
    char *rc = 0;
    qd_iterator_strncpy(addr_hash, pattern, len + 1);
    qd_iterator_reset(addr_hash);
    if (QDR_IS_LINK_ROUTE_PREFIX(pattern[0])) {
        // old style link route prefix address.  It needs to be converted to a
        // pattern by appending ".#"
        strcat(pattern, ".#");
    }
    rc = strdup(&pattern[1]);   // skip the prefix
    if (dir)
        *dir = QDR_LINK_ROUTE_DIR(pattern[0]);
    free(pattern);
    return rc;
}


static void qdr_link_route_activate_CT(qdr_core_t *core, qdr_link_route_t *lr, qdr_connection_t *conn)
{
    qdr_route_log_CT(core, "Link Route Activated", lr->name, lr->identity, conn);

    //
    // Activate the address for link-routed destinations.  If this is the first
    // activation for this address, notify the router module of the added address.
    //
    if (lr->addr)
        qdr_core_bind_address_conn_CT(core, lr->addr, conn);

    lr->active = true;
}


static void qdr_link_route_deactivate_CT(qdr_core_t *core, qdr_link_route_t *lr, qdr_connection_t *conn)
{
    qdr_route_log_CT(core, "Link Route Deactivated", lr->name, lr->identity, conn);

    //
    // Deactivate the address(es) for link-routed destinations.
    //
    if (lr->addr)
        qdr_core_unbind_address_conn_CT(core, lr->addr, conn);

    lr->active = false;
}


static void qdr_auto_link_activate_CT(qdr_core_t *core, qdr_auto_link_t *al, qdr_connection_t *conn)
{
    const char *key;

    qdr_route_log_CT(core, "Auto Link Activated", al->name, al->identity, conn);

    if (al->addr) {
        qdr_terminus_t *source = qdr_terminus(0);
        qdr_terminus_t *target = qdr_terminus(0);
        qdr_terminus_t *term;

        if (al->dir == QD_INCOMING)
            term = source;
        else
            term = target;

        key = (const char*) qd_hash_key_by_handle(al->addr->hash_handle);
        if (key || al->external_addr) {
            if (al->external_addr) {
                qdr_terminus_set_address(term, al->external_addr);
                if (key)
                    al->internal_addr = &key[2];
            } else
                qdr_terminus_set_address(term, &key[2]); // truncate the "Mp" annotation (where p = phase)
            al->link = qdr_create_link_CT(core, conn, QD_LINK_ENDPOINT, al->dir, source, target);
            al->link->auto_link = al;
            al->link->phase     = al->phase;
            al->state = QDR_AUTO_LINK_STATE_ATTACHING;
        }
        else {
            free_qdr_terminus_t(source);
            free_qdr_terminus_t(target);
        }
    }
}


/**
 * Attempts re-establishing auto links across the related connections/containers
 */
static void qdr_route_attempt_auto_link_CT(qdr_core_t      *core,
                                    void *context)
{
    qdr_auto_link_t *al = (qdr_auto_link_t *)context;
    qdr_connection_ref_t * cref = DEQ_HEAD(al->conn_id->connection_refs);
    while (cref) {
        qdr_auto_link_activate_CT(core, al, cref->conn);
        cref = DEQ_NEXT(cref);
    }

}


static void qdr_auto_link_deactivate_CT(qdr_core_t *core, qdr_auto_link_t *al, qdr_connection_t *conn)
{
    qdr_route_log_CT(core, "Auto Link Deactivated", al->name, al->identity, conn);

    if (al->link) {
        qdr_link_outbound_detach_CT(core, al->link, 0, QDR_CONDITION_NONE, true);
        al->link->auto_link = 0;
        al->link->phase     = 0;
        al->link            = 0;
    }

    al->state = QDR_AUTO_LINK_STATE_INACTIVE;
}


// router.config.linkRoute
qdr_link_route_t *qdr_route_add_link_route_CT(qdr_core_t             *core,
                                              qd_iterator_t          *name,
                                              const char             *addr_pattern,
                                              bool                    is_prefix,
                                              qd_parsed_field_t      *add_prefix_field,
                                              qd_parsed_field_t      *del_prefix_field,
                                              qd_parsed_field_t      *container_field,
                                              qd_parsed_field_t      *connection_field,
                                              qd_address_treatment_t  treatment,
                                              qd_direction_t          dir)
{
    //
    // Set up the link_route structure
    //
    qdr_link_route_t *lr = new_qdr_link_route_t();
    ZERO(lr);
    lr->identity  = qdr_identifier(core);
    lr->name      = name ? (char*) qd_iterator_copy(name) : 0;
    lr->dir       = dir;
    lr->treatment = treatment;
    lr->is_prefix = is_prefix;
    lr->pattern   = strdup(addr_pattern);

    if (!!add_prefix_field) {
        qd_iterator_t *ap_iter = qd_parse_raw(add_prefix_field);
        int ap_len = qd_iterator_length(ap_iter);
        lr->add_prefix = malloc(ap_len + 1);
        qd_iterator_strncpy(ap_iter, lr->add_prefix, ap_len + 1);
    }
    if (!!del_prefix_field) {
        qd_iterator_t *ap_iter = qd_parse_raw(del_prefix_field);
        int ap_len = qd_iterator_length(ap_iter);
        lr->del_prefix = malloc(ap_len + 1);
        qd_iterator_strncpy(ap_iter, lr->del_prefix, ap_len + 1);
    }
    //
    // Add the address to the routing hash table and map it as a pattern in the
    // wildcard pattern parse tree
    //
    {
        char *addr_hash = qdr_link_route_pattern_to_address(lr->pattern, dir);
        qd_iterator_t *a_iter = qd_iterator_string(addr_hash, ITER_VIEW_ALL);
        qd_hash_retrieve(core->addr_hash, a_iter, (void*) &lr->addr);
        if (!lr->addr) {
            lr->addr = qdr_address_CT(core, treatment, 0);
            if (lr->add_prefix) {
                lr->addr->add_prefix = (char*) malloc(strlen(lr->add_prefix) + 1);
                strcpy(lr->addr->add_prefix, lr->add_prefix);
            }
            if (lr->del_prefix) {
                lr->addr->del_prefix = (char*) malloc(strlen(lr->del_prefix) + 1);
                strcpy(lr->addr->del_prefix, lr->del_prefix);
            }
            //treatment will not be undefined for link route so above will not return null
            DEQ_INSERT_TAIL(core->addrs, lr->addr);
            qd_hash_insert(core->addr_hash, a_iter, lr->addr, &lr->addr->hash_handle);
            qdr_link_route_map_pattern_CT(core, a_iter, lr->addr);
        }

        qd_iterator_free(a_iter);
        free(addr_hash);
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
    qd_log(core->log, QD_LOG_TRACE, "Link route %spattern added: pattern=%s name=%s",
           is_prefix ? "prefix " : "", lr->pattern, lr->name);

    return lr;
}


void qdr_route_auto_link_detached_CT(qdr_core_t *core, qdr_link_t *link)
{
    if (!link->auto_link)
        return;

    if (!link->auto_link->retry_timer)
        link->auto_link->retry_timer = qdr_core_timer_CT(core, qdr_route_attempt_auto_link_CT, (void *)link->auto_link);

    static char *activation_failed = "Auto Link Activation Failed. ";
    int error_length = link->auto_link->last_error ? strlen(link->auto_link->last_error) : 0;
    int total_length = strlen(activation_failed) + error_length + 1;

    char error_msg[total_length];
    strcpy(error_msg, activation_failed);
    if (error_length)
        strcat(error_msg, link->auto_link->last_error);

    if (link->auto_link->retry_attempts == 0) {
        // First retry in 2 seconds
        qdr_core_timer_schedule_CT(core, link->auto_link->retry_timer, AUTO_LINK_FIRST_RETRY_INTERVAL);
        link->auto_link->retry_attempts += 1;
    }
    else {
        // Successive retries every 5 seconds
        qdr_core_timer_schedule_CT(core, link->auto_link->retry_timer, AUTO_LINK_RETRY_INTERVAL);
    }

    qdr_route_log_CT(core, error_msg, link->auto_link->name, link->auto_link->identity, link->conn);
}


void qdr_route_auto_link_closed_CT(qdr_core_t *core, qdr_link_t *link)
{
    if (link->auto_link && link->auto_link->retry_timer)
        qdr_core_timer_cancel_CT(core, link->auto_link->retry_timer);
}


// router.config.linkRoute
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
    // (and its associated pattern) should be removed.
    //
    qdr_address_t *addr = lr->addr;
    if (addr && --addr->ref_count == 0)
        qdr_check_addr_CT(core, addr);

    //
    // Remove the link route from the core list.
    //
    DEQ_REMOVE(core->link_routes, lr);
    qd_log(core->log, QD_LOG_TRACE, "Link route %spattern removed: pattern=%s name=%s",
           lr->is_prefix ? "prefix " : "", lr->pattern, lr->name);
    qdr_core_delete_link_route(core, lr);
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
        qdr_address_config_t   *addr_config = qdr_config_for_address_CT(core, 0, iter);
        qd_address_treatment_t  treatment   = addr_config ? addr_config->treatment : QD_TREATMENT_ANYCAST_BALANCED;

        if (treatment == QD_TREATMENT_UNAVAILABLE) {
            //if associated address is not defined, assume balanced
            treatment = QD_TREATMENT_ANYCAST_BALANCED;
        }
        al->addr = qdr_address_CT(core, treatment, addr_config);
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
        qdr_check_addr_CT(core, addr);

    //
    // Remove the auto link from the core list.
    //
    DEQ_REMOVE(core->auto_links, al);
    free(al->name);
    free(al->external_addr);
    qdr_core_timer_free_CT(core, al->retry_timer);
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
    //
    // release any connection-based link routes.  These can exist on
    // QDR_ROLE_NORMAL connections.
    //
    while (DEQ_HEAD(conn->conn_link_routes)) {
        qdr_link_route_t *lr = DEQ_HEAD(conn->conn_link_routes);
        // removes the link route from conn->link_routes
        qdr_route_del_conn_route_CT(core, lr);
    }

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


// add the link route address pattern to the lookup tree
// address is the hashed address, which includes 'C','D','E', or 'F' classifier
// in the first char.  The pattern follows in the second char.
void qdr_link_route_map_pattern_CT(qdr_core_t *core, qd_iterator_t *address, qdr_address_t *addr)
{
    qd_direction_t dir;
    char *pattern = qdr_address_to_link_route_pattern(address, &dir);
    qd_iterator_t *iter = qd_iterator_string(pattern, ITER_VIEW_ALL);

    qdr_address_t *other_addr;
    bool found = qd_parse_tree_get_pattern(core->link_route_tree[dir], iter, (void **)&other_addr);
    if (!found) {
        qd_parse_tree_add_pattern(core->link_route_tree[dir], iter, addr);
    } else {
        // the pattern is mapped once when the address is added to the hash
        // table.  It should not be mapped twice
        qd_log(core->log, QD_LOG_CRITICAL, "Link route %s mapped redundantly!",
               pattern);
    }

    qd_iterator_free(iter);
    free(pattern);
}


// remove the link route address pattern from the lookup tree.
// address is the hashed address, which includes 'C','D','E', or 'F' classifier
// in the first char.  The pattern follows in the second char.
void qdr_link_route_unmap_pattern_CT(qdr_core_t *core, qd_iterator_t *address)
{
    qd_direction_t dir;
    char *pattern = qdr_address_to_link_route_pattern(address, &dir);
    qd_iterator_t *iter = qd_iterator_string(pattern, ITER_VIEW_ALL);
    qdr_address_t *addr;
    bool found = qd_parse_tree_get_pattern(core->link_route_tree[dir], iter, (void **)&addr);
    if (found) {
        qd_parse_tree_remove_pattern(core->link_route_tree[dir], iter);
    } else {
        // expected that the pattern is removed when the address is deleted.
        // Attempting to remove it twice is unexpected
        qd_log(core->log, QD_LOG_CRITICAL, "link route pattern ummap: Pattern '%s' not found",
               pattern);
    }

    qd_iterator_free(iter);
    free(pattern);
}


qdr_link_route_t *qdr_route_add_conn_route_CT(qdr_core_t             *core,
                                              qdr_connection_t       *conn,
                                              qd_iterator_t          *name,
                                              const char             *addr_pattern,
                                              qd_direction_t          dir)
{
    //
    // Set up the link_route structure
    //
    qdr_link_route_t *lr = new_qdr_link_route_t();
    ZERO(lr);
    lr->identity  = qdr_identifier(core);
    lr->name      = name ? (char*) qd_iterator_copy(name) : 0;
    lr->dir       = dir;
    lr->treatment = QD_TREATMENT_LINK_BALANCED;
    lr->is_prefix = false;
    lr->pattern   = strdup(addr_pattern);
    lr->parent_conn = conn;

    //
    // Add the address to the routing hash table and map it as a pattern in the
    // wildcard pattern parse tree
    //
    {
        char *addr_hash = qdr_link_route_pattern_to_address(lr->pattern, dir);
        qd_iterator_t *a_iter = qd_iterator_string(addr_hash, ITER_VIEW_ALL);
        qd_hash_retrieve(core->addr_hash, a_iter, (void*) &lr->addr);
        if (!lr->addr) {
            lr->addr = qdr_address_CT(core, lr->treatment, 0);
            DEQ_INSERT_TAIL(core->addrs, lr->addr);
            qd_hash_insert(core->addr_hash, a_iter, lr->addr, &lr->addr->hash_handle);
            qdr_link_route_map_pattern_CT(core, a_iter, lr->addr);
        }

        qd_iterator_free(a_iter);
        free(addr_hash);
    }
    lr->addr->ref_count++;

    //
    // Add the link route to the parent connection's link route list
    // and fire it up
    //
    DEQ_INSERT_TAIL(conn->conn_link_routes, lr);
    qdr_link_route_activate_CT(core, lr, lr->parent_conn);

    qd_log(core->log, QD_LOG_TRACE,
           "Connection based link route pattern added: conn=%s pattern=%s name=%s",
           conn->connection_info->container, lr->pattern, lr->name);
    return lr;
}


void qdr_route_del_conn_route_CT(qdr_core_t       *core,
                                 qdr_link_route_t *lr)
{
    qdr_connection_t *conn = lr->parent_conn;
    qdr_link_route_deactivate_CT(core, lr, conn);

    //
    // Disassociate the link route from its address.  Check to see if the address
    // (and its associated pattern) should be removed.
    //
    qdr_address_t *addr = lr->addr;
    if (addr && --addr->ref_count == 0)
        qdr_check_addr_CT(core, addr);

    //
    // Remove the link route from the parent's link route list
    //
    DEQ_REMOVE(conn->conn_link_routes, lr);
    qd_log(core->log, QD_LOG_TRACE,
           "Connection based link route pattern removed: conn=%s pattern=%s name=%s",
           conn->connection_info->container, lr->pattern, lr->name);
    qdr_core_delete_link_route(core, lr);
}
