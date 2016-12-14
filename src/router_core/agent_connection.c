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

#include <qpid/dispatch/ctools.h>
#include "agent_connection.h"
#include <inttypes.h>
#include <stdio.h>

#define QDR_CONNECTION_IDENTITY         0
#define QDR_CONNECTION_HOST             1
#define QDR_CONNECTION_ROLE             2
#define QDR_CONNECTION_DIR              3
#define QDR_CONNECTION_CONTAINER_ID     4
#define QDR_CONNECTION_SASL_MECHANISMS  5
#define QDR_CONNECTION_IS_AUTHENTICATED 6
#define QDR_CONNECTION_USER             7
#define QDR_CONNECTION_IS_ENCRYPTED     8
#define QDR_CONNECTION_SSLPROTO         9
#define QDR_CONNECTION_SSLCIPHER        10
#define QDR_CONNECTION_PROPERTIES       11
#define QDR_CONNECTION_SSLSSF           12

const char *qdr_connection_columns[] =
    {"identity",
     "host",
     "role",
     "dir",
     "container",
     "sasl",
     "isAuthenticated",
     "user",
     "isEncrypted",
     "sslProto",
     "sslCipher",
     "properties",
     "sslSsf",
     0};

const char *CONFIG_CONNECTION_TYPE = "org.apache.qpid.dispatch.connection";


static void qd_get_next_pn_data(pn_data_t **data, const char **d, int *d1)
{
    if (pn_data_next(*data)) {
        switch (pn_data_type(*data)) {
            case PN_STRING:
                *d = pn_data_get_string(*data).start;
                break;
            case PN_SYMBOL:
                *d = pn_data_get_symbol(*data).start;
                break;
            case PN_INT:
                *d1 = pn_data_get_int(*data);
                break;
            default:
                break;
        }
    }
    }


static void qdr_connection_insert_column_CT(qdr_connection_t *conn, int col, qd_composed_field_t *body, bool as_map)
{
    char id_str[100];

    if (as_map)
        qd_compose_insert_string(body, qdr_connection_columns[col]);

    switch(col) {
    case QDR_CONNECTION_IDENTITY: {
        snprintf(id_str, 100, "%"PRId64, conn->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_CONNECTION_HOST:
        qd_compose_insert_string(body, conn->connection_info->host);
        break;

    case QDR_CONNECTION_ROLE:
        qd_compose_insert_string(body, conn->connection_info->role);
        break;

    case QDR_CONNECTION_DIR:
        qd_compose_insert_string(body, conn->connection_info->dir);
        break;

    case QDR_CONNECTION_CONTAINER_ID:
        if (conn->connection_info->container)
            qd_compose_insert_string(body, conn->connection_info->container);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONNECTION_SASL_MECHANISMS:
        qd_compose_insert_string(body, conn->connection_info->sasl_mechanisms);
        break;

    case QDR_CONNECTION_IS_AUTHENTICATED:
        qd_compose_insert_bool(body, conn->connection_info->is_authenticated);
        break;

    case QDR_CONNECTION_USER:
        qd_compose_insert_string(body, conn->connection_info->user);
        break;

    case QDR_CONNECTION_IS_ENCRYPTED:
        qd_compose_insert_bool(body, conn->connection_info->is_encrypted);
        break;

    case QDR_CONNECTION_SSLPROTO:
        if (conn->connection_info->ssl_proto[0] != '\0')
            qd_compose_insert_string(body, conn->connection_info->ssl_proto);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONNECTION_SSLCIPHER:
        if (conn->connection_info->ssl_cipher[0] != '\0')
            qd_compose_insert_string(body, conn->connection_info->ssl_cipher);
        else
            qd_compose_insert_null(body);
        break;
    case QDR_CONNECTION_SSLSSF:
        qd_compose_insert_long(body, conn->connection_info->ssl_ssf);
        break;
    case QDR_CONNECTION_PROPERTIES: {
        pn_data_t *data = conn->connection_info->connection_properties;
        qd_compose_start_map(body);
        if (data) {
            pn_data_next(data);
            size_t count = pn_data_get_map(data);
            pn_data_enter(data);

            if (count > 0) {

                for (size_t i = 0; i < count/2; i++) {
                    const char *key   = 0;
                    // We are assuming for now that all keys are strings
                    qd_get_next_pn_data(&data, &key, 0);

                    const char *value_string = 0;
                    int value_int = 0;
                    // We are assuming for now that all values are either strings or integers
                    qd_get_next_pn_data(&data, &value_string, &value_int);

                    qd_compose_insert_string(body, key);

                    if (value_string)
                        qd_compose_insert_string(body, value_string);
                    else if (value_int)
                        qd_compose_insert_int(body, value_int);

                }
            }

            pn_data_exit(data);

        }
        qd_compose_end_map(body);
    }
    break;
    }
}


static void qdr_agent_write_connection_CT(qdr_query_t *query,  qdr_connection_t *conn)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    while (query->columns[i] >= 0) {
        qdr_connection_insert_column_CT(conn, query->columns[i], body, false);
        i++;
    }
    qd_compose_end_list(body);
}


static void qdr_manage_advance_connection_CT(qdr_query_t *query, qdr_connection_t *conn)
{
    query->next_offset++;
    conn = DEQ_NEXT(conn);
    query->more = !!conn;
}


void qdra_connection_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->open_connections)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_connection_t *conn = DEQ_HEAD(core->open_connections);
    for (int i = 0; i < offset && conn; i++)
        conn = DEQ_NEXT(conn);
    assert(conn);

    //
    // Write the columns of the object into the response body.
    //
    qdr_agent_write_connection_CT(query, conn);

    //
    // Advance to the next connection
    //
    query->next_offset = offset;
    qdr_manage_advance_connection_CT(query, conn);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

void qdra_connection_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_connection_t *conn = 0;

    if (query->next_offset < DEQ_SIZE(core->open_connections)) {
        conn = DEQ_HEAD(core->open_connections);
        for (int i = 0; i < query->next_offset && conn; i++)
            conn = DEQ_NEXT(conn);
    }

    if (conn) {
        //
        // Write the columns of the connection entity into the response body.
        //
        qdr_agent_write_connection_CT(query, conn);

        //
        // Advance to the next object
        //
        qdr_manage_advance_connection_CT(query, conn);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

static void qdr_manage_write_connection_map_CT(qdr_core_t          *core,
                                               qdr_connection_t    *conn,
                                               qd_composed_field_t *body,
                                               const char          *qdr_connection_columns[])
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_CONNECTION_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_connection_columns[i]);
        qdr_connection_insert_column_CT(conn, i, body, false);
    }

    qd_compose_end_map(body);
}


static qdr_connection_t *qdr_connection_find_by_identity_CT(qdr_core_t *core, qd_iterator_t *identity)
{
    if (!identity)
        return 0;

    qdr_connection_t *conn = DEQ_HEAD(core->open_connections);
    while (conn) {
        // Convert the passed in identity to a char*
        char id[100];
        snprintf(id, 100, "%"PRId64, conn->identity);
        if (qd_iterator_equal(identity, (const unsigned char*) id))
            break;
        conn = DEQ_NEXT(conn);
    }

    return conn;

}


void qdra_connection_get_CT(qdr_core_t          *core,
                            qd_iterator_t *name,
                            qd_iterator_t *identity,
                            qdr_query_t         *query,
                            const char          *qdr_connection_columns[])
{
    qdr_connection_t *conn = 0;

    if (!identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "Name not supported. Identity required";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", CONFIG_CONNECTION_TYPE, query->status.description);
    }
    else {
        conn = qdr_connection_find_by_identity_CT(core, identity);

        if (conn == 0) {
            // Send back a 404
            query->status = QD_AMQP_NOT_FOUND;
        }
        else {
            //
            // Write the columns of the connection entity into the response body.
            //
            qdr_manage_write_connection_map_CT(core, conn, query->body, qdr_connection_columns);
            query->status = QD_AMQP_OK;
        }
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}
