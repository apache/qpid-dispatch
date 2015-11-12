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

#include <stdio.h>
#include <qpid/dispatch/parse.h>
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/router.h>
#include <qpid/dispatch/router_core.h>
#include <qpid/dispatch/compose.h>
#include <qpid/dispatch/dispatch.h>
#include "management_agent_private.h"
#include "dispatch_private.h"
#include "alloc.h"

const char *entity_type_key = "entityType";
const char *count_key = "count";
const char *offset_key = "offset";

const char *operation_type_key = "operation";
const char *attribute_names_key = "attributeNames";

const unsigned char *address_entity_type = (unsigned char*) "org.apache.qpid.dispatch.router.address";
const unsigned char *link_entity_type    = (unsigned char*) "org.apache.qpid.dispatch.router.link";

const char * const status_description = "statusDescription";
const char * const correlation_id = "correlation-id";
const char * const results = "results";
const char * const status_code = "statusCode";

const char * MANAGEMENT_INTERNAL = "$management_internal";

//TODO - Move these to amqp.h
const unsigned char *MANAGEMENT_QUERY = (unsigned char*) "QUERY";
const unsigned char *MANAGEMENT_CREATE = (unsigned char*) "CREATE";
const unsigned char *MANAGEMENT_READ = (unsigned char*) "READ";
const unsigned char *MANAGEMENT_UPDATE = (unsigned char*) "UPDATE";
const unsigned char *MANAGEMENT_DELETE = (unsigned char*) "DELETE";


typedef enum {
    QD_ROUTER_OPERATION_QUERY,
    QD_ROUTER_OPERATION_CREATE,
    QD_ROUTER_OPERATION_READ,
    QD_ROUTER_OPERATION_UPDATE,
    QD_ROUTER_OPERATION_DELETE,
} qd_router_operation_type_t;


typedef struct qd_management_context_t {
    qd_message_t        *msg;
    qd_composed_field_t *field;
    qdr_query_t         *query;
    qd_dispatch_t       *qd;
    qd_field_iterator_t *to;
    int                 count;
    int                 current_count;
} qd_management_context_t ;

ALLOC_DECLARE(qd_management_context_t);
ALLOC_DEFINE(qd_management_context_t);

/**
 * Convenience function to create and initialize context (qd_management_context_t)
 */
static qd_management_context_t* qd_management_context(qd_message_t         *msg,
                                                      qd_composed_field_t  *field,
                                                      qdr_query_t          *query,
                                                      qd_field_iterator_t  *to,
                                                      qd_dispatch_t        *qd,
                                                      int                  count)
{
    qd_management_context_t *ctx = new_qd_management_context_t();
    ctx->count = count;
    ctx->field = field;
    ctx->msg   = msg;
    if (query)
        ctx->query = query;
    else
        ctx->query = 0;
    ctx->current_count = 0;
    ctx->qd = qd;
    ctx->to = to;

    return ctx;
}

static void qd_compose_send(qd_management_context_t *ctx)
{
    qd_compose_end_list(ctx->field);
    qd_compose_end_map(ctx->field);
    qd_message_compose_2(ctx->msg, ctx->field);
    qd_router_send(ctx->qd, ctx->to, ctx->msg);

    //We have come to the very end. Free the appropriate memory.
    //ctx->field has already been freed in the call to qd_compose_end_list(ctx->field)
    //ctx->query has also been already freed
    qd_message_free(ctx->msg);
    qd_field_iterator_free(ctx->to);
    free_qd_management_context_t(ctx);
}


static void manage_response_handler (void *context, const qd_amqp_error_t *status, bool more)
{
    qd_management_context_t *ctx = (qd_management_context_t*) context;

    //TODO - Check the status (qd_amqp_error_t) here first. If the status is anything other that 200, you need to send it back the message with the status.

    if (!more || ctx->count == 0) {
       // If Count is zero or there are no more rows to process or the status returned is something other than
       // QD_AMQP_OK, we will close the list, send the message and
       qd_compose_send(ctx);
    }
    else {
        ctx->current_count++; // Increment how many you have at hand

        if (ctx->count == ctx->current_count) //The count has matched, we are done, close the list and send out the message
            qd_compose_send(ctx);
        else
            qdr_query_get_next(ctx->query);
    }
}

static void core_agent_query_handler(qd_dispatch_t           *qd,
                                     qd_router_entity_type_t entity_type,
                                     qd_message_t            *msg,
                                     int                     *count,
                                     int                     *offset)
{
    qdr_core_t *core = qd_router_core(qd);

    // Create a new message
    qd_message_t *message = qd_message();
    qd_field_iterator_t *correlation_id = qd_message_field_iterator_typed(msg, QD_FIELD_CORRELATION_ID);
    // Grab the reply_to field from the incoming message. This is the address we will send the response to.
    qd_field_iterator_t *reply_to = qd_message_field_iterator(msg, QD_FIELD_REPLY_TO);

    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);
    qd_compose_start_list(field);
    qd_compose_insert_null(field);                           // message-id
    qd_compose_insert_null(field);                           // user-id
    qd_compose_insert_string_iterator(field, reply_to);       // to
    qd_compose_insert_null(field);                           // subject
    qd_compose_insert_null(field);
    qd_compose_insert_typed_iterator(field, correlation_id);
    qd_compose_end_list(field);


    // Get the attributeNames
    qd_parsed_field_t *attribute_names_parsed_field = 0;

    qd_parsed_field_t *body = qd_parse(qd_message_field_iterator(msg, QD_FIELD_BODY));

    if (body != 0 && qd_parse_is_map(body))
        attribute_names_parsed_field = qd_parse_value_by_key(body, attribute_names_key);

    //
    // Insert application property map with statusDescription of OK and status code of 200
    //
    field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, field);
    qd_compose_start_map(field);

    // Insert {'statusDescription': 'OK'}
    qd_compose_insert_string(field, status_description);
    qd_compose_insert_string(field, QD_AMQP_OK.description);

    // Insert {'statusCode': '200'}
    qd_compose_insert_string(field, status_code);
    qd_compose_insert_uint(field, QD_AMQP_OK.status);

    qd_compose_end_map(field);

    //
    // Add Body
    //
    field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, field);

    // Start a map in the body
    qd_compose_start_map(field);

    qd_compose_insert_string(field, attribute_names_key); //add a "attributeNames" key

    // Set the callback function.
    qdr_manage_handler(core, manage_response_handler);

    // Local local function that creates and returns a qd_management_context_t
    qd_management_context_t *ctx = qd_management_context(message, field, 0, reply_to, qd, (*count));

    ctx->query = qdr_manage_query(core, ctx, entity_type, attribute_names_parsed_field, field);

    //Add the attribute names
    qdr_query_add_attribute_names(ctx->query);

    qd_compose_insert_string(field, results); //add a "results" key
    qd_compose_start_list(field); //start the list for results

    qdr_query_get_first(ctx->query, (*offset));
}

static void core_agent_create_handler()
{

}

static void core_agent_read_handler()
{

}

static void core_agent_update_handler()
{

}

static void core_agent_delete_handler()
{

}

/**
 * Checks the content of the message to see if this can be handled by this agent.
 */
static bool can_handle_request(qd_field_iterator_t *props,
                               qd_router_entity_type_t *entity_type,
                               qd_router_operation_type_t *operation_type,
                               int *count,
                               int *offset)
{
    qd_parsed_field_t *fld = qd_parse(props);

    // The must be a property field and that property field should be a AMQP map. This is true for QUERY but I need
    // to check if it true for CREATE, UPDATE and DELETE
    if (fld == 0 || !qd_parse_is_map(fld))
        return false;

    //
    // Only certain entity types can be handled by this agent.
    // 'entityType': 'org.apache.qpid.dispatch.router.address
    // 'entityType': 'org.apache.qpid.dispatch.router.link'
    // TODO - Add more entity types here. The above is not a complete list.

    qd_parsed_field_t *parsed_field = qd_parse_value_by_key(fld, entity_type_key);

    if (parsed_field == 0)
        return false;

    if (qd_field_iterator_equal(qd_parse_raw(parsed_field), address_entity_type))
        (*entity_type) = QD_ROUTER_ADDRESS;
    else if(qd_field_iterator_equal(qd_parse_raw(parsed_field), link_entity_type))
        (*entity_type) = QD_ROUTER_LINK;
    else
        return false;


    parsed_field = qd_parse_value_by_key(fld, operation_type_key);

    if (parsed_field == 0)
        return false;

    if (qd_field_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_QUERY))
        (*operation_type) = QD_ROUTER_OPERATION_QUERY;
    else if (qd_field_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_CREATE))
        (*operation_type) = QD_ROUTER_OPERATION_CREATE;
    else if (qd_field_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_READ))
        (*operation_type) = QD_ROUTER_OPERATION_READ;
    else if (qd_field_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_UPDATE))
        (*operation_type) = QD_ROUTER_OPERATION_UPDATE;
    else if (qd_field_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_DELETE))
        (*operation_type) = QD_ROUTER_OPERATION_DELETE;
    else
        // This is an unknown operation type. cannot be handled, return false.
        return false;

    // Obtain the count and offset.
    parsed_field = qd_parse_value_by_key(fld, count_key);
    if (parsed_field)
        (*count) = qd_parse_as_int(parsed_field);
    else
        (*count) = -1;

    parsed_field = qd_parse_value_by_key(fld, offset_key);
    if (parsed_field)
        (*offset) = qd_parse_as_int(parsed_field);
    else
        (*offset) = 0;

    qd_parse_free(parsed_field);

    return true;
}

/**
 *
 * Handler for the management agent.
 *
 */
void management_agent_handler(void *context, qd_message_t *msg, int link_id)
{
    qd_dispatch_t *qd = (qd_dispatch_t*) context;
    qd_field_iterator_t *iter = qd_message_field_iterator(msg, QD_FIELD_APPLICATION_PROPERTIES);

    qd_router_entity_type_t    entity_type = 0;
    qd_router_operation_type_t operation_type = 0;

    int32_t count = 0;
    int32_t offset = 0;

    if (can_handle_request(iter, &entity_type, &operation_type, &count, &offset)) {
        switch (operation_type) {
            case QD_ROUTER_OPERATION_QUERY:
                core_agent_query_handler(qd, entity_type, msg, &count, &offset);
                break;
            case QD_ROUTER_OPERATION_CREATE:
                core_agent_create_handler();
                break;
            case QD_ROUTER_OPERATION_READ:
                core_agent_read_handler();
                break;
            case QD_ROUTER_OPERATION_UPDATE:
                core_agent_update_handler();
                break;
            case QD_ROUTER_OPERATION_DELETE:
                core_agent_delete_handler();
                break;
       }
    }
    else
        qd_router_send2(qd, MANAGEMENT_INTERNAL, msg); //the C management agent is not going to handle this request. Forward it off to Python.
    // TODO - This is wrong. Need to find out how I can forward off the message to $management_internal so it can be handled by Python.

    qd_field_iterator_free(iter);

}

