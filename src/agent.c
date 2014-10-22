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

#include "dispatch_private.h"
#include <qpid/dispatch/error.h>
#include <qpid/dispatch/agent.h>
#include <qpid/dispatch/alloc.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/hash.h>
#include <qpid/dispatch/container.h>
#include <qpid/dispatch/message.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/timer.h>
#include <qpid/dispatch/router.h>
#include <qpid/dispatch/log.h>
#include <qpid/dispatch/compose.h>
#include <qpid/dispatch/parse.h>
#include <qpid/dispatch/amqp.h>
#include <string.h>
#include <stdio.h>

static qd_address_semantics_t agent_semantics = QD_FANOUT_SINGLE | QD_BIAS_CLOSEST | QD_CONGESTION_DROP | QD_DROP_FOR_SLOW_CONSUMERS;

struct qd_agent_class_t {
    DEQ_LINKS(qd_agent_class_t);
    qd_hash_handle_t           *hash_handle;
    void                       *context;
    const qd_agent_attribute_t *attributes;
    qd_agent_query_cb_t         query_handler;  // 0 iff class is an event.
};

DEQ_DECLARE(qd_agent_class_t, qd_agent_class_list_t);


struct qd_agent_t {
    qd_dispatch_t         *qd;
    qd_log_source_t       *log_source;
    qd_hash_t             *class_hash;
    qd_agent_class_list_t  class_list;
    qd_message_list_t      in_fifo;
    qd_message_list_t      out_fifo;
    sys_mutex_t           *lock;
    qd_timer_t            *timer;
    qd_address_t          *local_address;
    qd_address_t          *global_address;
    qd_agent_class_t      *container_class;
};


typedef struct {
    qd_agent_t             *agent;
    qd_composed_field_t    *response;
    uint32_t                index;
    uint32_t                count;
    uint32_t                offset;
    uint32_t                limit;
    const qd_agent_class_t *cls;
    int32_t                *attr_indices;
    uint32_t                attr_count;
} qd_agent_request_t;


typedef enum {
    QD_DISCOVER_TYPES,
    QD_DISCOVER_ATTRIBUTES,
    QD_DISCOVER_OPERATIONS
} qd_discover_t;

// Convenience for logging, expects agent to be defined.
#define LOG(LEVEL, MSG, ...) qd_log(agent->log_source, QD_LOG_##LEVEL, MSG, ##__VA_ARGS__)

static const char *AGENT_ADDRESS       = "$cmanagement";
static const char *STATUS_CODE         = "statusCode";
static const char *STATUS_DESCRIPTION  = "statusDescription";
static const char *AP_ENTITY_TYPE      = "entityType";
static const char *AP_OPERATION        = "operation";
static const char *AP_TYPE             = "type";
//static const char *AP_NAME             = "name";
//static const char *AP_IDENTITY         = "identity";
static const char *AP_OFFSET           = "offset";
static const char *AP_COUNT            = "count";
static const char *OP_QUERY            = "QUERY";
static const char *OP_GET_TYPES        = "GET-TYPES";
static const char *OP_GET_ATTRIBUTES   = "GET-ATTRIBUTES";
static const char *OP_GET_OPERATIONS   = "GET-OPERATIONS";
static const char *OP_GET_MGMT_NODES   = "GET-MGMT-NODES";
static const char *BODY_ATTR_NAMES     = "attributeNames";
static const char *BODY_RESULTS        = "results";

static const char *INVALID_ENTITY_TYPE="Invalid entityType";
static const char *NEED_BODY_MAP = "Expected map in body of the request";
static const char *NEED_ALIST    = "Expected attributeNames in body map";
static const char *ALL_DEFAULT   = "At least one of entityType or attributeNames must be provided";

#define ATTR_ABSENT 1000000
#define ATTR_TYPE   1000001


static qd_composed_field_t *qd_agent_setup_response(qd_field_iterator_t *reply_to, qd_field_iterator_t *cid, bool close_ap)
{
    qd_composed_field_t *field = 0;

    //
    // Compose the Properties
    //
    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_null(field);                       // message-id
    qd_compose_insert_null(field);                       // user-id
    qd_compose_insert_string_iterator(field, reply_to);  // to
    qd_compose_insert_null(field);                       // subject
    qd_compose_insert_null(field);                       // reply-to
    if (cid)
        qd_compose_insert_typed_iterator(field, cid);    // correlation-id
    //else
    //    qd_compose_insert_null(field);  // Uncomment this clause if more attributes are added after the correlation-id
    qd_compose_end_list(field);

    //
    // Compose the Application Properties
    //
    field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, field);
    qd_compose_start_map(field);
    qd_compose_insert_string(field, STATUS_CODE);
    qd_compose_insert_uint(field, QD_AMQP_OK.status);

    qd_compose_insert_string(field, STATUS_DESCRIPTION);
    qd_compose_insert_string(field, QD_AMQP_OK.description);

    if (close_ap)
        qd_compose_end_map(field);

    return field;
}


static qd_composed_field_t *qd_agent_setup_error(
    qd_agent_t* agent, qd_field_iterator_t *reply_to, qd_field_iterator_t *cid,
    qd_amqp_error_t error, const char *text)
{
    qd_composed_field_t *field = 0;

    //
    // Compose the Properties
    //
    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_null(field);                       // message-id
    qd_compose_insert_null(field);                       // user-id
    qd_compose_insert_string_iterator(field, reply_to);  // to
    qd_compose_insert_null(field);                       // subject
    qd_compose_insert_null(field);                       // reply-to
    if (cid)
        qd_compose_insert_typed_iterator(field, cid);    // correlation-id
    //else
    //    qd_compose_insert_null(field);  // Uncomment this clause if more attributes are added after the correlation-id
    qd_compose_end_list(field);

    //
    // Compose the Application Properties
    //
    field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, field);
    qd_compose_start_map(field);
    qd_compose_insert_string(field, STATUS_CODE);
    qd_compose_insert_uint(field, error.status);

    qd_compose_insert_string(field, STATUS_DESCRIPTION);
    char msg[QD_ERROR_MAX];
    snprintf(msg, sizeof(msg), "%s: %s", error.description, text);
    LOG(ERROR, "Management error: %s", msg);
    qd_compose_insert_string(field, msg);
    qd_compose_end_map(field);

    return field;
}


static void qd_agent_send_response(qd_agent_t *agent, qd_composed_field_t *field1, qd_composed_field_t *field2, qd_field_iterator_t *reply_to)
{
    qd_message_t *msg = qd_message();

    if (field2)
        qd_message_compose_3(msg, field1, field2);
    else
        qd_message_compose_2(msg, field1);

    qd_router_send(agent->qd, reply_to, msg);
    qd_message_free(msg);
    qd_compose_free(field1);
    if (field2)
        qd_compose_free(field2);
}

static void qd_agent_send_error(
    qd_agent_t *agent, qd_field_iterator_t *reply_to, qd_field_iterator_t *cid,
    qd_amqp_error_t code, const char *text)
{
    qd_agent_send_response(agent, qd_agent_setup_error(agent, reply_to, cid, code, text),
			   0, reply_to);
}

static void qd_agent_insert_attr_names(qd_composed_field_t    *field,
                                       const qd_agent_class_t *cls,
                                       qd_parsed_field_t      *attr_list)
{
    uint32_t count = attr_list ? qd_parse_sub_count(attr_list) : 0;
    uint32_t idx;

    if (count > 0)
        //
        // Simply echo the requested set of attributes
        //
        for (idx = 0; idx < count; idx++)
            qd_compose_insert_string_iterator(field, qd_parse_raw(qd_parse_sub_value(attr_list, idx)));
    else {
        //
        // No requested set of attributes, use the set for the supplied class.
        //
        assert(cls);
        qd_compose_insert_string(field, AP_TYPE);
        for (idx = 0; cls->attributes[idx].name; idx++)
            qd_compose_insert_string(field, cls->attributes[idx].name);
    }
}


static void qd_agent_query_one_class(const qd_agent_class_t *cls,
                                     qd_agent_request_t     *request,
                                     qd_parsed_field_t      *attr_list,
                                     uint32_t               *index,
                                     uint32_t                offset)
{
    //
    // Build the attribute index list for this class.
    //
    uint32_t list_count = attr_list ? qd_parse_sub_count(attr_list) : 0;
    uint32_t count      = list_count;
    uint32_t idx;

    if (count == 0) {
        for (idx = 0; cls->attributes[idx].name; idx++)
            count++;
        count++;  // Account for the type attribute
    }

    {
        int32_t attr_indices[count];

        if (list_count == 0) {
            attr_indices[0] = ATTR_TYPE;
            for (idx = 0; idx < count; idx++)
                attr_indices[idx + 1] = idx;
        } else {
            for (idx = 0; idx < count; idx++) {
                qd_field_iterator_t *attr = qd_parse_raw(qd_parse_sub_value(attr_list, idx));
                attr_indices[idx] = ATTR_ABSENT;
                for (int jdx = 0; cls->attributes[jdx].name; jdx++) {
                    if (qd_field_iterator_equal(attr, (const unsigned char*) cls->attributes[jdx].name)) {
                        attr_indices[idx] = jdx;
                        break;
                    } else if (qd_field_iterator_equal(attr, (const unsigned char*) AP_TYPE)) {
                        attr_indices[idx] = ATTR_TYPE;
                        break;
                    }
                }
            }
        }

        request->cls          = cls;
        request->attr_indices = attr_indices;
        request->attr_count   = count;

        //
        // Invoke the class's query handler.
        //
        cls->query_handler(cls->context, request);
    }
}


bool qd_agent_object(void *correlator, void *object_handle)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;

    //
    // Don't produce any output until we reach the offset.
    //
    if (request->index++ < request->offset)
        return true;

    //
    // Return false if there's a limit and we've reached it.
    //
    if (request->limit > 0 && request->count >= request->limit)
        return false;

    request->count++;

    qd_agent_value_start_list(correlator, 0);
    for (uint32_t idx = 0; idx < request->attr_count; idx++) {
        if (request->attr_indices[idx] == ATTR_ABSENT)
            qd_agent_value_null(correlator, 0);
        else if (request->attr_indices[idx] == ATTR_TYPE)
            qd_agent_value_string(correlator, 0, (const char*) qd_hash_key_by_handle(request->cls->hash_handle));
        else {
            const qd_agent_attribute_t *attr = &(request->cls->attributes[request->attr_indices[idx]]);
            attr->handler(object_handle, correlator, attr->context);
        }
    }
    qd_agent_value_end_list(correlator);

    return true;
}


static void qd_agent_process_object_query(qd_agent_t          *agent,
                                          qd_parsed_field_t   *map,
                                          qd_field_iterator_t *body,
                                          qd_field_iterator_t *reply_to,
                                          qd_field_iterator_t *cid)
{
    qd_parsed_field_t      *etype_field  = qd_parse_value_by_key(map, AP_ENTITY_TYPE);
    qd_parsed_field_t      *offset_field = qd_parse_value_by_key(map, AP_OFFSET);
    qd_parsed_field_t      *count_field  = qd_parse_value_by_key(map, AP_COUNT);
    qd_parsed_field_t      *body_map     = 0;
    qd_parsed_field_t      *attr_list    = 0;
    qd_composed_field_t    *hdr_field    = 0;
    qd_composed_field_t    *body_field   = 0;
    const qd_agent_class_t *cls_record   = 0;

    uint32_t index  = 0;
    uint32_t count  = 0;
    uint32_t offset = 0;
    uint32_t limit  = 0;

    do {
        //
        // The body of the request must be present
        //
        if (!body) {
            qd_agent_send_error(agent, reply_to, cid, QD_AMQP_BAD_REQUEST, NEED_BODY_MAP);
            break;
        }

        //
        // The body of the request must be a map.
        //
        body_map = qd_parse(body);
        if (!body_map || !qd_parse_ok(body_map) || !qd_parse_is_map(body_map)) {
            qd_agent_send_error(agent, reply_to, cid, QD_AMQP_BAD_REQUEST, NEED_BODY_MAP);
            break;
        }

        //
        // The map in the request body must have an element with key 'attributeNames' and the value
        // of that key must be a list.
        //
        attr_list = qd_parse_value_by_key(body_map, BODY_ATTR_NAMES);
        if (!attr_list || !qd_parse_ok(attr_list) || !qd_parse_is_list(attr_list)) {
            qd_agent_send_error(agent, reply_to, cid, QD_AMQP_BAD_REQUEST, NEED_ALIST);
            break;
        }

        //
        // Restriction of this implementation:  If the requested attribute list is empty
        // (requesting all attributes) AND the entityType field is absent (requesting
        // all entity types), we will not accept this request.  One or the other must be
        // specified in the request.
        //
        if (qd_parse_sub_count(attr_list) == 0 && !etype_field) {
            qd_agent_send_error(agent, reply_to, cid, QD_AMQP_BAD_REQUEST, ALL_DEFAULT);
            break;
        }

        //
        // If there is an entityType specified, see if it matches one of our classes.
        // If it does, set cls_record to the specified class.  If it was not specified,
        // leave cls_record as NULL and we will consider all classes.
        //
        if (etype_field) {
            qd_field_iterator_t *cls_string = qd_parse_raw(etype_field);
            qd_hash_retrieve_const(agent->class_hash, cls_string, (const void**) &cls_record);

            //
            // If the entityType was specified but not found, return an error.
            //
            if (cls_record == 0) {
		char entity[QD_ERROR_MAX];
		qd_field_iterator_strncpy(cls_string, entity, sizeof(entity));
                qd_agent_send_error(agent, reply_to, cid, QD_AMQP_NOT_FOUND, entity);
                break;
            }
        }

        //
        // Set up two composed fields, one for the headers and one for the body.  This is necessary
        // because the 'count' field in the headers won't be known until we finish building the body.
        //
        hdr_field  = qd_agent_setup_response(reply_to, cid, false);
        body_field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

        //
        // Echo the request headers to the reply (except for count, which we'll do later)
        //
        if (etype_field) {
            qd_compose_insert_string(hdr_field, AP_ENTITY_TYPE);
            qd_compose_insert_string_iterator(hdr_field, qd_parse_raw(etype_field));
        }

        if (offset_field) {
            offset = qd_parse_as_uint(offset_field);
            qd_compose_insert_string(hdr_field, AP_OFFSET);
            qd_compose_insert_uint(hdr_field, offset);
        }

        if (count_field)
            limit = qd_parse_as_uint(count_field);

        //
        // Build the attribute list in the response.
        //
        qd_compose_start_map(body_field);
        qd_compose_insert_string(body_field, BODY_ATTR_NAMES);
        qd_compose_start_list(body_field);
        qd_agent_insert_attr_names(body_field, cls_record, attr_list);
        qd_compose_end_list(body_field);

        //
        // Build the result list of lists.
        //
        qd_compose_insert_string(body_field, BODY_RESULTS);
        qd_compose_start_list(body_field);

        //
        // The request record is allocated locally because the entire processing of the request
        // will be done synchronously.
        //
        qd_agent_request_t request;
        request.agent    = agent;
        request.response = body_field;
        request.index    = 0;
        request.count    = count;
        request.offset   = offset;
        request.limit    = limit;

        if (cls_record)
            qd_agent_query_one_class(cls_record, &request, attr_list, &index, offset);
        else
            for (cls_record = DEQ_HEAD(agent->class_list);
                 cls_record && count < limit;
                 cls_record = DEQ_NEXT(cls_record)) {
                qd_agent_query_one_class(cls_record, &request, attr_list, &index, offset);
            }

        //
        // The response is complete, close the outer list.
        //
        qd_compose_end_list(body_field);
        qd_compose_end_map(body_field);

        //
        // Now add the count to the header field.
        //
        qd_compose_insert_string(hdr_field, AP_COUNT);
        qd_compose_insert_uint(hdr_field, request.count);
        qd_compose_end_map(hdr_field);

        //
        // Create a message and send it.
        //
        qd_agent_send_response(agent, hdr_field, body_field, reply_to);
    } while (false);

    qd_parse_free(body_map);
}


static void qd_agent_process_agent_query(qd_agent_t          *agent,
                                         qd_parsed_field_t   *map,
                                         qd_field_iterator_t *reply_to,
                                         qd_field_iterator_t *cid,
                                         qd_discover_t        kind)
{
    switch (kind) {
    case QD_DISCOVER_TYPES:
        qd_log(agent->log_source, QD_LOG_TRACE, "Received GET-TYPES request");
        break;

    case QD_DISCOVER_ATTRIBUTES:
        qd_log(agent->log_source, QD_LOG_TRACE, "Received GET-ATTRIBUTES request");
        break;

    case QD_DISCOVER_OPERATIONS:
        qd_log(agent->log_source, QD_LOG_TRACE, "Received GET-OPERATIONS request");
        break;
    }

    qd_parsed_field_t   *etype = qd_parse_value_by_key(map, AP_ENTITY_TYPE);
    qd_composed_field_t *field = 0;

    if (etype && !qd_parse_is_scalar(etype))
        field = qd_agent_setup_error(agent, reply_to, cid, QD_AMQP_BAD_REQUEST, INVALID_ENTITY_TYPE);
    else {
        field = qd_agent_setup_response(reply_to, cid, true);

        //
        // Open the Body (AMQP Value) to be filled in by the handler.
        //
        field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, field);
        qd_compose_start_map(field);

        //
        // Put entries into the map for each known entity type
        //
        sys_mutex_lock(agent->lock);
        qd_agent_class_t *cls = DEQ_HEAD(agent->class_list);
        for (; cls; cls = DEQ_NEXT(cls)) {
            const char *class_name = (const char*) qd_hash_key_by_handle(cls->hash_handle);

            //
            // If entityType was provided to restrict the result, check to see if the
            // class is in the restricted set.
            //
            if (etype && !qd_field_iterator_equal(qd_parse_raw(etype), (unsigned char*) class_name))
                continue;

            qd_compose_insert_string(field, class_name);
            switch (kind) {
            case QD_DISCOVER_TYPES:
                qd_compose_empty_list(field); // The list of supertypes implemented by this type.
                break;

            case QD_DISCOVER_ATTRIBUTES: {
                qd_compose_start_list(field);
                for (int idx = 0; cls->attributes[idx].name; idx++)
                    qd_compose_insert_string(field, cls->attributes[idx].name);
                qd_compose_end_list(field);
                break;
            }

            case QD_DISCOVER_OPERATIONS:
                qd_compose_start_list(field);
                qd_compose_insert_string(field, OP_QUERY);
                qd_compose_end_list(field);
                break;
            }
        }

        sys_mutex_unlock(agent->lock);
        qd_compose_end_map(field);
    }

    //
    // Send the response to the requestor
    //
    qd_agent_send_response(agent, field, 0, reply_to);
}


static void qd_agent_process_discover_nodes(qd_agent_t          *agent,
                                            qd_parsed_field_t   *map,
                                            qd_field_iterator_t *reply_to,
                                            qd_field_iterator_t *cid)
{
    qd_log(agent->log_source, QD_LOG_TRACE, "Received GET-MGMT-NODES request");

    qd_composed_field_t *field = qd_agent_setup_response(reply_to, cid, true);

    //
    // Open the Body (AMQP Value) to be filled in by the handler.
    //
    field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, field);

    //
    // Put entries into the list for each known management node
    //
    qd_compose_start_list(field);
    qd_router_build_node_list(agent->qd, field);
    qd_compose_end_list(field);

    //
    // Create a message and send it.
    //
    qd_agent_send_response(agent, field, 0, reply_to);
}


static void qd_agent_process_request(qd_agent_t *agent, qd_message_t *msg)
{
    //
    // Parse the message through the body and exit if the message is not well formed.
    //
    if (!qd_message_check(msg, QD_DEPTH_BODY)) {
	LOG(ERROR, "Cannot parse request: %s", qd_error_message());
	return;
    }

    //
    // Get an iterator for the reply-to.  Exit if not found.
    //
    qd_field_iterator_t *reply_to = qd_message_field_iterator(msg, QD_FIELD_REPLY_TO);
    if (reply_to == 0) {
	LOG(ERROR, "Reqeust has no reply-to");
        return;
    }

    //
    // Get an iterator for the correlation_id.
    //
    qd_field_iterator_t *cid = qd_message_field_iterator_typed(msg, QD_FIELD_CORRELATION_ID);

    //
    // Get an iterator for the application-properties.  Exit if the message has none.
    //
    qd_field_iterator_t *ap = qd_message_field_iterator(msg, QD_FIELD_APPLICATION_PROPERTIES);
    if (ap == 0) {
	qd_agent_send_error(agent, reply_to, cid, QD_AMQP_BAD_REQUEST, "No application-properties");
	return;
    }

    //
    // Try to get a map-view of the application-properties.
    //
    qd_parsed_field_t *map = qd_parse(ap);
    if (map == 0) {
        qd_field_iterator_free(ap);
        qd_field_iterator_free(reply_to);
	qd_agent_send_error(agent, reply_to, cid, QD_AMQP_BAD_REQUEST, "Application-properties not a map");
        return;
    }

    //
    // Exit if there was a parsing error or the application-properties is not a map.
    //
    if (!qd_parse_ok(map) || !qd_parse_is_map(map)) {
        qd_field_iterator_free(ap);
        qd_field_iterator_free(reply_to);
        qd_parse_free(map);
	qd_agent_send_error(agent, reply_to, cid, QD_AMQP_BAD_REQUEST, "Application-properties not a map");
        return;
    }

    //
    // Get an iterator for the "operation" field in the map.  Exit if the key is not found.
    //
    qd_parsed_field_t *operation = qd_parse_value_by_key(map, AP_OPERATION);
    if (operation == 0) {
        qd_parse_free(map);
        qd_field_iterator_free(ap);
        qd_field_iterator_free(reply_to);
	qd_agent_send_error(agent, reply_to, cid, QD_AMQP_BAD_REQUEST, "No operation");
        return;
    }

    //
    // Dispatch the operation to the appropriate handler
    //
    qd_field_iterator_t *operation_string = qd_parse_raw(operation);
    if      (qd_field_iterator_equal(operation_string, (unsigned char*) OP_QUERY)) {
        qd_field_iterator_t *body = qd_message_field_iterator(msg, QD_FIELD_BODY);
        qd_agent_process_object_query(agent, map, body, reply_to, cid);
        if (body)
            qd_field_iterator_free(body);
    }
    else if (qd_field_iterator_equal(operation_string, (unsigned char*) OP_GET_TYPES))
        qd_agent_process_agent_query(agent, map, reply_to, cid, QD_DISCOVER_TYPES);
    else if (qd_field_iterator_equal(operation_string, (unsigned char*) OP_GET_ATTRIBUTES))
        qd_agent_process_agent_query(agent, map, reply_to, cid, QD_DISCOVER_ATTRIBUTES);
    else if (qd_field_iterator_equal(operation_string, (unsigned char*) OP_GET_OPERATIONS))
        qd_agent_process_agent_query(agent, map, reply_to, cid, QD_DISCOVER_OPERATIONS);
    else if (qd_field_iterator_equal(operation_string, (unsigned char*) OP_GET_MGMT_NODES))
        qd_agent_process_discover_nodes(agent, map, reply_to, cid);
    else {
	char op[QD_ERROR_MAX];
	qd_field_iterator_strncpy(operation_string, op, sizeof(op));
        qd_agent_send_error(agent, reply_to, cid, QD_AMQP_NOT_IMPLEMENTED, op);
    }

    qd_parse_free(map);
    qd_field_iterator_free(ap);
    qd_field_iterator_free(reply_to);
    if (cid)
        qd_field_iterator_free(cid);
}


static void qd_agent_deferred_handler(void *context)
{
    qd_agent_t   *agent = (qd_agent_t*) context;
    qd_message_t *msg;

    do {
        sys_mutex_lock(agent->lock);
        msg = DEQ_HEAD(agent->in_fifo);
        if (msg)
            DEQ_REMOVE_HEAD(agent->in_fifo);
        sys_mutex_unlock(agent->lock);

        if (msg) {
            qd_agent_process_request(agent, msg);
            qd_message_free(msg);
        }
    } while (msg);
}


static void qd_agent_rx_handler(void *context, qd_message_t *msg, int unused_link_id)
{
    qd_agent_t   *agent = (qd_agent_t*) context;
    qd_message_t *copy  = qd_message_copy(msg);

    sys_mutex_lock(agent->lock);
    DEQ_INSERT_TAIL(agent->in_fifo, copy);
    sys_mutex_unlock(agent->lock);

    qd_timer_schedule(agent->timer, 0);
}


static qd_agent_class_t *qd_agent_register_class_LH(qd_agent_t                 *agent,
                                                    const char                 *fqname,
                                                    void                       *context,
                                                    const qd_agent_attribute_t *attributes,
                                                    qd_agent_query_cb_t         query_handler)
{
    qd_agent_class_t *cls = NEW(qd_agent_class_t);
    assert(cls);
    DEQ_ITEM_INIT(cls);
    cls->context       = context;
    cls->attributes    = attributes;
    cls->query_handler = query_handler;

    qd_field_iterator_t *iter = qd_field_iterator_string(fqname, ITER_VIEW_ALL);
    int result = qd_hash_insert_const(agent->class_hash, iter, cls, &cls->hash_handle);
    qd_field_iterator_free(iter);
    if (result < 0)
        assert(false);

    DEQ_INSERT_TAIL(agent->class_list, cls);

    qd_log(agent->log_source, QD_LOG_DEBUG, "Manageable Entity Type (%s) %s", query_handler ? "object" : "event", fqname);
    return cls;
}


qd_agent_t *qd_agent(qd_dispatch_t *qd)
{
    qd_agent_t *agent = NEW(qd_agent_t);
    agent->log_source = qd_log_source("CAGENT");
    agent->qd         = qd;
    agent->class_hash = qd_hash(6, 10, 1);
    DEQ_INIT(agent->class_list);
    DEQ_INIT(agent->in_fifo);
    DEQ_INIT(agent->out_fifo);
    agent->lock  = sys_mutex();
    agent->timer = qd_timer(qd, qd_agent_deferred_handler, agent);
    agent->local_address  = qd_router_register_address(qd, AGENT_ADDRESS, qd_agent_rx_handler, agent_semantics, false, agent);
    agent->global_address = qd_router_register_address(qd, AGENT_ADDRESS, qd_agent_rx_handler, agent_semantics, true, agent);

    return agent;
}


void qd_agent_free(qd_agent_t *agent)
{
    if (!agent) return;
    qd_agent_class_t *cls = DEQ_HEAD(agent->class_list);
    while (cls) {
        DEQ_REMOVE_HEAD(agent->class_list);
        qd_hash_handle_free(cls->hash_handle);
        free(cls);
        cls = DEQ_HEAD(agent->class_list);
    }

    qd_router_unregister_address(agent->local_address);
    qd_router_unregister_address(agent->global_address);
    sys_mutex_free(agent->lock);
    qd_timer_free(agent->timer);
    qd_hash_free(agent->class_hash);
    free(agent);
}


qd_agent_class_t *qd_agent_register_class(qd_dispatch_t              *qd,
                                          const char                 *fqname,
                                          void                       *context,
                                          const qd_agent_attribute_t *attributes,
                                          qd_agent_query_cb_t         query_handler)
{
    qd_agent_t       *agent = qd->agent;
    qd_agent_class_t *cls;

    sys_mutex_lock(agent->lock);
    cls = qd_agent_register_class_LH(agent, fqname, context, attributes, query_handler);
    sys_mutex_unlock(agent->lock);
    return cls;
}


void qd_agent_value_string(void *correlator, const char *key, const char *value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_string(request->response, value);
}


void qd_agent_value_uint(void *correlator, const char *key, uint64_t value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_uint(request->response, value);
}


void qd_agent_value_null(void *correlator, const char *key)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_null(request->response);
}


void qd_agent_value_boolean(void *correlator, const char *key, bool value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_bool(request->response, value);
}


void qd_agent_value_binary(void *correlator, const char *key, const uint8_t *value, size_t len)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_binary(request->response, value, len);
}


void qd_agent_value_uuid(void *correlator, const char *key, const uint8_t *value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_uuid(request->response, value);
}


void qd_agent_value_timestamp(void *correlator, const char *key, uint64_t value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_timestamp(request->response, value);
}


void qd_agent_value_start_list(void *correlator, const char *key)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_start_list(request->response);
}


void qd_agent_value_end_list(void *correlator)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_compose_end_list(request->response);
}


void qd_agent_value_start_map(void *correlator, const char *key)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_start_map(request->response);
}


void qd_agent_value_end_map(void *correlator)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_compose_end_map(request->response);
}


void *qd_agent_raise_event(qd_dispatch_t *qd, qd_agent_class_t *event)
{
    return 0;
}
