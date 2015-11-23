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
#include "router_core_private.h"
#include "dispatch_private.h"
#include "agent_waypoint.h"
#include "alloc.h"

const char *entity_type_key = "entityType";
const char *type_key = "type";
const char *count_key = "count";
const char *offset_key = "offset";
const char *name_key = "name";
const char *identity_key = "identity";


const char *operation_type_key = "operation";
const char *attribute_names_key = "attributeNames";

const unsigned char *waypoint_entity_type = (unsigned char*) "org.apache.qpid.dispatch.waypoint";
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
    qd_message_t        *source;
    qd_composed_field_t *field;
    qdr_query_t         *query;
    qd_dispatch_t       *qd;
    int                 count;
    int                 current_count;
    qd_router_operation_type_t operation_type;
} qd_management_context_t ;

ALLOC_DECLARE(qd_management_context_t);
ALLOC_DEFINE(qd_management_context_t);

/**
 * Convenience function to create and initialize context (qd_management_context_t)
 */
static qd_management_context_t* qd_management_context(qd_message_t               *msg,
                                                      qd_message_t               *source,
                                                      qd_composed_field_t        *field,
                                                      qdr_query_t                *query,
                                                      qd_dispatch_t              *qd,
                                                      qd_router_operation_type_t operation_type,
                                                      int                        count)
{
    qd_management_context_t *ctx = new_qd_management_context_t();
    ctx->count  = count;
    ctx->field  = field;
    ctx->msg    = msg;
    ctx->source = source;
    if (query)
        ctx->query = query;
    else
        ctx->query = 0;
    ctx->current_count = 0;
    ctx->qd = qd;
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
    qd_compose_insert_uint(*field, error->status);

    qd_compose_end_map(*field);
}


static void qd_set_properties(qd_message_t        *msg,
                              qd_field_iterator_t **reply_to,
                              qd_composed_field_t **fld)
{
    qd_field_iterator_t *correlation_id = qd_message_field_iterator_typed(msg, QD_FIELD_CORRELATION_ID);
    // Grab the reply_to field from the incoming message. This is the address we will send the response to.
    *reply_to = qd_message_field_iterator(msg, QD_FIELD_REPLY_TO);
    *fld = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);
    qd_compose_start_list(*fld);
    qd_compose_insert_null(*fld);                           // message-id
    qd_compose_insert_null(*fld);                           // user-id
    qd_compose_insert_string_iterator(*fld, *reply_to);     // to
    qd_compose_insert_null(*fld);                           // subject
    qd_compose_insert_null(*fld);
    qd_compose_insert_typed_iterator(*fld, correlation_id);
    qd_compose_end_list(*fld);

}


void qd_manage_response_handler(void *context, const qd_amqp_error_t *status, bool more)
{
    qd_management_context_t *ctx = (qd_management_context_t*) context;

    if (ctx->operation_type == QD_ROUTER_OPERATION_QUERY) {
        if (status == &QD_AMQP_OK) { // There is no error, proceed to conditionally call get_next
            if (more) {
               //If there are no more rows to process or the status returned is something other than
               // QD_AMQP_OK, we will close the list, send the message and
               ctx->current_count++; // Increment how many you have at hand
               if (ctx->count != ctx->current_count) {
                   qdr_query_get_next(ctx->query);
                   return;
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

    qd_field_iterator_t *reply_to = 0;

    qd_composed_field_t *fld = 0;

    // Start composing the message.
    // First set the properties on the message like reply_to, correlation-id etc.
    qd_set_properties(ctx->source, &reply_to, &fld);

    // Second, set the status on the message, QD_AMQP_OK or QD_AMQP_BAD_REQUEST and so on.
    qd_set_response_status(status, &fld);

    // Finally, compose and send the message.
    qd_message_compose_3(ctx->msg, fld, ctx->field);
    qd_router_send(ctx->qd, reply_to, ctx->msg);

    // We have come to the very end. Free the appropriate memory.
    // ctx->field has already been freed in the call to qd_compose_end_list(ctx->field)
    // ctx->query has also been already freed
    // Just go over this with Ted to see if I freed everything.

    if (ctx->msg)
        qd_message_free(ctx->msg);
    free_qd_management_context_t(ctx);
}


static void qd_core_agent_query_handler(qd_dispatch_t              *qd,
                                        qd_router_entity_type_t    entity_type,
                                        qd_router_operation_type_t operation_type,
                                        qd_message_t               *msg,
                                        int                        *count,
                                        int                        *offset)
{
    qdr_core_t *core = qd_router_core(qd);

    //
    // Add the Body
    //
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    // Start a map in the body. Look for the end map in the callback function, qd_manage_response_handler.
    qd_compose_start_map(field);

    qd_compose_insert_string(field, attribute_names_key); //add a "attributeNames" key

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *ctx = qd_management_context(qd_message(), msg, field, 0, qd, operation_type, (*count));

    // Grab the attribute names from the incoming message body. The attribute names will be used later on in the response.
    qd_parsed_field_t *attribute_names_parsed_field = 0;
    qd_parsed_field_t *body = qd_parse(qd_message_field_iterator(msg, QD_FIELD_BODY));
    if (body != 0 && qd_parse_is_map(body))
        attribute_names_parsed_field = qd_parse_value_by_key(body, attribute_names_key);

    ctx->query = qdr_manage_query(core, ctx, entity_type, attribute_names_parsed_field, field);

    //Add the attribute names
    qdr_query_add_attribute_names(ctx->query); //this adds adds a list of attribute names like ["attribute1", "attribute2", "attribute3", "attribute4",]
    qd_compose_insert_string(field, results); //add a "results" key
    qd_compose_start_list(field); //start the list for results

    qdr_query_get_first(ctx->query, (*offset));
}


static void qd_core_agent_read_handler(qd_dispatch_t              *qd,
                                       qd_message_t               *msg,
                                       qd_router_entity_type_t     entity_type,
                                       qd_router_operation_type_t  operation_type,
                                       qd_field_iterator_t        *identity_iter,
                                       qd_field_iterator_t        *name_iter)
{
    qdr_core_t *core = qd_router_core(qd);

    //
    // Add the Body
    //
    qd_composed_field_t *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *ctx = qd_management_context(qd_message(), msg, body, 0, qd, operation_type, 0);

    //Call the read API function
    qdr_manage_read(core, ctx, entity_type, name_iter, identity_iter, body);
}


/**
 * Returns true if all the keys contained in the body map are applicable to a waypoints, false otherwise
 * @see qdr_waypoint_columns
 */
static bool qd_is_valid_keys(qd_parsed_field_t *in_body, const char *qdr_columns[], int column_count)
{
    if(in_body != 0 && qd_parse_is_map(in_body)) {
        //
        // A map containing attributes with invalid values for an entity MUST result in a failure response with a statusCode of 400 (Bad Request).
        //
        int j=0;
        qd_parsed_field_t *field = qd_parse_sub_key(in_body, j);
        while (field) {
            bool found = false;
            for(int i = 1; i < column_count; i++) {
                if (qd_field_iterator_equal(qd_parse_raw(field), (unsigned char*)qdr_columns[i])) {
                    found = true;
                    break;
                }
            }
            if (!found) // Some bad field was specified in the body map. Reject this request
                return false;
            j++;
            field = qd_parse_sub_key(in_body, j);
        }
    }
    else
        // The body is either empty or the body is not a map, return false
        return false;

    return true;
}


/**
 * The body map containing any attributes that are not applicable for the entity being updated MUST result in a failure response with a statusCode of 400 (Bad Request).
 */
static bool qd_is_valid_request(qd_router_entity_type_t  entity_type,
                                qd_parsed_field_t       *in_body,
                                qd_composed_field_t     *out_body,
                                qd_management_context_t *ctx)
{
    if (entity_type == QD_ROUTER_WAYPOINT) {
        if(!qd_is_valid_keys(in_body, qdr_waypoint_columns, QDR_WAYPOINT_COLUMN_COUNT)){
            // Some bad field was specified in the body map. Reject this request
            return false;
        }
    }
    else if (entity_type == QD_ROUTER_ADDRESS) {
        /*
        if(!qd_is_valid_keys(in_body, qdr_address_columns, QDR_ADDRESS_COLUMN_COUNT)){
            return false;
        }
        */
    }

    return true;
}


static void qd_core_agent_create_handler(qd_dispatch_t              *qd,
                                         qd_message_t               *msg,
                                         qd_router_entity_type_t     entity_type,
                                         qd_router_operation_type_t  operation_type,
                                         qd_field_iterator_t        *name_iter)
{
    qdr_core_t *core = qd_router_core(qd);

    //
    // Add the Body
    //
    qd_composed_field_t *out_body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
    qd_parsed_field_t *in_body= qd_parse(qd_message_field_iterator(msg, QD_FIELD_BODY));

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *ctx = qd_management_context(qd_message(), msg, out_body, 0, qd, operation_type, 0);

    if(qd_is_valid_request(entity_type, in_body, out_body, ctx))
        qdr_manage_create(core, ctx, entity_type, name_iter, in_body, out_body);
    else {
        // The body of the incoming message did not pass validation tests. Reject this request
        qd_compose_start_map(out_body);
        qd_compose_end_map(out_body);
        qd_manage_response_handler(ctx, &QD_AMQP_BAD_REQUEST, false);
    }
}


static void qd_core_agent_update_handler(qd_dispatch_t              *qd,
                                         qd_message_t               *msg,
                                         qd_router_entity_type_t     entity_type,
                                         qd_router_operation_type_t  operation_type,
                                         qd_field_iterator_t        *identity_iter,
                                         qd_field_iterator_t        *name_iter)
{
    qdr_core_t *core = qd_router_core(qd);

    //
    // Add the Body
    //
    qd_composed_field_t *out_body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *ctx = qd_management_context(qd_message(), msg, out_body, 0, qd, operation_type, 0);

    qd_parsed_field_t *in_body= qd_parse(qd_message_field_iterator(msg, QD_FIELD_BODY));

    if(qd_is_valid_request(entity_type, in_body, out_body, ctx))
        qdr_manage_update(core, ctx, entity_type, name_iter, identity_iter, in_body, out_body);
    else {
        // The body of the incoming message did not pass validation tests. Reject this request
        qd_compose_start_map(out_body);
        qd_compose_end_map(out_body);
        qd_manage_response_handler(ctx, &QD_AMQP_BAD_REQUEST, false);
    }
}


static void qd_core_agent_delete_handler(qd_dispatch_t              *qd,
                                         qd_message_t               *msg,
                                         qd_router_entity_type_t    entity_type,
                                         qd_router_operation_type_t operation_type,
                                         qd_field_iterator_t        *identity_iter,
                                         qd_field_iterator_t        *name_iter)
{
    qdr_core_t *core = qd_router_core(qd);

    //
    // Add the Body
    //
    qd_composed_field_t *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *ctx = qd_management_context(qd_message(), msg, body, 0, qd, operation_type, 0);

    qdr_manage_delete(core, ctx, entity_type, name_iter, identity_iter);
}


/**
 * Checks the content of the message to see if this can be handled by this agent.
 */
static bool qd_can_handle_request(qd_field_iterator_t        *props,
                                  qd_router_entity_type_t    *entity_type,
                                  qd_router_operation_type_t *operation_type,
                                  qd_field_iterator_t        **identity_iter,
                                  qd_field_iterator_t        **name_iter,
                                  int                        *count,
                                  int                        *offset)
{
    qd_parsed_field_t *fld = qd_parse(props);

    // The must be a property field and that property field should be a AMQP map. This is true for QUERY but I need
    // to check if it true for CREATE, UPDATE and DELETE
    if (fld == 0 || !qd_parse_is_map(fld))
        return false;

    qd_parsed_field_t *parsed_field = qd_parse_value_by_key(fld, identity_key);
    if (parsed_field!=0) {
        *identity_iter = qd_parse_raw(parsed_field);
    }
    parsed_field = qd_parse_value_by_key(fld, name_key);
    if (parsed_field!=0) {
        *name_iter = qd_parse_raw(parsed_field);
    }

    parsed_field = qd_parse_value_by_key(fld, entity_type_key);

    if (parsed_field == 0) { // Sometimes there is no 'entityType' but 'type' might be available.
        parsed_field = qd_parse_value_by_key(fld, type_key);
        if (parsed_field == 0)
            return false;
    }

    if (qd_field_iterator_equal(qd_parse_raw(parsed_field), address_entity_type))
        (*entity_type) = QD_ROUTER_ADDRESS;
    else if(qd_field_iterator_equal(qd_parse_raw(parsed_field), link_entity_type))
        (*entity_type) = QD_ROUTER_LINK;
    else if(qd_field_iterator_equal(qd_parse_raw(parsed_field), waypoint_entity_type))
        (*entity_type) = QD_ROUTER_WAYPOINT;
    else
        return false;


    parsed_field = qd_parse_value_by_key(fld, operation_type_key);

    if (parsed_field == 0)
        return false;

    if (qd_field_iterator_equal(qd_parse_raw(parsed_field), MANAGEMENT_QUERY)) {
        (*operation_type) = QD_ROUTER_OPERATION_QUERY;
        // Obtain the count and offset. Count and offset fields are only applicable to queries.
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
    }
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

    //qd_parse_free(parsed_field);

    return true;
}


/**
 *
 * Handler for the management agent.
 *
 */
void qd_router_agent_on_message(void *context, qd_message_t *msg, int link_id)
{
    qd_dispatch_t *qd = (qd_dispatch_t*) context;
    qd_field_iterator_t *app_properties_iter = qd_message_field_iterator(msg, QD_FIELD_APPLICATION_PROPERTIES);

    qd_router_entity_type_t    entity_type = 0;
    qd_router_operation_type_t operation_type = 0;

    qd_field_iterator_t *identity_iter = 0;
    qd_field_iterator_t *name_iter = 0;

    int32_t count = 0;
    int32_t offset = 0;

    if (qd_can_handle_request(app_properties_iter, &entity_type, &operation_type, &identity_iter, &name_iter, &count, &offset)) {
        switch (operation_type) {
            case QD_ROUTER_OPERATION_QUERY:
                qd_core_agent_query_handler(qd, entity_type, operation_type, msg, &count, &offset);
                break;
            case QD_ROUTER_OPERATION_CREATE:
                qd_core_agent_create_handler(qd, msg, entity_type, operation_type, name_iter);
                break;
            case QD_ROUTER_OPERATION_READ:
                qd_core_agent_read_handler(qd, msg, entity_type, operation_type, identity_iter, name_iter);
                break;
            case QD_ROUTER_OPERATION_UPDATE:
                qd_core_agent_update_handler(qd, msg, entity_type, operation_type, identity_iter, name_iter);
                break;
            case QD_ROUTER_OPERATION_DELETE:
                qd_core_agent_delete_handler(qd, msg, entity_type, operation_type, identity_iter, name_iter);
                break;
       }
    }
    else
        qd_router_send2(qd, MANAGEMENT_INTERNAL, msg); //the C management agent is not going to handle this request. Forward it off to Python.
    // TODO - This is wrong. Need to find out how I can forward off the message to $management_internal so it can be handled by Python.

    qd_field_iterator_free(app_properties_iter);
    qd_field_iterator_free(name_iter);
    qd_field_iterator_free(identity_iter);

}
