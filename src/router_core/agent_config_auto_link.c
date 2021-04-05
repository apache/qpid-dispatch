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

#include "agent_config_auto_link.h"

#include "route_control.h"

#include "qpid/dispatch/ctools.h"

#include <inttypes.h>
#include <stdio.h>

#define QDR_CONFIG_AUTO_LINK_NAME          0
#define QDR_CONFIG_AUTO_LINK_IDENTITY      1
#define QDR_CONFIG_AUTO_LINK_TYPE          2
#define QDR_CONFIG_AUTO_LINK_ADDRESS       3
#define QDR_CONFIG_AUTO_LINK_ADDR          4
#define QDR_CONFIG_AUTO_LINK_DIRECTION     5
#define QDR_CONFIG_AUTO_LINK_DIR           6
#define QDR_CONFIG_AUTO_LINK_PHASE         7
#define QDR_CONFIG_AUTO_LINK_CONNECTION    8
#define QDR_CONFIG_AUTO_LINK_CONTAINER_ID  9
#define QDR_CONFIG_AUTO_LINK_EXT_ADDRESS   10
#define QDR_CONFIG_AUTO_LINK_EXT_ADDR      11
#define QDR_CONFIG_AUTO_LINK_LINK_REF      12
#define QDR_CONFIG_AUTO_LINK_OPER_STATUS   13
#define QDR_CONFIG_AUTO_LINK_LAST_ERROR    14
#define QDR_CONFIG_AUTO_LINK_FALLBACK      15

const char *qdr_config_auto_link_columns[] =
    {"name",
     "identity",
     "type",
     "address",
     "addr",
     "direction",
     "dir",
     "phase",
     "connection",
     "containerId",
     "externalAddress",
     "externalAddr",
     "linkRef",
     "operStatus",
     "lastError",
     "fallback",
     0};

const char *CONFIG_AUTOLINK_TYPE = "org.apache.qpid.dispatch.router.config.autoLink";
const char CONFIG_AUTO_LINK_PREFIX = 'A';

static void qdr_config_auto_link_insert_column_CT(qdr_auto_link_t *al, int col, qd_composed_field_t *body, bool as_map)
{
    const char *text = 0;
    const char *key;
    char id_str[100];

    if (!al)
        return;

    if (as_map)
        qd_compose_insert_string(body, qdr_config_auto_link_columns[col]);

    switch(col) {
    case QDR_CONFIG_AUTO_LINK_NAME:
        if (al->name)
            qd_compose_insert_string(body, al->name);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_IDENTITY:
        snprintf(id_str, 100, "%"PRId64, al->identity);
        qd_compose_insert_string(body, id_str);
        break;

    case QDR_CONFIG_AUTO_LINK_TYPE:
        qd_compose_insert_string(body, CONFIG_AUTOLINK_TYPE);
        break;

    case QDR_CONFIG_AUTO_LINK_ADDR:
    case QDR_CONFIG_AUTO_LINK_ADDRESS:
        key = (const char*) qd_hash_key_by_handle(al->addr->hash_handle);
        if (key && key[0] == 'M')
            qd_compose_insert_string(body, &key[2]);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_DIR:
    case QDR_CONFIG_AUTO_LINK_DIRECTION:
        text = al->dir == QD_INCOMING ? "in" : "out";
        qd_compose_insert_string(body, text);
        break;

    case QDR_CONFIG_AUTO_LINK_PHASE:
        qd_compose_insert_int(body, al->phase);
        break;

    case QDR_CONFIG_AUTO_LINK_CONNECTION:
    case QDR_CONFIG_AUTO_LINK_CONTAINER_ID:
        if (al->conn_id) {
            key = (const char*) qd_hash_key_by_handle(al->conn_id->connection_hash_handle);
            if (!key)
                key = (const char*) qd_hash_key_by_handle(al->conn_id->container_hash_handle);

            if (key && key[0] == 'L' && col == QDR_CONFIG_AUTO_LINK_CONNECTION) {
                qd_compose_insert_string(body, &key[1]);
                break;
            }
            if (key && key[0] == 'C' && col == QDR_CONFIG_AUTO_LINK_CONTAINER_ID) {
                qd_compose_insert_string(body, &key[1]);
                break;
            }
        }
        qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_EXT_ADDR:
    case QDR_CONFIG_AUTO_LINK_EXT_ADDRESS:
        if (al->external_addr)
            qd_compose_insert_string(body, al->external_addr);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_LINK_REF:
        if (al->link) {
            snprintf(id_str, 100, "%"PRId64, al->link->identity);
            qd_compose_insert_string(body, id_str);
        } else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_OPER_STATUS:
        switch (al->state) {
        case QDR_AUTO_LINK_STATE_INACTIVE:  text = "inactive";  break;
        case QDR_AUTO_LINK_STATE_ATTACHING: text = "attaching"; break;
        case QDR_AUTO_LINK_STATE_FAILED:    text = "failed";    break;
        case QDR_AUTO_LINK_STATE_ACTIVE:    text = "active";    break;
        case QDR_AUTO_LINK_STATE_QUIESCING: text = "quiescing"; break;
        case QDR_AUTO_LINK_STATE_IDLE:      text = "idle";      break;
        }

        if (text)
            qd_compose_insert_string(body, text);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_LAST_ERROR:
        if (al->last_error)
            qd_compose_insert_string(body, al->last_error);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_AUTO_LINK_FALLBACK:
        qd_compose_insert_bool(body, al->fallback);
        break;
    }
}


static void qdr_agent_write_config_auto_link_CT(qdr_query_t *query,  qdr_auto_link_t *al)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    if (al) {
        int i = 0;
        while (query->columns[i] >= 0) {
            qdr_config_auto_link_insert_column_CT(al, query->columns[i], body, false);
            i++;
        }
    }
    qd_compose_end_list(body);
}


static void qdr_manage_advance_config_auto_link_CT(qdr_query_t *query, qdr_auto_link_t *al)
{
    if (al) {
        query->next_offset++;
        al = DEQ_NEXT(al);
        query->more = !!al;
    }
    else {
        query->more = false;
    }
}


void qdra_config_auto_link_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->auto_links)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_auto_link_t *al = DEQ_HEAD(core->auto_links);
    for (int i = 0; i < offset && al; i++)
        al = DEQ_NEXT(al);
    assert(al);

    if (al) {

        //
        // Write the columns of the object into the response body.
        //
        qdr_agent_write_config_auto_link_CT(query, al);

        //
        // Advance to the next auto_link
        //
        query->next_offset = offset;
        qdr_manage_advance_config_auto_link_CT(query, al);
    }
    else {
        query->more = false;
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_config_auto_link_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_auto_link_t *al = 0;

    if (query->next_offset < DEQ_SIZE(core->auto_links)) {
        al = DEQ_HEAD(core->auto_links);
        for (int i = 0; i < query->next_offset && al; i++)
            al = DEQ_NEXT(al);
    }

    if (al) {
        //
        // Write the columns of the addr entity into the response body.
        //
        qdr_agent_write_config_auto_link_CT(query, al);

        //
        // Advance to the next object
        //
        qdr_manage_advance_config_auto_link_CT(query, al);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static const char *qdra_auto_link_direction_CT(qd_parsed_field_t *field, qd_direction_t *dir)
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


static qdr_auto_link_t *qdr_auto_link_config_find_by_identity_CT(qdr_core_t *core, qd_iterator_t *identity)
{
    if (!identity)
        return 0;

    qdr_auto_link_t *rc = DEQ_HEAD(core->auto_links);
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


static qdr_auto_link_t *qdr_auto_link_config_find_by_name_CT(qdr_core_t *core, qd_iterator_t *name)
{
    if (!name)
        return 0;

    qdr_auto_link_t *rc = DEQ_HEAD(core->auto_links);
    while (rc) { // Sometimes the name can be null
        if (rc->name && qd_iterator_equal(name, (const unsigned char*) rc->name))
            break;
        rc = DEQ_NEXT(rc);
    }

    return rc;
}


void qdra_config_auto_link_delete_CT(qdr_core_t    *core,
                                     qdr_query_t   *query,
                                     qd_iterator_t *name,
                                     qd_iterator_t *identity)
{
    qdr_auto_link_t *al = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing DELETE of %s: %s", CONFIG_AUTOLINK_TYPE, query->status.description);
    }
    else {
        if (identity)
            al = qdr_auto_link_config_find_by_identity_CT(core, identity);
        else if (name)
            al = qdr_auto_link_config_find_by_name_CT(core, name);

        if (al) {
            qdr_route_del_auto_link_CT(core, al);
            query->status = QD_AMQP_NO_CONTENT;
        } else
            query->status = QD_AMQP_NOT_FOUND;
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

void qdra_config_auto_link_create_CT(qdr_core_t        *core,
                                     qd_iterator_t     *name,
                                     qdr_query_t       *query,
                                     qd_parsed_field_t *in_body)
{
    while (true) {
        //
        // Ensure there isn't a duplicate name and that the body is a map
        //
        qdr_auto_link_t *al = 0;

        if (name) {
            qd_iterator_view_t iter_view = qd_iterator_get_view(name);
            qd_iterator_annotate_prefix(name, CONFIG_AUTO_LINK_PREFIX);
            qd_iterator_reset_view(name, ITER_VIEW_ADDRESS_HASH);
            qd_hash_retrieve(core->addr_lr_al_hash, name, (void**) &al);
            qd_iterator_reset_view(name, iter_view);
        }


        if (!!al) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Name conflicts with an existing entity";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_AUTOLINK_TYPE, query->status.description);
            break;
        }

        if (!qd_parse_is_map(in_body)) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Body of request must be a map";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_AUTOLINK_TYPE, query->status.description);
            break;
        }

        //
        // Extract the fields from the request
        //
        qd_parsed_field_t *addr_field       = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_ADDRESS]);
        if (!addr_field) {
            addr_field       = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_ADDR]);
            if (addr_field)
                qd_log(core->agent_log, QD_LOG_WARNING, "The 'addr' attribute of autoLink has been deprecated. Use 'address' instead");
        }
        qd_parsed_field_t *dir_field        = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_DIRECTION]);
        if (! dir_field) {
            dir_field        = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_DIR]);
            if (dir_field)
                qd_log(core->agent_log, QD_LOG_WARNING, "The 'dir' attribute of autoLink has been deprecated. Use 'direction' instead");
        }
        qd_parsed_field_t *phase_field      = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_PHASE]);
        qd_parsed_field_t *connection_field = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_CONNECTION]);
        qd_parsed_field_t *container_field  = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_CONTAINER_ID]);
        qd_parsed_field_t *fallback_field   = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_FALLBACK]);

        qd_parsed_field_t *external_addr    = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_EXT_ADDRESS]);
        if (!external_addr) {
            external_addr    = qd_parse_value_by_key(in_body, qdr_config_auto_link_columns[QDR_CONFIG_AUTO_LINK_EXT_ADDR]);
            if (external_addr)
                qd_log(core->agent_log, QD_LOG_WARNING, "The 'externalAddr' attribute of autoLink has been deprecated. Use 'externalAddress' instead");
        }


        if (connection_field && container_field) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "Both connection and containerId cannot be specified. Specify only one";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_AUTOLINK_TYPE, query->status.description);
            break;
        }

        bool fallback = !!fallback_field ? qd_parse_as_bool(fallback_field) : false;

        //
        // Addr and direction fields are mandatory.  Fail if they're not both here.
        //
        if (!addr_field || !dir_field) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "address and direction fields are mandatory";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_AUTOLINK_TYPE, query->status.description);
            break;
        }

        qd_direction_t dir;
        const char *error = qdra_auto_link_direction_CT(dir_field, &dir);
        if (error) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = error;
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_AUTOLINK_TYPE, query->status.description);
            break;
        }

        //
        // Use the specified phase if present.  Otherwise default based on the direction:
        // Phase 0 for outgoing links and phase 1 for incoming links.
        //
        long phase = phase_field ? qd_parse_as_long(phase_field) : ((dir == QD_OUTGOING || !!fallback) ? 0 : 1);

        //
        // Validate the phase
        //
        if (phase < 0 || phase > 9) {
            query->status = QD_AMQP_BAD_REQUEST;
            query->status.description = "autoLink phase must be between 0 and 9";
            qd_log(core->agent_log, QD_LOG_ERROR, "Error performing CREATE of %s: %s", CONFIG_AUTOLINK_TYPE, query->status.description);
            break;
        }

        //
        // The request is good.  Create the entity.
        //
        al = qdr_route_add_auto_link_CT(core, name, addr_field, dir, phase, container_field, connection_field, external_addr, fallback);

        //
        // Compose the result map for the response.
        //
        if (query->body) {
            qd_compose_start_map(query->body);
            for (int col = 0; col < QDR_CONFIG_AUTO_LINK_COLUMN_COUNT; col++)
                qdr_config_auto_link_insert_column_CT(al, col, query->body, true);
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
}


static void qdr_manage_write_config_auto_link_map_CT(qdr_core_t          *core,
                                                     qdr_auto_link_t     *al,
                                                     qd_composed_field_t *body,
                                                     const char          *qdr_config_auto_link_columns[])
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_CONFIG_AUTO_LINK_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_config_auto_link_columns[i]);
        qdr_config_auto_link_insert_column_CT(al, i, body, false);
    }

    qd_compose_end_map(body);
}


void qdra_config_auto_link_get_CT(qdr_core_t    *core,
                                  qd_iterator_t *name,
                                  qd_iterator_t *identity,
                                  qdr_query_t   *query,
                                  const char    *qdr_config_auto_link_columns[])
{
    qdr_auto_link_t *al = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", CONFIG_AUTOLINK_TYPE, query->status.description);
    }
    else {
        if (identity) //If there is identity, ignore the name
            al = qdr_auto_link_config_find_by_identity_CT(core, identity);
        else if (name)
            al = qdr_auto_link_config_find_by_name_CT(core, name);

        if (al == 0) {
            // Send back a 404
            query->status = QD_AMQP_NOT_FOUND;
        }
        else {
            //
            // Write the columns of the address entity into the response body.
            //
            qdr_manage_write_config_auto_link_map_CT(core, al, query->body, qdr_config_auto_link_columns);
            query->status = QD_AMQP_OK;
        }
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);

}
