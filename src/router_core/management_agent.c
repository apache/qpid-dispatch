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
#include "dispatch_private.h"
#include "router_core_private.h"

#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/compose.h"
#include "qpid/dispatch/dispatch.h"
#include "qpid/dispatch/iterator.h"
#include "qpid/dispatch/parse.h"
#include "qpid/dispatch/router.h"
#include "qpid/dispatch/router_core.h"

#include <stdio.h>

const char *ENTITY = "entityType";
const char *TYPE = "type";
const char *COUNT = "count";
const char *OFFSET = "offset";
const char *NAME = "name";
const char *IDENTITY = "identity";


const char *OPERATION = "operation";
const char *ATTRIBUTE_NAMES = "attributeNames";


const unsigned char *config_address_entity_type    = (unsigned char*) "org.apache.qpid.dispatch.router.config.address";
const unsigned char *link_route_entity_type        = (unsigned char*) "org.apache.qpid.dispatch.router.config.linkRoute";
const unsigned char *auto_link_entity_type         = (unsigned char*) "org.apache.qpid.dispatch.router.config.autoLink";
const unsigned char *address_entity_type           = (unsigned char*) "org.apache.qpid.dispatch.router.address";
const unsigned char *link_entity_type              = (unsigned char*) "org.apache.qpid.dispatch.router.link";
const unsigned char *console_entity_type           = (unsigned char*) "org.apache.qpid.dispatch.console";
const unsigned char *router_entity_type            = (unsigned char*) "org.apache.qpid.dispatch.router";
const unsigned char *connection_entity_type        = (unsigned char*) "org.apache.qpid.dispatch.connection";
const unsigned char *config_exchange_entity_type   = (unsigned char*) "org.apache.qpid.dispatch.router.config.exchange";
const unsigned char *config_binding_entity_type    = (unsigned char*) "org.apache.qpid.dispatch.router.config.binding";
const unsigned char *conn_link_route_entity_type   = (unsigned char*) "org.apache.qpid.dispatch.router.connection.linkRoute";

const char * const status_description = "statusDescription";
const char * const correlation_id = "correlation-id";
const char * const results = "results";
const char * const status_code = "statusCode";

const char * MANAGEMENT_INTERNAL = "_local/$_management_internal";

//TODO - Move these to amqp.h
const unsigned char *MANAGEMENT_QUERY  = (unsigned char*) "QUERY";
const unsigned char *MANAGEMENT_CREATE = (unsigned char*) "CREATE";
const unsigned char *MANAGEMENT_READ   = (unsigned char*) "READ";
const unsigned char *MANAGEMENT_UPDATE = (unsigned char*) "UPDATE";
const unsigned char *MANAGEMENT_DELETE = (unsigned char*) "DELETE";


typedef enum {
    QD_ROUTER_OPERATION_QUERY,
    QD_ROUTER_OPERATION_CREATE,
    QD_ROUTER_OPERATION_READ,
    QD_ROUTER_OPERATION_UPDATE,
    QD_ROUTER_OPERATION_DELETE
} qd_router_operation_type_t;


typedef struct qd_management_context_t {
    qd_message_t               *source;
    qd_composed_field_t        *field;
    qdr_query_t                *query;
    qdr_core_t                 *core;
    int                         count;
    int                         current_count;
    qd_router_operation_type_t  operation_type;
} qd_management_context_t ;

ALLOC_DECLARE(qd_management_context_t);
ALLOC_DEFINE(qd_management_context_t);

/**
 * Convenience function to create and initialize context (qd_management_context_t)
 */
static qd_management_context_t* qd_management_context(qd_message_t               *source,
                                                      qd_composed_field_t        *field,
                                                      qdr_query_t                *query,
                                                      qdr_core_t                 *core,
                                                      qd_router_operation_type_t operation_type,
                                                      int                        count)
{
    qd_management_context_t *ctx = new_qd_management_context_t();
    ctx->count  = count;
    ctx->field  = field;
    ctx->source = qd_message_copy(source);
    ctx->query  = query;
    ctx->current_count = 0;
    ctx->core   = core;
    ctx->operation_type = operation_type;

    return ctx;
}


/**
 * Sets the error status on a new composed field.
 */
static void qd_set_response_status(const qd_amqp_error_t *error, qd_composed_field_t **field)
{
    //
    // Insert appropriate success or error
    //
    *field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, *field);
    qd_compose_start_map(*field);
    qd_compose_insert_string(*field, status_description);
    qd_compose_insert_string(*field, error->description);
    qd_compose_insert_string(*field, status_code);
    qd_compose_insert_int(*field, error->status);
    qd_compose_end_map(*field);
}


static void qd_set_properties(qd_message_t         *msg,
                              qd_iterator_t       **reply_to,
                              qd_composed_field_t **fld)
{
    //
    // Start header
    //
    *fld   = qd_compose(QD_PERFORMATIVE_HEADER, 0);

    qd_compose_start_list(*fld);
    qd_compose_insert_bool(*fld, 0);     // durable
    qd_compose_end_list(*fld);

    //
    // End header, start properties
    //
    *fld = qd_compose(QD_PERFORMATIVE_PROPERTIES, *fld);
    qd_compose_start_list(*fld);
    qd_compose_insert_null(*fld);                           // message-id
    qd_compose_insert_null(*fld);                           // user-id
    qd_iterator_t *correlation_id = qd_message_field_iterator_typed(msg, QD_FIELD_CORRELATION_ID);
    // Grab the reply_to field from the incoming message. This is the address we will send the response to.
    *reply_to = qd_message_field_iterator(msg, QD_FIELD_REPLY_TO);
    qd_compose_insert_string_iterator(*fld, *reply_to);     // to
    qd_compose_insert_null(*fld);                           // subject
    qd_compose_insert_null(*fld);                           // reply-to
    if (correlation_id)
        qd_compose_insert_typed_iterator(*fld, correlation_id); // correlation-id
    else
        qd_compose_insert_null(*fld);
    qd_compose_end_list(*fld);
    qd_iterator_free(correlation_id);
}


static void qd_manage_response_handler(void *context, const qd_amqp_error_t *status, bool more)
{
    qd_management_context_t *ctx = (qd_management_context_t*) context;
    bool need_free = false;
    if (ctx->operation_type == QD_ROUTER_OPERATION_QUERY) {
        if (status->status / 100 == 2) { // There is no error, proceed to conditionally call get_next
            if (more) {
                ctx->current_count++; // Increment how many you have at hand
                if (ctx->count != ctx->current_count) {
                    qdr_query_get_next(ctx->query);
                    return;
                } else {
                    //
                    // This is one case where the core agent won't free the query itself.
                    // Don't free immediately as we need status below and it may belong
                    // to the query.
                    //
                    need_free = true;
                }
            }
        }
        qd_compose_end_list(ctx->field);
        qd_compose_end_map(ctx->field);
    }
    else if (ctx->operation_type == QD_ROUTER_OPERATION_DELETE) {
        // The body of the delete response message MUST consist of an amqp-value section containing a Map with zero entries.
        qd_compose_start_map(ctx->field);
        qd_compose_end_map(ctx->field);
    }
    else if (ctx->operation_type == QD_ROUTER_OPERATION_READ) {
        if (status->status / 100 != 2) {
            qd_compose_start_map(ctx->field);
            qd_compose_end_map(ctx->field);
        }
    }

    qd_iterator_t       *reply_to = 0;
    qd_composed_field_t *fld = 0;

    // Start composing the message.
    // First set the properties on the message like reply_to, correlation-id etc.
    qd_set_properties(ctx->source, &reply_to, &fld);

    // Second, set the status on the message, QD_AMQP_OK or QD_AMQP_BAD_REQUEST and so on.
    qd_set_response_status(status, &fld);

    // Finally, compose and send the message.
    qd_message_t *msg = qd_message_compose(fld, ctx->field, 0, true);
    qdr_send_to1(ctx->core, msg, reply_to, true, false);
    qd_message_free(msg);

    // We have come to the very end. Free the appropriate memory.
    // Just go over this with Ted to see if I freed everything.

    qd_iterator_free(reply_to);
    qd_message_free(ctx->source);

    if (need_free) {
        qdr_query_free(ctx->query);
    }
    free_qd_management_context_t(ctx);
}


static void qd_core_agent_query_handler(qdr_core_t                 *core,
                                        qd_router_entity_type_t     entity_type,
                                        qd_router_operation_type_t  operation_type,
                                        qd_message_t               *msg,
                                        int                        *count,
                                        int                        *offset,
                                        uint64_t                    in_conn)
{
    //
    // Add the Body.
    //
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    // Start a map in the body. Look for the end map in the callback function, qd_manage_response_handler.
    qd_compose_start_map(field);

    //add a "attributeNames" key
    qd_compose_insert_string(field, ATTRIBUTE_NAMES);

    // Call local function that creates and returns a local qd_management_context_t object containing the values passed in.
    qd_management_context_t *ctx = qd_management_context(msg, field, 0, core, operation_type, (*count));

    // Grab the attribute names from the incoming message body. The attribute names will be used later on in the response.
    qd_parsed_field_t *attribute_names_parsed_field = 0;

    qd_iterator_t *body_iter = qd_message_field_iterator(msg, QD_FIELD_BODY);

    qd_parsed_field_t *body = qd_parse(body_iter);
    if (body != 0 && qd_parse_is_map(body)) {
        attribute_names_parsed_field = qd_parse_value_by_key(body, ATTRIBUTE_NAMES);
    }

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);
    ctx->query = qdr_manage_query(core, ctx, entity_type, attribute_names_parsed_field, field, in_conn);

    //Add the attribute names
    qdr_query_add_attribute_names(ctx->query); //this adds a list of attribute names like ["attribute1", "attribute2", "attribute3", "attribute4",]
    qd_compose_insert_string(field, results); //add a "results" key
    qd_compose_start_list(field); //start the list for results

    qdr_query_get_first(ctx->query, (*offset));

    qd_iterator_free(body_iter);
    qd_parse_free(body);
}


static void qd_core_agent_read_handler(qdr_core_t                 *core,
                                       qd_message_t               *msg,
                                       qd_router_entity_type_t     entity_type,
                                       qd_router_operation_type_t  operation_type,
                                       qd_iterator_t              *identity_iter,
                                       qd_iterator_t              *name_iter,
                                       uint64_t                    in_conn)
{
    //
    // Add the Body
    //
    qd_composed_field_t *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *ctx = qd_management_context(msg, body, 0, core, operation_type, 0);

    //Call the read API function
    qdr_manage_read(core, ctx, entity_type, name_iter, identity_iter, body, in_conn);
}


static void qd_core_agent_create_handler(qdr_core_t                 *core,
                                         qd_message_t               *msg,
                                         qd_router_entity_type_t     entity_type,
                                         qd_router_operation_type_t  operation_type,
                                         qd_iterator_t              *name_iter,
                                         uint64_t                    in_conn_id)
{
    //
    // Add the Body
    //
    qd_composed_field_t *out_body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *ctx = qd_management_context(msg, out_body, 0, core, operation_type, 0);

    qd_iterator_t *body_iter = qd_message_field_iterator(msg, QD_FIELD_BODY);

    qd_parsed_field_t *in_body = qd_parse(body_iter);

    qd_buffer_list_t empty_list;
    DEQ_INIT(empty_list);
    qdr_manage_create(core, ctx, entity_type, name_iter, in_body, out_body, empty_list, in_conn_id);
    qd_iterator_free(body_iter);
}


static void qd_core_agent_update_handler(qdr_core_t                 *core,
                                         qd_message_t               *msg,
                                         qd_router_entity_type_t     entity_type,
                                         qd_router_operation_type_t  operation_type,
                                         qd_iterator_t              *identity_iter,
                                         qd_iterator_t              *name_iter,
                                         uint64_t                    in_conn)
{
    qd_composed_field_t *out_body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    qd_management_context_t *ctx = qd_management_context(msg, out_body, 0, core, operation_type, 0);

    qd_iterator_t *iter = qd_message_field_iterator(msg, QD_FIELD_BODY);
    qd_parsed_field_t *in_body= qd_parse(iter);
    qd_iterator_free(iter);

    qdr_manage_update(core, ctx, entity_type, name_iter, identity_iter, in_body, out_body, in_conn);

}


static void qd_core_agent_delete_handler(qdr_core_t                 *core,
                                         qd_message_t               *msg,
                                         qd_router_entity_type_t     entity_type,
                                         qd_router_operation_type_t  operation_type,
                                         qd_iterator_t              *identity_iter,
                                         qd_iterator_t              *name_iter,
                                         uint64_t                    in_conn)
{
    //
    // Add the Body
    //
    qd_composed_field_t *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *ctx = qd_management_context(msg, body, 0, core, operation_type, 0);

    qdr_manage_delete(core, ctx, entity_type, name_iter, identity_iter, in_conn);
}


/**
 * Checks the content of the message to see if this can be handled by the C-management agent. If this agent cannot handle it, it will be
 * forwarded to the Python agent.
 */
static bool qd_can_handle_request(qd_parsed_field_t           *properties_fld,
                                  qd_router_entity_type_t     *entity_type,
                                  qd_router_operation_type_t  *operation_type,
                                  qd_iterator_t              **identity_iter,
                                  qd_iterator_t              **name_iter,
                                  int                         *count,
                                  int                         *offset)
{
    // The must be a property field and that property field should be a AMQP map. This is true for QUERY but I need
    // to check if it true for CREATE, UPDATE and DELETE
    if (properties_fld == 0 || !qd_parse_is_map(properties_fld))
        return false;

    //
    // Only certain entity types can be handled by this agent.
    // 'entityType': 'org.apache.qpid.dispatch.router.address
    // 'entityType': 'org.apache.qpid.dispatch.router.link'
    // TODO - Add more entity types here. The above is not a complete list.

    qd_parsed_field_t *parsed_field = qd_parse_value_by_key(properties_fld, IDENTITY);
    if (parsed_field!=0) {
        *identity_iter = qd_parse_raw(parsed_field);
    }
    parsed_field = qd_parse_value_by_key(properties_fld, NAME);
    if (parsed_field!=0) {
        *name_iter = qd_parse_raw(parsed_field);
    }

    parsed_field = qd_parse_value_by_key(properties_fld, ENTITY);

    if (parsed_field == 0) { // Sometimes there is no 'entityType' but 'type' might be available.
        parsed_field = qd_parse_value_by_key(properties_fld, TYPE);
        if (parsed_field == 0)
            return false;
    }

    if (qd_iterator_equal(qd_parse_raw(parsed_field), address_entity_type))
        *entity_type = QD_ROUTER_ADDRESS;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), link_entity_type))
        *entity_type = QD_ROUTER_LINK;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), config_address_entity_type))
        *entity_type = QD_ROUTER_CONFIG_ADDRESS;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), link_route_entity_type))
        *entity_type = QD_ROUTER_CONFIG_LINK_ROUTE;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), auto_link_entity_type))
        *entity_type = QD_ROUTER_CONFIG_AUTO_LINK;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), router_entity_type))
        *entity_type = QD_ROUTER_ROUTER;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), console_entity_type))
        *entity_type = QD_ROUTER_FORBIDDEN;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), connection_entity_type))
        *entity_type = QD_ROUTER_CONNECTION;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), config_exchange_entity_type))
        *entity_type = QD_ROUTER_EXCHANGE;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), config_binding_entity_type))
        *entity_type = QD_ROUTER_BINDING;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), conn_link_route_entity_type))
        *entity_type = QD_ROUTER_CONN_LINK_ROUTE;
    else
        return false;


    parsed_field = qd_parse_value_by_key(properties_fld, OPERATION);

    if (parsed_field == 0)
        return false;

    if (qd_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_QUERY))
        (*operation_type) = QD_ROUTER_OPERATION_QUERY;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_CREATE))
        (*operation_type) = QD_ROUTER_OPERATION_CREATE;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_READ))
        (*operation_type) = QD_ROUTER_OPERATION_READ;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_UPDATE))
        (*operation_type) = QD_ROUTER_OPERATION_UPDATE;
    else if (qd_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_DELETE))
        (*operation_type) = QD_ROUTER_OPERATION_DELETE;
    else
        // This is an unknown operation type. cannot be handled, return false.
        return false;

    // Obtain the count and offset.
    parsed_field = qd_parse_value_by_key(properties_fld, COUNT);
    if (parsed_field)
        (*count) = (int)qd_parse_as_long(parsed_field);
    else
        (*count) = -1;

    parsed_field = qd_parse_value_by_key(properties_fld, OFFSET);
    if (parsed_field)
        (*offset) = (int)qd_parse_as_long(parsed_field);
    else
        (*offset) = 0;

    return true;
}


/**
 *
 * Handler for the management agent.
 *
 */
uint64_t qdr_management_agent_on_message(void *context, qd_message_t *msg, int unused_link_id, int unused_cost,
                                         uint64_t in_conn_id, const qd_policy_spec_t *policy_spec, qdr_error_t **error)
{
    qdr_core_t *core = (qdr_core_t*) context;
    qd_iterator_t *app_properties_iter = qd_message_field_iterator(msg, QD_FIELD_APPLICATION_PROPERTIES);

    qd_router_entity_type_t    entity_type = 0;
    qd_router_operation_type_t operation_type = 0;

    qd_iterator_t *identity_iter = 0;
    qd_iterator_t *name_iter = 0;

    int32_t count = 0;
    int32_t offset = 0;

    *error = 0;

    qd_parsed_field_t *properties_fld = qd_parse(app_properties_iter);

    if (qd_can_handle_request(properties_fld, &entity_type, &operation_type, &identity_iter, &name_iter, &count, &offset)) {
        switch (operation_type) {
        case QD_ROUTER_OPERATION_QUERY:
            qd_core_agent_query_handler(core, entity_type, operation_type, msg, &count, &offset, in_conn_id);
            break;
        case QD_ROUTER_OPERATION_CREATE:
            qd_core_agent_create_handler(core, msg, entity_type, operation_type, name_iter, in_conn_id);
            break;
        case QD_ROUTER_OPERATION_READ:
            qd_core_agent_read_handler(core, msg, entity_type, operation_type, identity_iter, name_iter, in_conn_id);
            break;
        case QD_ROUTER_OPERATION_UPDATE:
            qd_core_agent_update_handler(core, msg, entity_type, operation_type, identity_iter, name_iter, in_conn_id);
            break;
        case QD_ROUTER_OPERATION_DELETE:
            qd_core_agent_delete_handler(core, msg, entity_type, operation_type, identity_iter, name_iter, in_conn_id);
            break;
        }
    } else {
        //
        // The C management agent is not going to handle this request. Forward it off to Python.
        //
        qdr_send_to2(core, msg, MANAGEMENT_INTERNAL, false, false);
    }

    qd_iterator_free(app_properties_iter);
    qd_parse_free(properties_fld);
    return PN_ACCEPTED;
}

