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

#include "agent_link.h"
#include <stdio.h>

#define QDR_ROUTE_NAME               0
#define QDR_ROUTE_IDENTITY           1
#define QDR_ROUTE_TYPE               2
#define QDR_ROUTE_OBJECT_TYPE        3
#define QDR_ROUTE_ADDRESS            4
#define QDR_ROUTE_CONNECTOR          5
#define QDR_ROUTE_DIRECTION          6
#define QDR_ROUTE_TREATMENT          7
#define QDR_ROUTE_INGRESS_ADDRESS    8
#define QDR_ROUTE_EGRESS_ADDRESS     9
#define QDR_ROUTE_INGRESS_TREATMENT  10
#define QDR_ROUTE_EGRESS_TREATMENT   11

const char *qdr_route_columns[] =
    {"name",
     "identity",
     "type",
     "objectType",
     "address",
     "connector",
     "direction",
     "treatment",
     "ingressAddress",
     "egressAddress",
     "ingressTreatment",
     "egressTreatment",
     0};


static void qdr_route_insert_column_CT(qdr_route_t *route, int col, qd_composed_field_t *body, bool as_map)
{
    if (as_map)
        qd_compose_insert_string(body, qdr_route_columns[col]);

    switch(col) {
    case QDR_ROUTE_NAME:
        if (route->name) {
            qd_compose_insert_string(body, route->name);
            break;
        }
        // else fall into IDENTITY

    case QDR_ROUTE_IDENTITY:

    case QDR_ROUTE_TYPE:
        qd_compose_insert_string(body, "org.apache.qpid.dispatch.router.route");
        break;

    case QDR_ROUTE_OBJECT_TYPE:
    case QDR_ROUTE_ADDRESS:
    case QDR_ROUTE_CONNECTOR:
    case QDR_ROUTE_DIRECTION:
    case QDR_ROUTE_TREATMENT:
    case QDR_ROUTE_INGRESS_ADDRESS:
    case QDR_ROUTE_EGRESS_ADDRESS:
    case QDR_ROUTE_INGRESS_TREATMENT:
    case QDR_ROUTE_EGRESS_TREATMENT:
    default:
        qd_compose_insert_null(body);
        break;
    }
}


static void qdr_agent_write_route_CT(qdr_query_t *query,  qdr_route_t *route)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    while (query->columns[i] >= 0) {
        qdr_route_insert_column_CT(route, query->columns[i], body, false);
        i++;
    }
    qd_compose_end_list(body);
}

static void qdr_manage_advance_route_CT(qdr_query_t *query, qdr_route_t *route)
{
    query->next_offset++;
    route = DEQ_NEXT(route);
    query->more = !!route;
}


void qdra_route_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = &QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->routes)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_route_t *route = DEQ_HEAD(core->routes);
    for (int i = 0; i < offset && route; i++)
        route = DEQ_NEXT(route);
    assert(route);

    //
    // Write the columns of the object into the response body.
    //
    qdr_agent_write_route_CT(query, route);

    //
    // Advance to the next address
    //
    query->next_offset = offset;
    qdr_manage_advance_route_CT(query, route);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_route_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_route_t *route = 0;

        if (query->next_offset < DEQ_SIZE(core->routes)) {
            route = DEQ_HEAD(core->routes);
            for (int i = 0; i < query->next_offset && route; i++)
                route = DEQ_NEXT(route);
        }

    if (route) {
        //
        // Write the columns of the route entity into the response body.
        //
        qdr_agent_write_route_CT(query, route);

        //
        // Advance to the next object
        //
        qdr_manage_advance_route_CT(query, route);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static qd_address_treatment_t qdra_treatment(qd_parsed_field_t *field)
{
    if (field) {
        qd_field_iterator_t *iter = qd_parse_raw(field);
        if (qd_field_iterator_equal(iter, (unsigned char*) "multi"))       return QD_TREATMENT_MULTICAST_ONCE;
        if (qd_field_iterator_equal(iter, (unsigned char*) "anyClosest"))  return QD_TREATMENT_ANYCAST_CLOSEST;
        if (qd_field_iterator_equal(iter, (unsigned char*) "anyBalanced")) return QD_TREATMENT_ANYCAST_BALANCED;
    }
    return QD_TREATMENT_ANYCAST_BALANCED;
}


static qdr_address_config_t *qdra_configure_address_prefix_CT(qdr_core_t *core, qd_parsed_field_t *addr_field, char cls,
                                                              qd_address_treatment_t treatment)
{
    if (!addr_field)
        return 0;

    qd_field_iterator_t *iter = qd_parse_raw(addr_field);
    qd_address_iterator_override_prefix(iter, cls);
    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);

    qdr_address_config_t *addr = 0;
    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    if (addr) {
        // Log error TODO
        return 0;
    }

    addr = new_qdr_address_config_t();
    DEQ_ITEM_INIT(addr);
    addr->treatment = treatment;

    if (!!addr) {
        qd_field_iterator_reset(iter);
        qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_INSERT_TAIL(core->addr_config, addr);
    }

    return addr;
}


static qdr_address_t *qdra_configure_address_CT(qdr_core_t *core, qd_parsed_field_t *addr_field, char cls,
                                                qd_address_treatment_t treatment)
{
    if (!addr_field)
        return 0;

    qd_field_iterator_t *iter = qd_parse_raw(addr_field);
    qd_address_iterator_override_prefix(iter, cls);
    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);

    qdr_address_t *addr = 0;
    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    if (addr) {
        // Log error TODO
        return 0;
    }

    addr = qdr_address_CT(core, treatment);

    if (!!addr) {
        qd_field_iterator_reset(iter);
        qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_INSERT_TAIL(core->addrs, addr);
    }

    return addr;
}


void qdra_route_create_CT(qdr_core_t *core, qd_field_iterator_t *name,
                          qdr_query_t *query, qd_parsed_field_t *in_body)
{
    // TODO - reject duplicate names

    if (qd_parse_is_map(in_body)) {
        qd_parsed_field_t *type_field     = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_OBJECT_TYPE]);
        qd_parsed_field_t *addr_field     = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_ADDRESS]);
        qd_parsed_field_t *conn_field     = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_CONNECTOR]);
        qd_parsed_field_t *dir_field      = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_DIRECTION]);
        qd_parsed_field_t *sem_field      = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_TREATMENT]);
        //qd_parsed_field_t *in_addr_field  = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_INGRESS_ADDRESS]);
        //qd_parsed_field_t *out_addr_field = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_EGRESS_ADDRESS]);
        //qd_parsed_field_t *in_sem_field   = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_INGRESS_TREATMENT]);
        //qd_parsed_field_t *out_sem_field  = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_EGRESS_TREATMENT]);

        bool still_good = true;
        qdr_route_t *route = new_qdr_route_t();
        ZERO(route);

        route->identity = qdr_identifier(core);
        if (name)
            route->name = (char*) qd_field_iterator_copy(name);

        if (!type_field)
            route->object_type = QDR_ROUTE_TYPE_ADDRESS;
        else {
            qd_field_iterator_t *type_iter = qd_parse_raw(type_field);
            if      (qd_field_iterator_equal(type_iter, (unsigned char*) "address"))
                route->object_type = QDR_ROUTE_TYPE_ADDRESS;
            else if (qd_field_iterator_equal(type_iter, (unsigned char*) "linkDestination"))
                route->object_type = QDR_ROUTE_TYPE_LINK_DEST;
            else if (qd_field_iterator_equal(type_iter, (unsigned char*) "waypoint"))
                route->object_type = QDR_ROUTE_TYPE_WAYPOINT;
            else
                still_good = false;
        }

        route->treatment = qdra_treatment(sem_field);

        route->direction_in  = true;
        route->direction_out = true;
        if (dir_field) {
            qd_field_iterator_t *dir_iter = qd_parse_raw(dir_field);
            if (qd_field_iterator_equal(dir_iter, (unsigned char*) "in"))
                route->direction_out = false;
            if (qd_field_iterator_equal(dir_iter, (unsigned char*) "out"))
                route->direction_in = false;
        }

        if (conn_field) {
            qd_field_iterator_t *conn_iter  = qd_parse_raw(conn_field);
            route->connector_label = (char*) qd_field_iterator_copy(conn_iter);
        }

        switch (route->object_type) {
        case QDR_ROUTE_TYPE_ADDRESS:
            route->addr_config = qdra_configure_address_prefix_CT(core, addr_field, 'Z', route->treatment);
            break;

        case QDR_ROUTE_TYPE_LINK_DEST:
            if (route->direction_in)
                route->ingress_addr = qdra_configure_address_CT(core, addr_field, 'C', route->treatment);
            if (route->direction_out)
                route->egress_addr  = qdra_configure_address_CT(core, addr_field, 'D', route->treatment);
            break;

        case QDR_ROUTE_TYPE_WAYPOINT:
            break;
        }

        if (still_good) {
            // TODO - write response map
            query->status = &QD_AMQP_CREATED;
            DEQ_INSERT_TAIL(core->routes, route);
        } else {
            query->status = &QD_AMQP_BAD_REQUEST;
            if (route->name)
                free(route->name);
            free_qdr_route_t(route);
        }
    }
    else
        query->status = &QD_AMQP_BAD_REQUEST;

    //
    // Enqueue the response.
    //
    if (query->body)
        qdr_agent_enqueue_response_CT(core, query);
    else
        free_qdr_query_t(query);
}
