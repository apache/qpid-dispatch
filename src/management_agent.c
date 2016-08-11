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
#include <qpid/dispatch/connection_manager.h>
#include "dispatch_private.h"
#include "agent.h"

const char *ENTITY = "entityType";
const char *TYPE = "type";
const char *COUNT = "count";
const char *OFFSET = "offset";
const char *NAME = "name";
const char *IDENTITY = "identity";


const char *OPERATION = "operation";
const char *ATTRIBUTE_NAMES = "attributeNames";

const unsigned char *config_address_entity_type = (unsigned char*) "org.apache.qpid.dispatch.router.config.address";
const unsigned char *link_route_entity_type     = (unsigned char*) "org.apache.qpid.dispatch.router.config.linkRoute";
const unsigned char *auto_link_entity_type      = (unsigned char*) "org.apache.qpid.dispatch.router.config.autoLink";
const unsigned char *address_entity_type        = (unsigned char*) "org.apache.qpid.dispatch.router.address";
const unsigned char *link_entity_type           = (unsigned char*) "org.apache.qpid.dispatch.router.link";
const unsigned char *console_entity_type        = (unsigned char*) "org.apache.qpid.dispatch.console";

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

typedef struct qd_management_context_t {
    qd_message_t                *response;
    qd_composed_field_t         *out_body;
    qd_field_iterator_t         *reply_to;
    qd_field_iterator_t         *correlation_id;
    qdr_query_t                 *query;
    qdr_core_t                  *core;
    int                          count;
    int                          current_count;
    qd_schema_entity_operation_t operation_type;
} qd_management_context_t ;

ALLOC_DECLARE(qd_management_context_t);
ALLOC_DEFINE(qd_management_context_t);


/**
 * Convenience function to create and initialize context (qd_management_context_t)
 */
static qd_management_context_t* qd_management_context(qd_field_iterator_t          *reply_to,
                                                      qd_field_iterator_t          *correlation_id,
                                                      qdr_query_t                  *query,
                                                      qdr_core_t                   *core,
                                                      qd_schema_entity_operation_t operation_type,
                                                      int                          count)
{
    qd_management_context_t *ctx = new_qd_management_context_t();
    ctx->response       = qd_message();
    ctx->out_body       = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);
    ctx->reply_to       = reply_to;
    ctx->correlation_id = correlation_id;
    ctx->query          = query;
    ctx->core           = core;
    ctx->count          = count;
    ctx->current_count  = 0;
    ctx->operation_type = operation_type;

    return ctx;
}

static void qd_parse_agent_request(qd_agent_request_t       *request,
                                   qd_schema_entity_type_t  *entity_type,
                                   qd_field_iterator_t      **cid,
                                   qd_field_iterator_t      **reply_to,
                                   qd_field_iterator_t      **name_iter,
                                   qd_field_iterator_t      **identity_iter,
                                   qd_parsed_field_t        **body)
{
    qd_buffer_list_t *buffer_list = get_request_buffers(request);
    qd_field_iterator_t *iter = qd_address_iterator_buffer(DEQ_HEAD(*buffer_list), 0, qd_buffer_list_length(buffer_list), ITER_VIEW_ALL);
    *entity_type = get_request_entity_type(request);
    *cid = qd_parse_raw(qd_parse(iter));
    *reply_to = qd_parse_raw(qd_parse(iter));
    *name_iter = qd_parse_raw(qd_parse(iter));
    *identity_iter = qd_parse_raw(qd_parse(iter));
    *body = qd_parse(iter);
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


static void qd_set_properties(qd_field_iterator_t *correlation_id,
                              qd_field_iterator_t *reply_to,
                              qd_composed_field_t **fld)
{
    // Set the correlation_id and reply_to on fld
    *fld = qd_compose(QD_PERFORMATIVE_PROPERTIES, 0);
    qd_compose_start_list(*fld);
    qd_compose_insert_null(*fld);                           // message-id
    qd_compose_insert_null(*fld);                           // user-id
    qd_compose_insert_string_iterator(*fld, reply_to);     // to
    qd_compose_insert_null(*fld);                           // subject
    qd_compose_insert_null(*fld);
    qd_compose_insert_typed_iterator(*fld, correlation_id);
    qd_compose_end_list(*fld);
    qd_field_iterator_free(correlation_id);
}


void send_response(void *ctx,
                   qd_field_iterator_t *reply_to,
                   qd_field_iterator_t *correlation_id,
                   const qd_amqp_error_t *status,
                   qd_composed_field_t *out_body)
{
    qd_composed_field_t *fld = 0;

    // Start composing the message.
    // First set the properties on the message like reply_to, correlation-id etc.
    qd_set_properties(correlation_id, reply_to, &fld);

    // Second, set the status on the message, QD_AMQP_OK or QD_AMQP_BAD_REQUEST and so on.
    qd_set_response_status(status, &fld);

    qd_message_t *response = qd_message();
    qdr_core_t *core = (qdr_core_t*)ctx; // This is not right since this is also used by non-core stuff

    // Finally, compose and send the message.
    qd_message_compose_3(response, fld, out_body);
    qdr_send_to1(core, response, reply_to, true, false);

    qd_compose_free(fld);

}

void qd_create_ssl_profile(qd_agent_request_t *request)
{
    qd_schema_entity_type_t entity_type;
    qd_field_iterator_t     *correlation_id;
    qd_field_iterator_t     *reply_to;
    qd_field_iterator_t     *name_iter;
    qd_field_iterator_t     *identity_iter;
    qd_parsed_field_t       *in_body;

    qd_parse_agent_request(request, &entity_type, &correlation_id, &reply_to, &name_iter, &identity_iter, &in_body);

    qd_dispatch_t *qd = (qd_dispatch_t*)get_request_context(request);

    qd_composed_field_t *out_body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    qd_amqp_error_t status = qd_connection_manager_create_ssl_profile(qd, name_iter, in_body, out_body);

    send_response(get_request_context(request), reply_to, correlation_id, &status, out_body);
}


void qd_query_ssl_profile(qd_agent_request_t *request)
{
    qd_schema_entity_type_t entity_type;
    qd_field_iterator_t     *correlation_id;
    qd_field_iterator_t     *reply_to;
    qd_field_iterator_t     *name_iter;
    qd_field_iterator_t     *identity_iter;
    qd_parsed_field_t       *in_body;

    qd_parse_agent_request(request, &entity_type, &correlation_id, &reply_to, &name_iter, &identity_iter, &in_body);

    qd_dispatch_t *qd = (qd_dispatch_t*)get_request_context(request);

    qd_composed_field_t *out_body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    qd_amqp_error_t status = qd_connection_manager_query_ssl_profile(qd, get_request_offset(request), get_request_count(request), in_body, out_body);

    send_response(get_request_context(request), reply_to, correlation_id, &status, out_body);
}


void qd_delete_ssl_profile(qd_agent_request_t *request)
{
    qd_schema_entity_type_t entity_type;
    qd_field_iterator_t     *correlation_id;
    qd_field_iterator_t     *reply_to;
    qd_field_iterator_t     *name_iter;
    qd_field_iterator_t     *identity_iter;
    qd_parsed_field_t       *in_body;

    qd_parse_agent_request(request, &entity_type, &correlation_id, &reply_to, &name_iter, &identity_iter, &in_body);
}

static void qd_manage_response_handler(void *context, const qd_amqp_error_t *status, bool more)
{
    qd_management_context_t *mgmt_ctx = (qd_management_context_t*) context;

    if (mgmt_ctx->operation_type == QD_SCHEMA_ENTITY_OPERATION_QUERY) {
        if (status->status / 100 == 2) { // There is no error, proceed to conditionally call get_next
            if (more) {
               mgmt_ctx->current_count++; // Increment how many you have at hand
               if (mgmt_ctx->count != mgmt_ctx->current_count) {
                   qdr_query_get_next(mgmt_ctx->query);
                   return;
               } else
                   //
                   // This is the one case where the core agent won't free the query itself.
                   //
                   qdr_query_free(mgmt_ctx->query);
            }
        }
        qd_compose_end_list(mgmt_ctx->out_body);
        qd_compose_end_map(mgmt_ctx->out_body);
    }
    else if (mgmt_ctx->operation_type == QD_SCHEMA_ENTITY_OPERATION_DELETE) {
        // The body of the delete response message MUST consist of an amqp-value section containing a Map with zero entries.
        qd_compose_start_map(mgmt_ctx->out_body);
        qd_compose_end_map(mgmt_ctx->out_body);
    }
    else if (mgmt_ctx->operation_type == QD_SCHEMA_ENTITY_OPERATION_READ) {
        if (status->status / 100 != 2) {
            qd_compose_start_map(mgmt_ctx->out_body);
            qd_compose_end_map(mgmt_ctx->out_body);
        }
    }

    //Send the response to the caller
    send_response(mgmt_ctx->core, mgmt_ctx->reply_to, mgmt_ctx->correlation_id, status, mgmt_ctx->out_body);

    qd_message_free(mgmt_ctx->response);
    qd_compose_free(mgmt_ctx->out_body);

    free_qd_management_context_t(mgmt_ctx);
}


void qd_core_agent_query_handler(qd_agent_request_t *request)
{
    qd_schema_entity_type_t  entity_type;
    qd_field_iterator_t     *correlation_id;
    qd_field_iterator_t     *reply_to;
    qd_field_iterator_t     *name_iter;
    qd_field_iterator_t     *identity_iter;
    qd_parsed_field_t       *in_body;

    qd_parse_agent_request(request, &entity_type, &correlation_id, &reply_to, &name_iter, &identity_iter, &in_body);
    int count = get_request_count(request);
    int offset = get_request_offset(request);
    qdr_core_t *core = (qdr_core_t*)get_request_context(request);

    // Call local function that creates and returns a local qd_management_context_t object containing the values passed in.
    qd_management_context_t *mgmt_ctx = qd_management_context(reply_to, correlation_id, 0, core, QD_SCHEMA_ENTITY_OPERATION_QUERY, count);

    //
    // Add the Body.
    //
    qd_composed_field_t *field = mgmt_ctx->out_body;

    // Start a map in the body. Look for the end map in the callback function, qd_manage_response_handler.
    qd_compose_start_map(field);

    //add a "attributeNames" key
    qd_compose_insert_string(field, ATTRIBUTE_NAMES);

    // Grab the attribute names from the incoming message body. The attribute names will be used later on in the response.
    qd_parsed_field_t *attribute_names_parsed_field = 0;

    if (in_body != 0 && qd_parse_is_map(in_body)) {
        attribute_names_parsed_field = qd_parse_value_by_key(in_body, ATTRIBUTE_NAMES);
    }

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);
    mgmt_ctx->query = qdr_manage_query(core, mgmt_ctx, entity_type, attribute_names_parsed_field, field);

    //Add the attribute names
    qdr_query_add_attribute_names(mgmt_ctx->query); //this adds a list of attribute names like ["attribute1", "attribute2", "attribute3", "attribute4",]
    qd_compose_insert_string(field, results); //add a "results" key
    qd_compose_start_list(field); //start the list for results

    qdr_query_get_first(mgmt_ctx->query, offset);

    qd_parse_free(in_body);
}

void qd_core_agent_create_handler(qd_agent_request_t *request)
{
    qd_schema_entity_type_t entity_type;
    qd_field_iterator_t     *correlation_id;
    qd_field_iterator_t     *reply_to;
    qd_field_iterator_t     *name_iter;
    qd_field_iterator_t     *identity_iter;
    qd_parsed_field_t       *in_body;

    qd_parse_agent_request(request, &entity_type, &correlation_id, &reply_to, &name_iter, &identity_iter, &in_body);

    qdr_core_t *core = (qdr_core_t*)get_request_context(request);
    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *mgmt_ctx = qd_management_context(reply_to, correlation_id, 0, core, QD_SCHEMA_ENTITY_OPERATION_CREATE, 0);

    qdr_manage_create(core, mgmt_ctx, entity_type, name_iter, in_body, mgmt_ctx->out_body);

}

void qd_core_agent_read_handler(qd_agent_request_t *request)
{
    qd_schema_entity_type_t  entity_type;
    qd_field_iterator_t     *correlation_id;
    qd_field_iterator_t     *reply_to;
    qd_field_iterator_t     *name_iter;
    qd_field_iterator_t     *identity_iter;
    qd_parsed_field_t       *in_body;

    qd_parse_agent_request(request, &entity_type, &correlation_id, &reply_to, &name_iter, &identity_iter, &in_body);

    qdr_core_t *core = (qdr_core_t*)get_request_context(request);

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *mgmt_ctx = qd_management_context(reply_to, correlation_id, 0, core, QD_SCHEMA_ENTITY_OPERATION_READ, 0);

    //Call the read API function
    qdr_manage_read(core, mgmt_ctx, entity_type, name_iter, identity_iter, mgmt_ctx->out_body);
}


void qd_core_agent_update_handler(qd_agent_request_t *request)
{
    qd_schema_entity_type_t  entity_type;
    qd_field_iterator_t     *correlation_id;
    qd_field_iterator_t     *reply_to;
    qd_field_iterator_t     *name_iter;
    qd_field_iterator_t     *identity_iter;
    qd_parsed_field_t       *in_body;

    qd_parse_agent_request(request, &entity_type, &correlation_id, &reply_to, &name_iter, &identity_iter, &in_body);

    qdr_core_t *core = (qdr_core_t*)get_request_context(request);

    qd_management_context_t *mgmt_ctx = qd_management_context(reply_to, correlation_id, 0, core, QD_SCHEMA_ENTITY_OPERATION_UPDATE, 0);

    qdr_manage_update(core, mgmt_ctx, entity_type, name_iter, identity_iter, in_body, mgmt_ctx->out_body);
}


void qd_core_agent_delete_handler(qd_agent_request_t *request)
{
    qd_schema_entity_type_t  entity_type;
    qd_field_iterator_t     *correlation_id;
    qd_field_iterator_t     *reply_to;
    qd_field_iterator_t     *name_iter;
    qd_field_iterator_t     *identity_iter;
    qd_parsed_field_t       *in_body;

    qd_parse_agent_request(request, &entity_type, &correlation_id, &reply_to, &name_iter, &identity_iter, &in_body);

    qdr_core_t *core = (qdr_core_t*)get_request_context(request);

    // Set the callback function.
    qdr_manage_handler(core, qd_manage_response_handler);

    // Call local function that creates and returns a qd_management_context_t containing the values passed in.
    qd_management_context_t *mgmt_ctx = qd_management_context(reply_to, correlation_id, 0, core, QD_SCHEMA_ENTITY_OPERATION_DELETE, 0);

    qdr_manage_delete(core, mgmt_ctx, entity_type, name_iter, identity_iter);
}
