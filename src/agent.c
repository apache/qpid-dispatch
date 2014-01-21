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
    qd_hash_handle_t     *hash_handle;
    void                 *context;
    qd_agent_schema_cb_t  schema_handler;
    qd_agent_query_cb_t   query_handler;  // 0 iff class is an event.
};

DEQ_DECLARE(qd_agent_class_t, qd_agent_class_list_t);


struct qd_agent_t {
    qd_dispatch_t         *qd;
    qd_hash_t             *class_hash;
    qd_agent_class_list_t  class_list;
    qd_message_list_t      in_fifo;
    qd_message_list_t      out_fifo;
    sys_mutex_t           *lock;
    qd_timer_t            *timer;
    qd_address_t          *address;
    qd_agent_class_t      *container_class;
};


typedef struct {
    qd_agent_t          *agent;
    qd_composed_field_t *response;
    int                  empty;
} qd_agent_request_t;


static char *log_module = "AGENT";


static void qd_agent_check_empty(qd_agent_request_t *request)
{
    if (request->empty) {
        request->empty = 0;
        qd_compose_start_map(request->response);
    }
}


static qd_composed_field_t *qd_agent_setup_response(qd_field_iterator_t *reply_to, qd_field_iterator_t *cid)
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
    //    qd_compose_insert_null(field);
    qd_compose_end_list(field);

    //
    // Compose the Application Properties
    //
    field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, field);
    qd_compose_start_map(field);
    qd_compose_insert_string(field, "status-code");
    qd_compose_insert_uint(field, 200);

    qd_compose_insert_string(field, "status-descriptor");
    qd_compose_insert_string(field, "OK");
    qd_compose_end_map(field);

    return field;
}


static void qd_agent_process_get(qd_agent_t          *agent,
                                 qd_parsed_field_t   *map,
                                 qd_field_iterator_t *reply_to,
                                 qd_field_iterator_t *cid)
{
    qd_parsed_field_t *cls = qd_parse_value_by_key(map, "type");
    if (cls == 0)
        return;

    qd_field_iterator_t    *cls_string = qd_parse_raw(cls);
    const qd_agent_class_t *cls_record;
    qd_hash_retrieve_const(agent->class_hash, cls_string, (const void**) &cls_record);
    if (cls_record == 0)
        return;

    qd_log(log_module, QD_LOG_TRACE, "Received GET request for type: %s", qd_hash_key_by_handle(cls_record->hash_handle));

    qd_composed_field_t *field = qd_agent_setup_response(reply_to, cid);

    //
    // Open the Body (AMQP Value) to be filled in by the handler.
    //
    field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, field);
    qd_compose_start_list(field);

    //
    // The request record is allocated locally because the entire processing of the request
    // will be done synchronously.
    //
    qd_agent_request_t request;
    request.agent    = agent;
    request.response = field;
    request.empty    = 1;

    cls_record->query_handler(cls_record->context, 0, &request);

    //
    // The response is complete, close the list.
    //
    qd_compose_end_list(field);

    //
    // Create a message and send it.
    //
    qd_message_t *msg = qd_message();
    qd_message_compose_2(msg, field);
    qd_router_send(agent->qd, reply_to, msg);

    qd_message_free(msg);
    qd_compose_free(field);
}


static void qd_agent_process_discover_types(qd_agent_t          *agent,
                                            qd_parsed_field_t   *map,
                                            qd_field_iterator_t *reply_to,
                                            qd_field_iterator_t *cid)
{
    qd_log(log_module, QD_LOG_TRACE, "Received DISCOVER-TYPES request");

    qd_composed_field_t *field = qd_agent_setup_response(reply_to, cid);

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
    while (cls) {
        qd_compose_insert_string(field, (const char*) qd_hash_key_by_handle(cls->hash_handle));
        qd_compose_empty_list(field);
        cls = DEQ_NEXT(cls);
    }
    sys_mutex_unlock(agent->lock);
    qd_compose_end_map(field);

    //
    // Create a message and send it.
    //
    qd_message_t *msg = qd_message();
    qd_message_compose_2(msg, field);
    qd_router_send(agent->qd, reply_to, msg);

    qd_message_free(msg);
    qd_compose_free(field);
}


static void qd_agent_process_discover_operations(qd_agent_t          *agent,
                                                 qd_parsed_field_t   *map,
                                                 qd_field_iterator_t *reply_to,
                                                 qd_field_iterator_t *cid)
{
    qd_log(log_module, QD_LOG_TRACE, "Received DISCOVER-OPERATIONS request");

    qd_composed_field_t *field = qd_agent_setup_response(reply_to, cid);

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
    while (cls) {
        qd_compose_insert_string(field, (const char*) qd_hash_key_by_handle(cls->hash_handle));
        qd_compose_start_list(field);
        qd_compose_insert_string(field, "READ");
        qd_compose_end_list(field);
        cls = DEQ_NEXT(cls);
    }
    sys_mutex_unlock(agent->lock);
    qd_compose_end_map(field);

    //
    // Create a message and send it.
    //
    qd_message_t *msg = qd_message();
    qd_message_compose_2(msg, field);
    qd_router_send(agent->qd, reply_to, msg);

    qd_message_free(msg);
    qd_compose_free(field);
}


static void qd_agent_process_discover_nodes(qd_agent_t          *agent,
                                            qd_parsed_field_t   *map,
                                            qd_field_iterator_t *reply_to,
                                            qd_field_iterator_t *cid)
{
    qd_log(log_module, QD_LOG_TRACE, "Received DISCOVER-MGMT-NODES request");

    qd_composed_field_t *field = qd_agent_setup_response(reply_to, cid);

    //
    // Open the Body (AMQP Value) to be filled in by the handler.
    //
    field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, field);

    //
    // Put entries into the list for each known management node
    //
    qd_compose_start_list(field);
    qd_compose_insert_string(field, "amqp:/_local/$management");
    qd_router_build_node_list(agent->qd, field);
    qd_compose_end_list(field);

    //
    // Create a message and send it.
    //
    qd_message_t *msg = qd_message();
    qd_message_compose_2(msg, field);
    qd_router_send(agent->qd, reply_to, msg);

    qd_message_free(msg);
    qd_compose_free(field);
}


static void qd_agent_process_request(qd_agent_t *agent, qd_message_t *msg)
{
    //
    // Parse the message through the body and exit if the message is not well formed.
    //
    if (!qd_message_check(msg, QD_DEPTH_BODY))
        return;

    //
    // Get an iterator for the application-properties.  Exit if the message has none.
    //
    qd_field_iterator_t *ap = qd_message_field_iterator(msg, QD_FIELD_APPLICATION_PROPERTIES);
    if (ap == 0)
        return;

    //
    // Get an iterator for the reply-to.  Exit if not found.
    //
    qd_field_iterator_t *reply_to = qd_message_field_iterator(msg, QD_FIELD_REPLY_TO);
    if (reply_to == 0)
        return;

    //
    // Try to get a map-view of the application-properties.
    //
    qd_parsed_field_t *map = qd_parse(ap);
    if (map == 0) {
        qd_field_iterator_free(ap);
        qd_field_iterator_free(reply_to);
        return;
    }

    //
    // Exit if there was a parsing error.
    //
    if (!qd_parse_ok(map)) {
        qd_log(log_module, QD_LOG_TRACE, "Received unparsable App Properties: %s", qd_parse_error(map));
        qd_field_iterator_free(ap);
        qd_field_iterator_free(reply_to);
        qd_parse_free(map);
        return;
    }

    //
    // Exit if it is not a map.
    //
    if (!qd_parse_is_map(map)) {
        qd_field_iterator_free(ap);
        qd_field_iterator_free(reply_to);
        qd_parse_free(map);
        return;
    }

    //
    // Get an iterator for the "operation" field in the map.  Exit if the key is not found.
    //
    qd_parsed_field_t *operation = qd_parse_value_by_key(map, "operation");
    if (operation == 0) {
        qd_parse_free(map);
        qd_field_iterator_free(ap);
        qd_field_iterator_free(reply_to);
        return;
    }

    //
    // Get an iterator for the correlation_id.
    //
    qd_field_iterator_t *cid = qd_message_field_iterator_typed(msg, QD_FIELD_CORRELATION_ID);

    //
    // Dispatch the operation to the appropriate handler
    //
    qd_field_iterator_t *operation_string = qd_parse_raw(operation);
    if (qd_field_iterator_equal(operation_string, (unsigned char*) "GET"))
        qd_agent_process_get(agent, map, reply_to, cid);
    if (qd_field_iterator_equal(operation_string, (unsigned char*) "DISCOVER-TYPES"))
        qd_agent_process_discover_types(agent, map, reply_to, cid);
    if (qd_field_iterator_equal(operation_string, (unsigned char*) "DISCOVER-OPERATIONS"))
        qd_agent_process_discover_operations(agent, map, reply_to, cid);
    if (qd_field_iterator_equal(operation_string, (unsigned char*) "DISCOVER-MGMT-NODES"))
        qd_agent_process_discover_nodes(agent, map, reply_to, cid);

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


static qd_agent_class_t *qd_agent_register_class_LH(qd_agent_t           *agent,
                                                    const char           *fqname,
                                                    void                 *context,
                                                    qd_agent_schema_cb_t  schema_handler,
                                                    qd_agent_query_cb_t   query_handler)
{
    qd_agent_class_t *cls = NEW(qd_agent_class_t);
    assert(cls);
    DEQ_ITEM_INIT(cls);
    cls->context        = context;
    cls->schema_handler = schema_handler;
    cls->query_handler  = query_handler;

    qd_field_iterator_t *iter = qd_field_iterator_string(fqname, ITER_VIEW_ALL);
    int result = qd_hash_insert_const(agent->class_hash, iter, cls, &cls->hash_handle);
    qd_field_iterator_free(iter);
    if (result < 0)
        assert(false);

    DEQ_INSERT_TAIL(agent->class_list, cls);

    qd_log(log_module, QD_LOG_INFO, "Manageable Entity Type (%s) %s", query_handler ? "object" : "event", fqname);
    return cls;
}


qd_agent_t *qd_agent(qd_dispatch_t *qd)
{
    qd_agent_t *agent = NEW(qd_agent_t);
    agent->qd         = qd;
    agent->class_hash = qd_hash(6, 10, 1);
    DEQ_INIT(agent->class_list);
    DEQ_INIT(agent->in_fifo);
    DEQ_INIT(agent->out_fifo);
    agent->lock    = sys_mutex();
    agent->timer   = qd_timer(qd, qd_agent_deferred_handler, agent);
    agent->address = qd_router_register_address(qd, "$management", qd_agent_rx_handler, agent_semantics, agent);

    return agent;
}


void qd_agent_free(qd_agent_t *agent)
{
    qd_router_unregister_address(agent->address);
    sys_mutex_free(agent->lock);
    qd_timer_free(agent->timer);
    qd_hash_free(agent->class_hash);
    free(agent);
}


qd_agent_class_t *qd_agent_register_class(qd_dispatch_t        *qd,
                                          const char           *fqname,
                                          void                 *context,
                                          qd_agent_schema_cb_t  schema_handler,
                                          qd_agent_query_cb_t   query_handler)
{
    qd_agent_t       *agent = qd->agent;
    qd_agent_class_t *cls;

    sys_mutex_lock(agent->lock);
    cls = qd_agent_register_class_LH(agent, fqname, context, schema_handler, query_handler);
    sys_mutex_unlock(agent->lock);
    return cls;
}


qd_agent_class_t *qd_agent_register_event(qd_dispatch_t        *qd,
                                          const char           *fqname,
                                          void                 *context,
                                          qd_agent_schema_cb_t  schema_handler)
{
    return qd_agent_register_class(qd, fqname, context, schema_handler, 0);
}


void qd_agent_value_string(void *correlator, const char *key, const char *value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_agent_check_empty(request);
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_string(request->response, value);
}


void qd_agent_value_uint(void *correlator, const char *key, uint64_t value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_agent_check_empty(request);
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_uint(request->response, value);
}


void qd_agent_value_null(void *correlator, const char *key)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_agent_check_empty(request);
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_null(request->response);
}


void qd_agent_value_boolean(void *correlator, const char *key, bool value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_agent_check_empty(request);
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_bool(request->response, value);
}


void qd_agent_value_binary(void *correlator, const char *key, const uint8_t *value, size_t len)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_agent_check_empty(request);
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_binary(request->response, value, len);
}


void qd_agent_value_uuid(void *correlator, const char *key, const uint8_t *value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_agent_check_empty(request);
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_uuid(request->response, value);
}


void qd_agent_value_timestamp(void *correlator, const char *key, uint64_t value)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_agent_check_empty(request);
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_insert_timestamp(request->response, value);
}


void qd_agent_value_start_list(void *correlator, const char *key)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_agent_check_empty(request);
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
    qd_agent_check_empty(request);
    if (key)
        qd_compose_insert_string(request->response, key);
    qd_compose_start_map(request->response);
}


void qd_agent_value_end_map(void *correlator)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;
    qd_compose_end_map(request->response);
}


void qd_agent_value_complete(void *correlator, bool more)
{
    qd_agent_request_t *request = (qd_agent_request_t*) correlator;

    if (!more && request->empty)
        return;

    assert (!more || !request->empty);

    qd_compose_end_map(request->response);
    if (more)
        qd_compose_start_map(request->response);
}


void *qd_agent_raise_event(qd_dispatch_t *qd, qd_agent_class_t *event)
{
    return 0;
}

