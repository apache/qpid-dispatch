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

#include "agent_conn_link_route.h"

#include "agent_config_address.h"
#include "agent_config_link_route.h"
#include "route_control.h"

#include <inttypes.h>
#include <stdio.h>

const char *qdr_conn_link_route_columns[QDR_CONN_LINK_ROUTE_COLUMN_COUNT + 1] =
    {"name",
     "identity",
     "type",
     "pattern",
     "direction",
     "containerId",
     0};

const char *CONN_LINK_ROUTE_TYPE = "org.apache.qpid.dispatch.router.connection.linkRoute";


static void _insert_column_CT(qdr_link_route_t *lr, int col, qd_composed_field_t *body, bool as_map)
{
    if (as_map)
        qd_compose_insert_string(body, qdr_conn_link_route_columns[col]);

    switch(col) {
    case QDR_CONN_LINK_ROUTE_NAME:
        if (lr->name)
            qd_compose_insert_string(body, lr->name);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONN_LINK_ROUTE_IDENTITY: {
        char id_str[100];
        snprintf(id_str, 100, "%"PRId64, lr->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_CONN_LINK_ROUTE_TYPE:
        qd_compose_insert_string(body, CONN_LINK_ROUTE_TYPE);
        break;

    case QDR_CONN_LINK_ROUTE_PATTERN:
        qd_compose_insert_string(body, lr->pattern);
        break;
    case QDR_CONN_LINK_ROUTE_DIRECTION:
        qd_compose_insert_string(body,
                                 lr->dir == QD_INCOMING
                                 ? "in"
                                 : "out");
        break;
    case QDR_CONN_LINK_ROUTE_CONTAINER_ID:
        if (lr->parent_conn && lr->parent_conn->connection_info) {
            if (lr->parent_conn->connection_info->container) {
                qd_compose_insert_string(body,
                                         lr->parent_conn->connection_info->container);
                break;
            }
        }
        qd_compose_insert_null(body);
        break;
    }
}


static void _write_as_list_CT(qdr_query_t *query, qdr_link_route_t *lr)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    while (query->columns[i] >= 0) {
        _insert_column_CT(lr, query->columns[i], body, false);
        i++;
    }
    qd_compose_end_list(body);
}


static void _write_as_map_CT(qdr_query_t *query, qdr_link_route_t *lr)
{
    qd_composed_field_t *body = query->body;
    qd_compose_start_map(body);
    for (int col = 0; col < QDR_CONN_LINK_ROUTE_COLUMN_COUNT; col++)
        _insert_column_CT(lr, col, body, true);
    qd_compose_end_map(body);
}



// get conn via identity
static qdr_connection_t *_find_conn_CT(qdr_core_t *core, uint64_t conn_id)
{
    qdr_connection_t *conn = DEQ_HEAD(core->open_connections);
    while (conn) {
        if (conn->identity == conn_id)
            break;
        conn = DEQ_NEXT(conn);
    }
    return conn;
}


// get the link route by either name or id
static qdr_link_route_t *_find_link_route_CT(qdr_connection_t *conn,
                                             qd_iterator_t *name, qd_iterator_t *identity)
{
    qdr_link_route_t *lr = NULL;

    // if both id and name provided, prefer id
    //
    if (identity) {
        char buf[64];
        uint64_t id = 0;
        assert(qd_iterator_length(identity) < sizeof(buf));
        qd_iterator_strncpy(identity, buf, sizeof(buf));
        if (sscanf(buf, "%"SCNu64, &id) != 1) {
            return NULL;
        }
        lr = DEQ_HEAD(conn->conn_link_routes);
        while (lr) {
            if (id == lr->identity)
                break;
            lr = DEQ_NEXT(lr);
        }
    } else if (name) {
        lr = DEQ_HEAD(conn->conn_link_routes);
        while (lr) {
            if (qd_iterator_equal(name, (unsigned char *)lr->name))
                break;
            lr = DEQ_NEXT(lr);
        }
    }

    return lr;
}


void qdra_conn_link_route_create_CT(qdr_core_t         *core,
                                    qd_iterator_t      *name,
                                    qdr_query_t        *query,
                                    qd_parsed_field_t  *in_body)
{
    char *pattern = NULL;

    query->status = QD_AMQP_BAD_REQUEST;

    // fail if creating via a configuration file
    if (query->in_conn == 0) {
        query->status.description = "Can only create via management CREATE";
        goto exit;
    }

    // find the associated connection
    qdr_connection_t *conn = _find_conn_CT(core, query->in_conn);
    if (!conn) {
        query->status.description = "Parent connection no longer exists";
        goto exit;
    }

    // fail if forbidden by policy
    bool allow = conn->policy_spec ? conn->policy_spec->allowDynamicLinkRoutes : true;
    if (!allow) {
        query->status = QD_AMQP_FORBIDDEN;
        goto exit;
    }

    if (!qd_parse_is_map(in_body)) {
        query->status.description = "Body of request must be a map";
        goto exit;
    }

    //
    // Extract the fields from the request
    //
    qd_parsed_field_t *pattern_field    = qd_parse_value_by_key(in_body, qdr_conn_link_route_columns[QDR_CONN_LINK_ROUTE_PATTERN]);
    qd_parsed_field_t *dir_field        = qd_parse_value_by_key(in_body, qdr_conn_link_route_columns[QDR_CONN_LINK_ROUTE_DIRECTION]);

    if (!pattern_field) {
        query->status.description = "Pattern field is required";
        goto exit;
    }

    const char *error = NULL;
    pattern = qdra_config_address_validate_pattern_CT(pattern_field, false, &error);
    if (!pattern) {
        query->status.description = error;
        goto exit;
    }

    qd_direction_t dir;
    error = qdra_link_route_direction_CT(dir_field, &dir);
    if (error) {
        query->status.description = error;
        goto exit;
    }

    qdr_link_route_t *lr = qdr_route_add_conn_route_CT(core, conn, name, pattern, dir);
    if (!lr) {
        query->status.description = "creation failed";
        goto exit;
    }

    query->status = QD_AMQP_CREATED;
    _write_as_map_CT(query, lr);

exit:
    free(pattern);
    if (query->status.status != QD_AMQP_CREATED.status) {
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s",
               CONN_LINK_ROUTE_TYPE, query->status.description);
        qd_compose_insert_null(query->body);  // no body map
    }
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_conn_link_route_delete_CT(qdr_core_t    *core,
                                        qdr_query_t   *query,
                                        qd_iterator_t *name,
                                        qd_iterator_t *identity)
{
    query->status = QD_AMQP_BAD_REQUEST;

    if (!name && !identity) {
        query->status.description = "No name or identity provided";
        goto exit;
    }

    // find the associated connection, if it is not present then the entity has
    // automatically been deleted (not an error)
    //
    qdr_connection_t *conn = _find_conn_CT(core, query->in_conn);
    if (!conn) {
        query->status = QD_AMQP_NO_CONTENT;
        goto exit;
    }

    // find the targetted link route
    qdr_link_route_t *lr = _find_link_route_CT(conn, name, identity);
    if (!lr) {
        query->status = QD_AMQP_NOT_FOUND;
        goto exit;
    }

    qdr_route_del_conn_route_CT(core, lr);
    query->status = QD_AMQP_NO_CONTENT;

exit:
    if (query->status.status != QD_AMQP_NO_CONTENT.status) {
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing DELETE of %s: %s",
               CONN_LINK_ROUTE_TYPE, query->status.description);
    }
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_conn_link_route_get_CT(qdr_core_t    *core,
                                     qd_iterator_t *name,
                                     qd_iterator_t *identity,
                                     qdr_query_t   *query,
                                     const char    *columns[])
{
    query->status = QD_AMQP_BAD_REQUEST;

    if (!name && !identity) {
        query->status.description = "No name or identity provided";
        goto exit;
    }

    qdr_connection_t *conn = _find_conn_CT(core, query->in_conn);
    qdr_link_route_t *lr = (conn) ? _find_link_route_CT(conn, name, identity) : NULL;

    if (!lr) {
        // Send back a 404
        query->status = QD_AMQP_NOT_FOUND;
        goto exit;
    }

    //
    // Write the columns of the linkRoute entity into the response body.
    //
    query->status = QD_AMQP_OK;
    _write_as_map_CT(query, lr);

exit:
    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_conn_link_route_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    query->status = QD_AMQP_OK;

    qdr_connection_t *conn = _find_conn_CT(core, query->in_conn);
    if (!conn || offset >= DEQ_SIZE(conn->conn_link_routes)) {
        query->more = false;
    } else {
        // Find the lr at the offset.
        //
        qdr_link_route_t *lr = DEQ_HEAD(conn->conn_link_routes);

        if (!lr) {
            query->more = false;
            qdr_agent_enqueue_response_CT(core, query);
            return;
        }

        for (int i = 0; i < offset && lr; i++)
            lr = DEQ_NEXT(lr);
        assert(lr);

        if (lr) {
            // write the lr into the response and advance to next
            _write_as_list_CT(query, lr);
            query->next_offset = offset + 1;
            query->more = DEQ_NEXT(lr) != NULL;
        }
        else {
            query->more = false;
        }
    }
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_conn_link_route_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_connection_t *conn = _find_conn_CT(core, query->in_conn);
    if (!conn || query->next_offset >= DEQ_SIZE(conn->conn_link_routes)) {
        query->more = false;
    } else {
        // find the lr at the offset
        //
        qdr_link_route_t *lr = DEQ_HEAD(conn->conn_link_routes);
        for (int i = 0; i < query->next_offset && lr; i++)
            lr = DEQ_NEXT(lr);

        if (lr) {
            // write response and advance to next
            _write_as_list_CT(query, lr);
            ++query->next_offset;
            query->more = DEQ_NEXT(lr) != NULL;
        } else
            query->more = false;
    }
    qdr_agent_enqueue_response_CT(core, query);
}
