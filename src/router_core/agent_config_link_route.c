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

#include "agent_config_link_route.h"

#include "agent_config_address.h"
#include "route_control.h"

#include "qpid/dispatch/ctools.h"

#include <inttypes.h>
#include <stdio.h>

#define QDR_CONFIG_LINK_ROUTE_NAME          0
#define QDR_CONFIG_LINK_ROUTE_IDENTITY      1
#define QDR_CONFIG_LINK_ROUTE_TYPE          2
#define QDR_CONFIG_LINK_ROUTE_PREFIX        3
#define QDR_CONFIG_LINK_ROUTE_DISTRIBUTION  4
#define QDR_CONFIG_LINK_ROUTE_CONNECTION    5
#define QDR_CONFIG_LINK_ROUTE_CONTAINER_ID  6
#define QDR_CONFIG_LINK_ROUTE_DIRECTION     7
#define QDR_CONFIG_LINK_ROUTE_DIR           8
#define QDR_CONFIG_LINK_ROUTE_OPER_STATUS   9
#define QDR_CONFIG_LINK_ROUTE_PATTERN       10
#define QDR_CONFIG_LINK_ROUTE_ADD_EXTERNAL_PREFIX 11
#define QDR_CONFIG_LINK_ROUTE_DEL_EXTERNAL_PREFIX 12

const char *qdr_config_link_route_columns[] =
    {"name",
     "identity",
     "type",
     "prefix",
     "distribution",
     "connection",
     "containerId",
     "direction",
     "dir",
     "operStatus",
     "pattern",
     "addExternalPrefix",
     "delExternalPrefix",
     0};

const char *CONFIG_LINKROUTE_TYPE = "org.apache.qpid.dispatch.router.config.linkRoute";
const char CONFIG_LINK_ROUTE_PREFIX = 'L';

const qd_amqp_error_t QD_AMQP_NAME_IDENTITY_MISSING = { 400, "No name or identity provided" };

static void qdr_config_link_route_insert_column_CT(qdr_link_route_t *lr, int col, qd_composed_field_t *body, bool as_map)
{
    const char *text = 0;
    const char *key;

    if  (!lr)
        return;

    if (as_map)
        qd_compose_insert_string(body, qdr_config_link_route_columns[col]);

    switch(col) {
    case QDR_CONFIG_LINK_ROUTE_NAME:
        if (lr->name)
            qd_compose_insert_string(body, lr->name);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_LINK_ROUTE_IDENTITY: {
        char id_str[100];
        snprintf(id_str, 100, "%"PRId64, lr->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_CONFIG_LINK_ROUTE_TYPE:
        qd_compose_insert_string(body, CONFIG_LINKROUTE_TYPE);
        break;

    case QDR_CONFIG_LINK_ROUTE_PATTERN:
        if (lr->pattern && !lr->is_prefix)
            qd_compose_insert_string(body, lr->pattern);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_LINK_ROUTE_PREFIX:
        if (lr->pattern && lr->is_prefix) {
            // the prefix is converted to a pattern by appending '.#' to the
            // prefix, so strip it off
            const size_t len = strlen(lr->pattern);
            assert(len > 2);
            qd_compose_insert_string_n(body, lr->pattern, len - 2);
        } else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_LINK_ROUTE_ADD_EXTERNAL_PREFIX:
        if (lr->add_prefix)
            qd_compose_insert_string(body, lr->add_prefix);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_LINK_ROUTE_DEL_EXTERNAL_PREFIX:
        if (lr->del_prefix)
            qd_compose_insert_string(body, lr->del_prefix);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_LINK_ROUTE_DISTRIBUTION:
        switch (lr->treatment) {
        case QD_TREATMENT_LINK_BALANCED: text = "linkBalanced"; break;
        default:
            text = 0;
        }

        if (text)
            qd_compose_insert_string(body, text);
        else
            qd_compose_insert_null(body);

        break;

    case QDR_CONFIG_LINK_ROUTE_CONNECTION:
    case QDR_CONFIG_LINK_ROUTE_CONTAINER_ID:
        if (lr->conn_id) {
            key = (const char*) qd_hash_key_by_handle(lr->conn_id->connection_hash_handle);
            if (!key)
                key = (const char*) qd_hash_key_by_handle(lr->conn_id->container_hash_handle);

            if (key && key[0] == 'L' && col == QDR_CONFIG_LINK_ROUTE_CONNECTION) {
                qd_compose_insert_string(body, &key[1]);
                break;
            }
            if (key && key[0] == 'C' && col == QDR_CONFIG_LINK_ROUTE_CONTAINER_ID) {
                qd_compose_insert_string(body, &key[1]);
                break;
            }
        }
        qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_LINK_ROUTE_DIR:
    case QDR_CONFIG_LINK_ROUTE_DIRECTION:
        text = lr->dir == QD_INCOMING ? "in" : "out";
        qd_compose_insert_string(body, text);
        break;

    case QDR_CONFIG_LINK_ROUTE_OPER_STATUS:
        text = lr->active ? "active" : "inactive";
        qd_compose_insert_string(body, text);
        break;
    }
}


static void qdr_agent_write_config_link_route_CT(qdr_query_t *query,  qdr_link_route_t *lr)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    if (lr) {
        while (query->columns[i] >= 0) {
            qdr_config_link_route_insert_column_CT(lr, query->columns[i], body, false);
            i++;
        }
    }
    qd_compose_end_list(body);
}


static void qdr_manage_advance_config_link_route_CT(qdr_query_t *query, qdr_link_route_t *lr)
{
    if (lr){
        query->next_offset++;
        lr = DEQ_NEXT(lr);
        query->more = !!lr;
    }
    else {
        query->more = false;
    }
}


void qdra_config_link_route_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->link_routes)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_link_route_t *lr = DEQ_HEAD(core->link_routes);
    for (int i = 0; i < offset && lr; i++)
        lr = DEQ_NEXT(lr);
    assert(lr);

    //
    // Write the columns of the object into the response body.
    //
    qdr_agent_write_config_link_route_CT(query, lr);

    //
    // Advance to the next link_route
    //
    query->next_offset = offset;
    qdr_manage_advance_config_link_route_CT(query, lr);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static void qdr_manage_write_config_link_route_map_CT(qdr_core_t          *core,
                                                      qdr_link_route_t    *lr,
                                                      qd_composed_field_t *body,
                                                      const char          *qdr_config_link_route_columns[])
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_CONFIG_LINK_ROUTE_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_config_link_route_columns[i]);
        qdr_config_link_route_insert_column_CT(lr, i, body, false);
    }

    qd_compose_end_map(body);
}



void qdra_config_link_route_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_link_route_t *lr = 0;

    if (query->next_offset < DEQ_SIZE(core->link_routes)) {
        lr = DEQ_HEAD(core->link_routes);
        for (int i = 0; i < query->next_offset && lr; i++)
            lr = DEQ_NEXT(lr);
    }

    if (lr) {
        //
        // Write the columns of the addr entity into the response body.
        //
        qdr_agent_write_config_link_route_CT(query, lr);

        //
        // Advance to the next object
        //
        qdr_manage_advance_config_link_route_CT(query, lr);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static const char *qdra_link_route_treatment_CT(qd_parsed_field_t *field, qd_address_treatment_t *trt)
{
    if (field) {
        qd_iterator_t *iter = qd_parse_raw(field);
        if (qd_iterator_equal(iter, (unsigned char*) "linkBalanced")) {
            *trt = QD_TREATMENT_LINK_BALANCED;
            return 0;
        }
        return "Invalid value for 'distribution'";
    }

    *trt = QD_TREATMENT_LINK_BALANCED;
    return 0;
}


const char *qdra_link_route_direction_CT(qd_parsed_field_t *field, qd_direction_t *dir)
{
    if (field) {
        qd_iterator_t *iter = qd_parse_raw(field);
        if (qd_iterator_equal(iter, (unsigned char*) "in")) {
            *dir = QD_INCOMING;
            return 0;
        } else if (qd_iterator_equal(iter, (unsigned char*) "out")) {
            *dir = QD_OUTGOING;
            return 0;
        }
        return "Invalid value for 'direction'";
    }
    return "Missing value for 'direction'";
}


static qdr_link_route_t *qdr_link_route_config_find_by_identity_CT(qdr_core_t *core, qd_iterator_t *identity)
{
    if (!identity)
        return 0;

    qdr_link_route_t *rc = DEQ_HEAD(core->link_routes);
    while (rc) {
        // Convert the passed in identity to a char*
        char id[100];
        snprintf(id, 100, "%"PRId64, rc->identity);
        if (qd_iterator_equal(identity, (const unsigned char*) id))
            break;
        rc = DEQ_NEXT(rc);
    }

    return rc;

}


static qdr_link_route_t *qdr_link_route_config_find_by_name_CT(qdr_core_t *core, qd_iterator_t *name)
{
    if (!name)
        return 0;

    qdr_link_route_t *rc = DEQ_HEAD(core->link_routes);
    while (rc) { // Sometimes the name can be null
        if (rc->name && qd_iterator_equal(name, (const unsigned char*) rc->name))
            break;
        rc = DEQ_NEXT(rc);
    }

    return rc;
}


void qdra_config_link_route_delete_CT(qdr_core_t    *core,
                                      qdr_query_t   *query,
                                      qd_iterator_t *name,
                                      qd_iterator_t *identity)
{
    qdr_link_route_t *lr = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing DELETE of %s: %s", CONFIG_LINKROUTE_TYPE, query->status.description);
    }
    else {
        if (identity)
            lr = qdr_link_route_config_find_by_identity_CT(core, identity);
        else if (name)
            lr = qdr_link_route_config_find_by_name_CT(core, name);

        if (lr) {
            qdr_route_del_link_route_CT(core, lr);
            query->status = QD_AMQP_NO_CONTENT;
        } else
            query->status = QD_AMQP_NOT_FOUND;
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

void qdra_config_link_route_create_CT(qdr_core_t        *core,
                                      qd_iterator_t     *name,
                                      qdr_query_t       *query,
                                      qd_parsed_field_t *in_body)
{
    char *pattern = NULL;

    while (true) {
        //
        // Ensure there isn't a duplicate name and that the body is a map
        //
        qdr_link_route_t *lr = 0;

        if (name) {
            qd_iterator_view_t iter_view = qd_iterator_get_view(name);
            qd_iterator_annotate_prefix(name, CONFIG_LINK_ROUTE_PREFIX);
            qd_iterator_reset_view(name, ITER_VIEW_ADDRESS_HASH);
            qd_hash_retrieve(core->addr_lr_al_hash, name, (void**) &lr);
            qd_iterator_reset_view(name, iter_view);
        }


        if (!!lr) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Name conflicts with an existing entity";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_LINKROUTE_TYPE, query->status.description);
            break;
        }

        if (!qd_parse_is_map(in_body)) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Body of request must be a map";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_LINKROUTE_TYPE, query->status.description);
            break;
        }


        //
        // Extract the fields from the request
        //
        qd_parsed_field_t *prefix_field     = qd_parse_value_by_key(in_body, qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_PREFIX]);
        qd_parsed_field_t *pattern_field    = qd_parse_value_by_key(in_body, qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_PATTERN]);
        qd_parsed_field_t *add_prefix_field    = qd_parse_value_by_key(in_body, qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_ADD_EXTERNAL_PREFIX]);
        qd_parsed_field_t *del_prefix_field    = qd_parse_value_by_key(in_body, qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_DEL_EXTERNAL_PREFIX]);
        qd_parsed_field_t *distrib_field    = qd_parse_value_by_key(in_body, qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_DISTRIBUTION]);
        qd_parsed_field_t *connection_field = qd_parse_value_by_key(in_body, qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_CONNECTION]);
        qd_parsed_field_t *container_field  = qd_parse_value_by_key(in_body, qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_CONTAINER_ID]);
        qd_parsed_field_t *dir_field        = qd_parse_value_by_key(in_body, qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_DIRECTION]);
        if (! dir_field) {
            dir_field        = qd_parse_value_by_key(in_body, qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_DIR]);
            if (dir_field)
                qd_log(core->agent_log, QD_LOG_WARNING, "The 'dir' attribute of linkRoute has been deprecated. Use 'direction' instead");
        }


        //
        // Both connection and containerId cannot be specified because both can represent different connections. Only one those
        // can be specified.
        //
        if (connection_field && container_field) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Both connection and containerId cannot be specified. Specify only one";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_LINKROUTE_TYPE, query->status.description);
            break;
        }

        //
        // The direction field is mandatory.
        // Either a prefix or a pattern field is mandatory.  However prefix and pattern
        // are mutually exclusive. Fail if either both or none are given.
        //
        const char *msg = NULL;
        if (!dir_field) {
            msg = "No 'direction' attribute provided - it is mandatory";
        } else if (!prefix_field && !pattern_field) {
            msg = "Either a 'prefix' or 'pattern' attribute must be provided";
        } else if (prefix_field && pattern_field) {
            msg = "Cannot specify both a 'prefix' and a 'pattern' attribute";
        }
        if (msg) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = msg;
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_LINKROUTE_TYPE, query->status.description);
            break;
        }

        // validate the pattern/prefix, add "/#" if prefix
        pattern = qdra_config_address_validate_pattern_CT((prefix_field) ? prefix_field : pattern_field,
                                                          !!prefix_field,
                                                          &msg);
        if (!pattern) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = msg;
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_LINKROUTE_TYPE, query->status.description);
            break;
        }

        qd_direction_t dir;
        const char *error = qdra_link_route_direction_CT(dir_field, &dir);
        if (error) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = error;
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_LINKROUTE_TYPE, query->status.description);
            break;
        }

        qd_address_treatment_t trt;
        error = qdra_link_route_treatment_CT(distrib_field, &trt);
        if (error) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = error;
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_LINKROUTE_TYPE, query->status.description);
            break;
        }

        //
        // The request is good.  Create the entity.
        //

        lr = qdr_route_add_link_route_CT(core, name, pattern, !!prefix_field,
                                         add_prefix_field, del_prefix_field,
                                         container_field, connection_field,
                                         trt, dir);
        //
        // Compose the result map for the response.
        //
        if (query->body) {
            qd_compose_start_map(query->body);
            for (int col = 0; col < QDR_CONFIG_LINK_ROUTE_COLUMN_COUNT; col++)
                qdr_config_link_route_insert_column_CT(lr, col, query->body, true);
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
        //
        // If there was an error in processing the create, insert a NULL value into the body.
        //
        if (query->status.status / 100 > 2)
            qd_compose_insert_null(query->body);
        qdr_agent_enqueue_response_CT(core, query);
    } else {
        if (query->status.status / 100 > 2)
            qd_log(core->log, QD_LOG_ERROR, "Error configuring linkRoute: %s", query->status.description);
        qdr_query_free(query);
    }
    free(pattern);
}

void qdra_config_link_route_get_CT(qdr_core_t    *core,
                                   qd_iterator_t *name,
                                   qd_iterator_t *identity,
                                   qdr_query_t   *query,
                                   const char    *qdr_config_link_route_columns[])
{
    qdr_link_route_t *lr = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", CONFIG_LINKROUTE_TYPE, query->status.description);
    }
    else {
        if (identity) //If there is identity, ignore the name
            lr = qdr_link_route_config_find_by_identity_CT(core, identity);
        else if (name)
            lr = qdr_link_route_config_find_by_name_CT(core, name);

        if (lr == 0) {
            // Send back a 404
            query->status = QD_AMQP_NOT_FOUND;
        }
        else {
            //
            // Write the columns of the linkRoute entity into the response body.
            //
            qdr_manage_write_config_link_route_map_CT(core, lr, query->body, qdr_config_link_route_columns);
            query->status = QD_AMQP_OK;
        }
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);

}
