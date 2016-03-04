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

#include "agent_route.h"
#include "route_control.h"
#include <stdio.h>

#define QDR_ROUTE_NAME          0
#define QDR_ROUTE_IDENTITY      1
#define QDR_ROUTE_TYPE          2
#define QDR_ROUTE_ADDRESS       3
#define QDR_ROUTE_PATH          4
#define QDR_ROUTE_TREATMENT     5
#define QDR_ROUTE_CONNECTORS    6
#define QDR_ROUTE_CONTAINERS    7
#define QDR_ROUTE_ROUTE_ADDRESS 8

const char *qdr_route_columns[] =
    {"name",
     "identity",
     "type",
     "address",
     "path",
     "treatment",
     "connectors",
     "containers",
     "routeAddress",
     0};


static void qdr_route_insert_column_CT(qdr_route_config_t *route, int col, qd_composed_field_t *body, bool as_map)
{
    const char *text = 0;
    qdr_route_active_t *active;
    const char         *key;

    if (as_map)
        qd_compose_insert_string(body, qdr_route_columns[col]);

    switch(col) {
    case QDR_ROUTE_NAME:
        if (route->name)
            qd_compose_insert_string(body, route->name);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_ROUTE_IDENTITY: {
        char id_str[100];
        snprintf(id_str, 100, "%ld", route->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_ROUTE_TYPE:
        qd_compose_insert_string(body, "org.apache.qpid.dispatch.route");
        break;

    case QDR_ROUTE_ADDRESS:
        if (route->addr_config)
            qd_compose_insert_string(body, (const char*) qd_hash_key_by_handle(route->addr_config->hash_handle));
        else
            qd_compose_insert_null(body);
        break;

    case QDR_ROUTE_PATH:
        switch (route->path) {
        case QDR_ROUTE_PATH_DIRECT:   text = "direct";  break;
        case QDR_ROUTE_PATH_SOURCE:   text = "source";  break;
        case QDR_ROUTE_PATH_SINK:     text = "sink";    break;
        case QDR_ROUTE_PATH_WAYPOINT: text = "waypoint"; break;
        }
        qd_compose_insert_string(body, text);
        break;

    case QDR_ROUTE_TREATMENT:
        switch (route->treatment) {
        case QD_TREATMENT_MULTICAST_FLOOD:
        case QD_TREATMENT_MULTICAST_ONCE:   text = "multicast";    break;
        case QD_TREATMENT_ANYCAST_CLOSEST:  text = "closest";      break;
        case QD_TREATMENT_ANYCAST_BALANCED: text = "balanced";     break;
        case QD_TREATMENT_LINK_BALANCED:    text = "linkBalanced"; break;
        }
        qd_compose_insert_string(body, text);
        break;

    case QDR_ROUTE_CONNECTORS:
        qd_compose_start_list(body);
        active = DEQ_HEAD(route->active_list);
        while(active) {
            key = (const char*) qd_hash_key_by_handle(active->conn_id->hash_handle);
            if (key && key[0] == 'L')
                qd_compose_insert_string(body, &key[1]);
            active = DEQ_NEXT(active);
        }
        qd_compose_end_list(body);
        break;

    case QDR_ROUTE_CONTAINERS:
        qd_compose_start_list(body);
        active = DEQ_HEAD(route->active_list);
        while(active) {
            key = (const char*) qd_hash_key_by_handle(active->conn_id->hash_handle);
            if (key && key[0] == 'C')
                qd_compose_insert_string(body, &key[1]);
            active = DEQ_NEXT(active);
        }
        qd_compose_end_list(body);
        break;

    case QDR_ROUTE_ROUTE_ADDRESS:
        qd_compose_insert_null(body);
        break;
    }
}


static void qdr_agent_write_route_CT(qdr_query_t *query,  qdr_route_config_t *route)
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

static void qdr_manage_advance_route_CT(qdr_query_t *query, qdr_route_config_t *route)
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
    query->status = QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->route_config)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_route_config_t *route = DEQ_HEAD(core->route_config);
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
    qdr_route_config_t *route = 0;

    if (query->next_offset < DEQ_SIZE(core->route_config)) {
        route = DEQ_HEAD(core->route_config);
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
        if (qd_field_iterator_equal(iter, (unsigned char*) "multicast"))    return QD_TREATMENT_MULTICAST_ONCE;
        if (qd_field_iterator_equal(iter, (unsigned char*) "closest"))      return QD_TREATMENT_ANYCAST_CLOSEST;
        if (qd_field_iterator_equal(iter, (unsigned char*) "balanced"))     return QD_TREATMENT_ANYCAST_BALANCED;
        if (qd_field_iterator_equal(iter, (unsigned char*) "linkBalanced")) return QD_TREATMENT_LINK_BALANCED;
    }
    return QD_TREATMENT_ANYCAST_BALANCED;
}


void qdra_route_create_CT(qdr_core_t *core, qd_field_iterator_t *name,
                          qdr_query_t *query, qd_parsed_field_t *in_body)
{
    // TODO - Validation
    //    - No duplicate names
    //    - For "direct" path, no containers or connections
    //    - For "direct" path, no link-* treatments

    while (true) {
        //
        // Validation of the request occurs here.  Make sure the body is a map.
        //
        if (!qd_parse_is_map(in_body)) {
            query->status = QD_AMQP_BAD_REQUEST;
            break;
        }

        //
        // Extract the fields from the request
        //
        qd_parsed_field_t *addr_field       = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_ADDRESS]);
        qd_parsed_field_t *path_field       = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_PATH]);
        qd_parsed_field_t *conn_field       = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_CONNECTORS]);
        qd_parsed_field_t *cont_field       = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_CONTAINERS]);
        qd_parsed_field_t *treatment_field  = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_TREATMENT]);
        qd_parsed_field_t *route_addr_field = qd_parse_value_by_key(in_body, qdr_route_columns[QDR_ROUTE_ROUTE_ADDRESS]);

        //
        // Determine the path, which defaults to Direct
        //
        qdr_route_path_t path = QDR_ROUTE_PATH_DIRECT;
        if (path_field) {
            qd_field_iterator_t *path_iter = qd_parse_raw(path_field);
            if      (qd_field_iterator_equal(path_iter, (unsigned char*) "direct"))
                path = QDR_ROUTE_PATH_DIRECT;
            else if (qd_field_iterator_equal(path_iter, (unsigned char*) "source"))
                path = QDR_ROUTE_PATH_SOURCE;
            else if (qd_field_iterator_equal(path_iter, (unsigned char*) "sink"))
                path = QDR_ROUTE_PATH_SINK;
            else if (qd_field_iterator_equal(path_iter, (unsigned char*) "waypoint"))
                path = QDR_ROUTE_PATH_WAYPOINT;
            else {
                query->status = QD_AMQP_BAD_REQUEST;
                break;
            }
        }

        qd_address_treatment_t treatment = qdra_treatment(treatment_field);

        //
        // Ask the route_control module to create the route object and put into effect any needed
        // side effects.
        //
        qdr_route_config_t *route;
        const char *error = qdr_route_create_CT(core, name, path, treatment, addr_field, route_addr_field, &route);

        if (error) {
            query->status.status      = 400;
            query->status.description = error;
            break;
        }

        //
        // Add the initial list of connection labels to the route
        //
        if (conn_field && qd_parse_is_list(conn_field)) {
            uint32_t count = qd_parse_sub_count(conn_field);
            for (uint32_t i = 0; i < count; i++) {
                qd_parsed_field_t *conn_label = qd_parse_sub_value(conn_field, i);
                qdr_route_connection_add_CT(core, route, conn_label, false);
            }
        }

        //
        // Add the initial list of container IDs to the route
        //
        if (cont_field && qd_parse_is_list(cont_field)) {
            uint32_t count = qd_parse_sub_count(cont_field);
            for (uint32_t i = 0; i < count; i++) {
                qd_parsed_field_t *cont_id = qd_parse_sub_value(cont_field, i);
                qdr_route_connection_add_CT(core, route, cont_id, true);
            }
        }

        //
        // Compose the result map for the response.
        //
        if (query->body) {
            qd_compose_start_map(query->body);
            for (int col = 0; col < QDR_ROUTE_COLUMN_COUNT; col++)
                qdr_route_insert_column_CT(route, col, query->body, true);
            qd_compose_end_map(query->body);
        }

        query->status = QD_AMQP_CREATED;
        break;
    }

    //
    // Enqueue the response if there is a body. If there is no body, this is a management
    // operation created internally by the configuration file parser.
    //
    if (query->body) {
        if (query->status.status / 100 > 2)
            qd_compose_insert_null(query->body);
        qdr_agent_enqueue_response_CT(core, query);
    } else
        free_qdr_query_t(query);
}
