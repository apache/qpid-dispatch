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

#include "agent_waypoint.h"

const char *address   = "address";
const char *connector = "connector";
const char *inPhase   = "inPhase";
const char *outPhase  = "outPhase";
const char *mode      = "mode";

#define QDR_WAYPOINT_NAME         0
#define QDR_WAYPOINT_ADDRESS      1
#define QDR_WAYPOINT_CONNECTOR    2
#define QDR_WAYPOINT_INPHASE      3
#define QDR_WAYPOINT_OUTPHASE     4
#define QDR_WAYPOINT_MODE         5

#define QDR_WAYPOINT_COLUMN_COUNT  6

static const char *qdr_waypoint_columns[] =
    {"name",
     "address",
     "connector",
     "inPhase",
     "outPhase",
     "mode",
     0};

static void qdr_insert_waypoint_columns_CT(qd_composed_field_t  *body,
                                          int column_index)
{
    // TODO replace nulls with actual values.
    switch(column_index) {
        case QDR_WAYPOINT_NAME:
            qd_compose_insert_null(body);
            break;

        case QDR_WAYPOINT_ADDRESS:
            qd_compose_insert_null(body);
            break;

        case QDR_WAYPOINT_CONNECTOR:
            qd_compose_insert_null(body);
            break;

        case QDR_WAYPOINT_INPHASE:
            qd_compose_insert_null(body);
            break;

        case QDR_WAYPOINT_OUTPHASE:
            qd_compose_insert_null(body);
            break;

        case QDR_WAYPOINT_MODE:
            qd_compose_insert_null(body);  // TEMP
            break;

        default:
            qd_compose_insert_null(body);
            break;
    }

}

static void qdr_manage_write_response_map_CT(qd_composed_field_t *body)
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_WAYPOINT_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_waypoint_columns[i]);
        qdr_insert_waypoint_columns_CT(body, i);
    }

    qd_compose_end_map(body);
}

void qdra_waypoint_create_CT(qdr_core_t          *core,
                             qd_field_iterator_t *name,
                             qdr_query_t         *query,
                             qd_parsed_field_t   *in_body)
{
    // Get the map fields from the body
    if (qd_parse_is_map(in_body)) {
        qd_parsed_field_t *address_field = qd_parse_value_by_key(in_body, address);
        qd_parsed_field_t *connector_field = qd_parse_value_by_key(in_body, connector);
        qd_parsed_field_t *inPhase_field = qd_parse_value_by_key(in_body, inPhase);
        qd_parsed_field_t *outPhase_field = qd_parse_value_by_key(in_body, outPhase);
        qd_parsed_field_t *mode_field = qd_parse_value_by_key(in_body, mode);

        if ( address_field   &&
             connector_field &&
             inPhase_field   &&
             outPhase_field  &&
             mode_field) {
            // TODO - Add code here that would actually create a waypoint.
            // If the request was successful then the statusCode MUST be 201 (Created) and the body of the message
            // MUST consist an amqp-value section that contains a Map containing the actual attributes of the entity created
            qdr_manage_write_response_map_CT(query->body);
            query->status = &QD_AMQP_CREATED;
        }
        else {
            query->status = &QD_AMQP_BAD_REQUEST;
        }
    }
    else {
        query->status = &QD_AMQP_BAD_REQUEST;
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);


}

void qdra_waypoint_delete_CT(qdr_core_t          *core,
                             qd_field_iterator_t *name,
                             qd_field_iterator_t *identity,
                             qdr_query_t          *query)
{
    bool success = true;

    if (identity) {//If there is identity, ignore the name
       //TOOD - do something here
    }
    else if (name) {
       //TOOD - do something here
    }
    else {
        query->status = &QD_AMQP_BAD_REQUEST;
        success = false;
    }


    // TODO - Add more logic here.
    if (success) {
        // If the request was successful then the statusCode MUST be 204 (No Content).
        query->status = &QD_AMQP_NO_CONTENT;
    }

    // The body of the message MUST consist of an amqp-value section containing a Map with zero entries.
    qd_compose_start_map(query->body);
    qd_compose_end_map(query->body);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_waypoint_update_CT(qdr_core_t *core, qd_field_iterator_t *name, qdr_query_t *query)
{

}


