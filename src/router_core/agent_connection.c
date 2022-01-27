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

#include "agent_connection.h"

#include "qpid/dispatch/ctools.h"

#include <inttypes.h>
#include <stdio.h>

#define QDR_CONNECTION_NAME                   0
#define QDR_CONNECTION_IDENTITY               1
#define QDR_CONNECTION_HOST                   2
#define QDR_CONNECTION_ROLE                   3
#define QDR_CONNECTION_PROTOCOL               4
#define QDR_CONNECTION_DIR                    5
#define QDR_CONNECTION_CONTAINER_ID           6
#define QDR_CONNECTION_SASL_MECHANISMS        7
#define QDR_CONNECTION_IS_AUTHENTICATED       8
#define QDR_CONNECTION_USER                   9
#define QDR_CONNECTION_IS_ENCRYPTED          10
#define QDR_CONNECTION_SSLPROTO              11
#define QDR_CONNECTION_SSLCIPHER             12
#define QDR_CONNECTION_PROPERTIES            13
#define QDR_CONNECTION_SSLSSF                14
#define QDR_CONNECTION_TENANT                15
#define QDR_CONNECTION_TYPE                  16
#define QDR_CONNECTION_SSL                   17
#define QDR_CONNECTION_OPENED                18
#define QDR_CONNECTION_ACTIVE                19
#define QDR_CONNECTION_ADMIN_STATUS          20
#define QDR_CONNECTION_OPER_STATUS           21
#define QDR_CONNECTION_UPTIME_SECONDS        22
#define QDR_CONNECTION_LAST_DLV_SECONDS      23
#define QDR_CONNECTION_ENABLE_PROTOCOL_TRACE 24


const char * const QDR_CONNECTION_DIR_IN  = "in";
const char * const QDR_CONNECTION_DIR_OUT = "out";

const char * QDR_CONNECTION_ADMIN_STATUS_DELETED = "deleted";
const char * QDR_CONNECTION_ADMIN_STATUS_ENABLED = "enabled";

const char * QDR_CONNECTION_OPER_STATUS_UP      = "up";
const char * QDR_CONNECTION_OPER_STATUS_CLOSING = "closing";


const char *qdr_connection_roles[] =
    {"normal",
     "inter-router",
     "route-container",
     "edge",
     0};

const char *qdr_connection_columns[] =
    {"name",
     "identity",
     "host",
     "role",
     "protocol",
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
     "tenant",
     "type",
     "ssl",
     "opened",
     "active",
     "adminStatus",
     "operStatus",
     "uptimeSeconds",
     "lastDlvSeconds",
     "enableProtocolTrace",
     0};

const char *CONNECTION_TYPE = "org.apache.qpid.dispatch.connection";

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
            case PN_LONG:
                *d1 = pn_data_get_long(*data);
                break;
            default:
                break;
        }
    }
    }


static void qdr_connection_insert_column_CT(qdr_core_t *core, qdr_connection_t *conn, int col, qd_composed_field_t *body, bool as_map)
{
    char id_str[100];
    const char *text = 0;

    if (!conn)
        return;

    if (as_map)
        qd_compose_insert_string(body, qdr_connection_columns[col]);

    switch(col) {
    case QDR_CONNECTION_NAME:
        qd_compose_insert_string2(body, "connection/", conn->connection_info->host);
        break;

    case QDR_CONNECTION_IDENTITY: {
        snprintf(id_str, 100, "%"PRId64, conn->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_CONNECTION_HOST:
        qd_compose_insert_string(body, conn->connection_info->host);
        break;

    case QDR_CONNECTION_ROLE:
        qd_compose_insert_string(body, qdr_connection_roles[conn->connection_info->role]);
        break;

    case QDR_CONNECTION_PROTOCOL:
        qd_compose_insert_string(body, conn->protocol_adaptor->name);
        break;

    case QDR_CONNECTION_DIR:
        if (conn->connection_info->dir == QD_INCOMING)
            qd_compose_insert_string(body, QDR_CONNECTION_DIR_IN);
        else
            qd_compose_insert_string(body, QDR_CONNECTION_DIR_OUT);
        break;

    case QDR_CONNECTION_CONTAINER_ID:
        if (conn->connection_info->container)
            qd_compose_insert_string(body, conn->connection_info->container);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONNECTION_SASL_MECHANISMS:
        if (conn->connection_info->sasl_mechanisms) 
            qd_compose_insert_string(body, conn->connection_info->sasl_mechanisms);
	else
	    qd_compose_insert_null(body);
        break;

    case QDR_CONNECTION_IS_AUTHENTICATED:
        qd_compose_insert_bool(body, conn->connection_info->is_authenticated);
        break;

    case QDR_CONNECTION_USER:
        if (conn->connection_info->user)
            qd_compose_insert_string(body, conn->connection_info->user);
        else
            qd_compose_insert_null(body);
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

    case QDR_CONNECTION_TENANT:
        if (conn->tenant_space)
            qd_compose_insert_string(body, conn->tenant_space);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONNECTION_TYPE:
        qd_compose_insert_string(body, CONNECTION_TYPE);
        break;

    case QDR_CONNECTION_SSL:
        qd_compose_insert_bool(body, conn->connection_info->ssl);
        break;

    case QDR_CONNECTION_OPENED:
        qd_compose_insert_bool(body, conn->connection_info->opened);
        break;

    case QDR_CONNECTION_ENABLE_PROTOCOL_TRACE:
        qd_compose_insert_bool(body, conn->enable_protocol_trace);
        break;

    case QDR_CONNECTION_ACTIVE:
        if (conn->role == QDR_ROLE_EDGE_CONNECTION) {
            if (core->router_mode == QD_ROUTER_MODE_INTERIOR) {
                qd_compose_insert_bool(body, true);
            }
            else if (core->router_mode  == QD_ROUTER_MODE_EDGE){
                if (core->active_edge_connection == conn)
                    qd_compose_insert_bool(body, true);
                else
                    qd_compose_insert_bool(body, false);
            }
        }
        else {
            qd_compose_insert_bool(body, true);
        }
        break;

    case QDR_CONNECTION_ADMIN_STATUS:
        text = conn->closed ? QDR_CONNECTION_ADMIN_STATUS_DELETED : QDR_CONNECTION_ADMIN_STATUS_ENABLED;
        qd_compose_insert_string(body, text);
        break;

    case QDR_CONNECTION_OPER_STATUS:
        text = conn->closed ? QDR_CONNECTION_OPER_STATUS_CLOSING : QDR_CONNECTION_OPER_STATUS_UP;
        qd_compose_insert_string(body, text);
        break;

    case QDR_CONNECTION_UPTIME_SECONDS:
        qd_compose_insert_uint(body, qdr_core_uptime_ticks(core) - conn->conn_uptime);
        break;

    case QDR_CONNECTION_LAST_DLV_SECONDS:
        if (conn->last_delivery_time==0)
            qd_compose_insert_null(body);
        else
            qd_compose_insert_uint(body, qdr_core_uptime_ticks(core) - conn->last_delivery_time);
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

                    // We now have the key and the value. Do not insert the key or the value if key is empty
                    if (key) {
                        qd_compose_insert_string(body, key);
                        if (value_string)
                            qd_compose_insert_string(body, value_string);
                        else if (value_int)
                            qd_compose_insert_int(body, value_int);
                    }

                }
            }

            pn_data_exit(data);

        }
        qd_compose_end_map(body);
    }
    break;
    }
}


static void qdr_agent_write_connection_CT(qdr_core_t *core, qdr_query_t *query,  qdr_connection_t *conn)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);

    if (conn) {
        int i = 0;
        while (query->columns[i] >= 0) {
            qdr_connection_insert_column_CT(core, conn, query->columns[i], body, false);
            i++;
        }
    }
    qd_compose_end_list(body);
}


static void qdr_manage_advance_connection_CT(qdr_query_t *query, qdr_connection_t *conn)
{
    if (conn) {
        query->next_offset++;
        conn = DEQ_NEXT(conn);
        query->more = !!conn;
    }
    else {
        query->more = false;
    }
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

    if (conn) {
        //
        // Write the columns of the object into the response body.
        //
        qdr_agent_write_connection_CT(core, query, conn);

        //
        // Advance to the next connection
        //
        query->next_offset = offset;
        qdr_manage_advance_connection_CT(query, conn);
    }
    else {
        query->more = false;
    }

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
        qdr_agent_write_connection_CT(core, query, conn);

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
        qdr_connection_insert_column_CT(core, conn, i, body, false);
    }

    qd_compose_end_map(body);
}

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

void qdra_connection_get_CT(qdr_core_t    *core,
                            qd_iterator_t *name,
                            qd_iterator_t *identity,
                            qdr_query_t   *query,
                            const char    *qdr_connection_columns[])
{
    qdr_connection_t *conn = 0;

    if (!identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "Name not supported. Identity required";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s", CONNECTION_TYPE, query->status.description);
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


static void qdra_connection_set_bad_request(qdr_query_t *query)
{
    query->status = QD_AMQP_BAD_REQUEST;
    qd_compose_start_map(query->body);
    qd_compose_end_map(query->body);
}


static void qdra_connection_update_set_status(qdr_core_t *core, qdr_query_t *query, qdr_connection_t *conn, qd_parsed_field_t *admin_state)
{
    if (conn) {
        qd_iterator_t *admin_status_iter = qd_parse_raw(admin_state);

        if (qd_iterator_equal(admin_status_iter, (unsigned char*) QDR_CONNECTION_ADMIN_STATUS_DELETED)) {
            // This connection has been force-closed.
            // Inter-router and edge connections may not be force-closed
            if (conn->role != QDR_ROLE_INTER_ROUTER && conn->role != QDR_ROLE_EDGE_CONNECTION) {
                qdr_close_connection_CT(core, conn);
                qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"] Connection force-closed by request from connection [C%"PRIu64"]", conn->identity, query->in_conn);
                query->status = QD_AMQP_OK;
                qdr_manage_write_connection_map_CT(core, conn, query->body, qdr_connection_columns);
            }
            else {
                //
                // You are trying to delete an inter-router connection and that is always forbidden, no matter what
                // policy rights you have.
                //
                query->status = QD_AMQP_FORBIDDEN;
                query->status.description = "You are not allowed to perform this operation.";
                qd_compose_start_map(query->body);
                qd_compose_end_map(query->body);
            }

        }
        else if (qd_iterator_equal(admin_status_iter, (unsigned char*) QDR_CONNECTION_ADMIN_STATUS_ENABLED)) {
            query->status = QD_AMQP_OK;
            qdr_manage_write_connection_map_CT(core, conn, query->body, qdr_connection_columns);
        }
        else {
            qdra_connection_set_bad_request(query);
        }
    }
    else {
        query->status = QD_AMQP_NOT_FOUND;
        qd_compose_start_map(query->body);
        qd_compose_end_map(query->body);
    }
}



void qdra_connection_update_CT(qdr_core_t      *core,
                             qd_iterator_t     *name,
                             qd_iterator_t     *identity,
                             qdr_query_t       *query,
                             qd_parsed_field_t *in_body)
{
    // If the request was successful then the statusCode MUST contain 200 (OK) and the body of the message
    // MUST contain a map containing the actual attributes of the entity updated. These MAY differ from those
    // requested.
    // A map containing attributes that are not applicable for the entity being created, or invalid values for a
    // given attribute, MUST result in a failure response with a statusCode of 400 (Bad Request).
    if (qd_parse_is_map(in_body) && identity) {
        // The absence of an attribute name implies that the entity should retain its already existing value.
        // If the map contains a key-value pair where the value is null then the updated entity should have no value
        // for that attribute, removing any previous value.
        qd_parsed_field_t *admin_state = qd_parse_value_by_key(in_body, qdr_connection_columns[QDR_CONNECTION_ADMIN_STATUS]);

        // Find the connection that the user connected on. This connection must have the correct policy rights which
        // will allow the user on this connection to terminate some other connection.
        qdr_connection_t *user_conn = _find_conn_CT(core, query->in_conn);
        qd_parsed_field_t *trace_field   = qd_parse_value_by_key(in_body, qdr_connection_columns[QDR_CONNECTION_ENABLE_PROTOCOL_TRACE]);


        //
        // The only two fields that can be updated on a connection is the enableProtocolTrace flag and the admin state.
        // If both these fields are not there, this is a bad request
        // For example, this qdmanage is a bad request - qdmanage update --type=connection identity=1
        //
        if (!trace_field && !admin_state) {
            qdra_connection_set_bad_request(query);
            qdr_agent_enqueue_response_CT(core, query);
            return;
        }

        bool enable_protocol_trace = !!trace_field ? qd_parse_as_bool(trace_field) : false;

        qdr_connection_t *conn = qdr_connection_find_by_identity_CT(core, identity);

        if (!conn) {
            //
            // The identity supplied was used to obtain the connection. If the connection was not found,
            // it is possible that the connection went away or the wrong identity was provided.
            // Either way we will have to let the caller know that this is a bad request.
            //
            qdra_connection_set_bad_request(query);
            qdr_agent_enqueue_response_CT(core, query);
            return;
        }

        bool admin_status_bad_or_forbidden = false;

        if (admin_state) {
            if (!user_conn) {
                // This is bad. The user connection (that was requesting that some
                // other connection be dropped) is gone
                query->status.description = "Parent connection no longer exists";
                qdra_connection_set_bad_request(query);
                admin_status_bad_or_forbidden = true;
            }
            else {
                bool allow = user_conn->policy_spec ? user_conn->policy_spec->allowAdminStatusUpdate : true;
                if (!allow) {
                    //
                    // Policy on the connection that is requesting that some other connection be deleted does not allow
                    // for the other connection to be deleted.Set the status to QD_AMQP_FORBIDDEN and just quit.
                    //
                    query->status = QD_AMQP_FORBIDDEN;
                    query->status.description = "You are not allowed to perform this operation.";
                    qd_compose_start_map(query->body);
                    qd_compose_end_map(query->body);
                    admin_status_bad_or_forbidden = true;
                 }
                else {
                    qdra_connection_update_set_status(core, query, conn, admin_state);
                }
            }

            if (admin_status_bad_or_forbidden) {
                //
                // Enqueue the response and return
                //
                qdr_agent_enqueue_response_CT(core, query);
                return;
            }
        }

        if (trace_field) {
            //
            // Trace logging needs to be turned on if enableProtocolTrace is true.
            // Trace logging needs to be turned off if enableProtocolTrace is false.
            //
            if (conn->enable_protocol_trace != enable_protocol_trace) {
                qdr_connection_work_type_t work_type = QDR_CONNECTION_WORK_TRACING_ON;
                conn->enable_protocol_trace = enable_protocol_trace;
                if (!enable_protocol_trace) {
                    work_type = QDR_CONNECTION_WORK_TRACING_OFF;
                }
                qdr_connection_work_t *work = new_qdr_connection_work_t();
                ZERO(work);
                work->work_type = work_type;
                qdr_connection_enqueue_work_CT(core, conn, work);

            }
            query->status = QD_AMQP_OK;
            qdr_manage_write_connection_map_CT(core, conn, query->body, qdr_connection_columns);
        }

    }    // if (qd_parse_is_map(in_body) && identity)
    else {
        qdra_connection_set_bad_request(query);
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

