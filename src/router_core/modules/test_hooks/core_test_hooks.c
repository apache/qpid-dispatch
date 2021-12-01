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

#undef NDEBUG  // test code - uses asserts

#include "core_client_api.h"
#include "core_link_endpoint.h"
#include "module.h"

#include "qpid/dispatch/compose.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/message.h"

#include <inttypes.h>
#include <stdio.h>

typedef enum {
    TEST_NODE_ECHO,
    TEST_NODE_DENY,
    TEST_NODE_SINK,
    TEST_NODE_SOURCE,
    TEST_NODE_SOURCE_PS,
    TEST_NODE_DISCARD
} test_node_behavior_t;

typedef struct test_module_t test_module_t;
typedef struct test_node_t   test_node_t;
typedef struct test_client_t test_client_t;

typedef struct test_endpoint_t {
    DEQ_LINKS(struct test_endpoint_t);
    test_node_t            *node;
    qdrc_endpoint_t        *ep;
    qdr_delivery_list_t     deliveries;
    int                     credit;
    bool                    in_action_list;
    bool                    detached;
    qd_direction_t          dir;
    struct test_endpoint_t *peer;
} test_endpoint_t;

DEQ_DECLARE(test_endpoint_t, test_endpoint_list_t);

struct test_node_t {
    qdr_core_t           *core;
    test_module_t        *module;
    test_node_behavior_t  behavior;
    qdrc_endpoint_desc_t *desc;
    test_endpoint_list_t  in_links;
    test_endpoint_list_t  out_links;
};

struct test_module_t {
    qdr_core_t    *core;
    test_node_t   *echo_node;
    test_node_t   *deny_node;
    test_node_t   *sink_node;
    test_node_t   *source_node;
    test_node_t   *source_ps_node;
    test_node_t   *discard_node;
    test_client_t *test_client;
};


static void endpoint_action(qdr_core_t *core, qdr_action_t *action, bool discard);


static void source_send(test_endpoint_t *ep, bool presettled)
{
    static uint32_t      sequence = 0;
    static char          stringbuf[100];
    qdr_delivery_t      *dlv;
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_HEADER, 0);

    sprintf(stringbuf, "Sequence: %"PRIu32, sequence);

    qd_compose_start_list(field);
    qd_compose_insert_bool(field, 0);     // durable
    qd_compose_end_list(field);

    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_null(field);        // message-id
    qd_compose_end_list(field);

    field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, field);
    qd_compose_start_map(field);
    qd_compose_insert_symbol(field, "sequence");
    qd_compose_insert_uint(field, sequence++);
    qd_compose_end_map(field);

    field = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, field);
    qd_compose_insert_string(field, stringbuf);

    qd_message_t *msg = qd_message_compose(field, 0, 0, true);
    dlv = qdrc_endpoint_delivery_CT(ep->node->core, ep->ep, msg);
    qdrc_endpoint_send_CT(ep->node->core, ep->ep, dlv, presettled);

    if (--ep->credit > 0) {
        qdr_action_t *action = qdr_action(endpoint_action, "test_hooks_endpoint_action");
        action->args.general.context_1 = (void*) ep;
        ep->in_action_list = true;
        qdr_action_enqueue(ep->node->core, action);
    }
}


static void free_endpoint(test_endpoint_t *ep)
{
    test_node_t *node = ep->node;

    if (ep->dir == QD_INCOMING)
        DEQ_REMOVE(node->in_links, ep);
    else
        DEQ_REMOVE(node->out_links, ep);
    free(ep);
}


static void endpoint_action(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    test_endpoint_t *ep = (test_endpoint_t*) action->args.general.context_1;

    ep->in_action_list = false;
    if (ep->detached) {
        free_endpoint(ep);
        return;
    }

    switch (ep->node->behavior) {
    case TEST_NODE_DENY :
    case TEST_NODE_SINK :
    case TEST_NODE_DISCARD :

    case TEST_NODE_SOURCE :
        source_send(ep, false);
        break;

    case TEST_NODE_SOURCE_PS :
        source_send(ep, true);
        break;

    case TEST_NODE_ECHO :
        break;
    }
}


static void on_first_attach(void             *bind_context,
                            qdrc_endpoint_t  *endpoint,
                            void            **link_context,
                            qdr_terminus_t   *source,
                            qdr_terminus_t   *target)
{
    test_node_t     *node     = (test_node_t*) bind_context;
    test_endpoint_t *test_ep  = 0;
    bool             incoming = qdrc_endpoint_get_direction_CT(endpoint) == QD_INCOMING;
    qdr_error_t     *error    = 0;

    switch (node->behavior) {
    case TEST_NODE_DENY :
        error = qdr_error("qd:forbidden", "Connectivity to the deny node is forbidden");
        break;

    case TEST_NODE_ECHO :
        break;

    case TEST_NODE_SINK :
        if (incoming) {
            qdrc_endpoint_flow_CT(node->core, endpoint, 1, false);
        } else {
            error = qdr_error("qd:forbidden", "Sink function only accepts incoming links");
        }
        break;

    case TEST_NODE_SOURCE :
    case TEST_NODE_SOURCE_PS :
        if (incoming) {
            error = qdr_error("qd:forbidden", "Source function only accepts outgoing links");
        }
        break;

    case TEST_NODE_DISCARD :
        if (incoming) {
            qdrc_endpoint_flow_CT(node->core, endpoint, 1, false);
        } else {
            error = qdr_error("qd:forbidden", "Discard function only accepts incoming links");
        }
        break;
    }

    if (!!error) {
        qdrc_endpoint_detach_CT(node->core, endpoint, error);
        qdr_terminus_free(source);
        qdr_terminus_free(target);
        return;
    }

    test_ep = NEW(test_endpoint_t);
    ZERO(test_ep);
    test_ep->node = node;
    test_ep->ep   = endpoint;
    test_ep->dir  = incoming ? QD_INCOMING : QD_OUTGOING;
    *link_context = test_ep;

    if (incoming)
        DEQ_INSERT_TAIL(node->in_links, test_ep);
    else
        DEQ_INSERT_TAIL(node->out_links, test_ep);

    if (node->behavior == TEST_NODE_ECHO) {
        test_endpoint_t *peer = NEW(test_endpoint_t);
        ZERO(peer);
        peer->node = node;
        peer->ep   = qdrc_endpoint_create_link_CT(node->core,
                                                  qdrc_endpoint_get_connection_CT(endpoint),
                                                  incoming ? QD_OUTGOING : QD_INCOMING,
                                                  source,
                                                  target,
                                                  node->desc,
                                                  peer);
        test_ep->dir  = incoming ? QD_INCOMING : QD_OUTGOING;
        test_ep->peer = peer;
        peer->peer    = test_ep;

        if (incoming)
            DEQ_INSERT_TAIL(node->out_links, peer);
        else
            DEQ_INSERT_TAIL(node->in_links, peer);
    } else
        qdrc_endpoint_second_attach_CT(node->core, endpoint, source, target);
}


static void on_second_attach(void           *link_context,
                             qdr_terminus_t *remote_source,
                             qdr_terminus_t *remote_target)
{
    test_endpoint_t *ep = (test_endpoint_t*) link_context;

    if (!!ep->peer) {
        qdrc_endpoint_second_attach_CT(ep->node->core, ep->peer->ep, remote_source, remote_target);
    }
}


static void on_flow(void *link_context,
                    int   available_credit,
                    bool  drain)
{
    test_endpoint_t *ep = (test_endpoint_t*) link_context;
    if (!ep || available_credit == 0)
        return;

    ep->credit = available_credit;

    switch (ep->node->behavior) {
    case TEST_NODE_DENY :
    case TEST_NODE_SINK :
    case TEST_NODE_DISCARD :
        break;

    case TEST_NODE_SOURCE :
        source_send(ep, false);
        break;

    case TEST_NODE_SOURCE_PS :
        source_send(ep, true);
        break;

    case TEST_NODE_ECHO :
        break;
    }
}


static void on_update(void           *link_context,
                      qdr_delivery_t *delivery,
                      bool            settled,
                      uint64_t        disposition)
{
}


static void on_transfer(void           *link_context,
                        qdr_delivery_t *delivery,
                        qd_message_t   *message)
{
    test_endpoint_t *ep = (test_endpoint_t*) link_context;

    if (!qd_message_receive_complete(message))
        return;

    switch (ep->node->behavior) {
    case TEST_NODE_DENY :
    case TEST_NODE_SOURCE :
    case TEST_NODE_SOURCE_PS :
        assert(false); // Can't get here.  Link should not have been opened
        break;

    case TEST_NODE_ECHO :
        break;

    case TEST_NODE_SINK :
        qdrc_endpoint_settle_CT(ep->node->core, delivery, PN_ACCEPTED);
        qdrc_endpoint_flow_CT(ep->node->core, ep->ep, 1, false);
        break;

    case TEST_NODE_DISCARD :
        qdrc_endpoint_settle_CT(ep->node->core, delivery, PN_REJECTED);
        qdrc_endpoint_flow_CT(ep->node->core, ep->ep, 1, false);
        break;
    }
}


static void on_first_detach(void        *link_context,
                            qdr_error_t *error)
{
    test_endpoint_t *ep = (test_endpoint_t*) link_context;

    if (ep->node->behavior == TEST_NODE_ECHO) {
        if (!!ep->peer) {
            qdrc_endpoint_detach_CT(ep->node->core, ep->peer->ep, error);
            ep->peer = 0;
            return;
        }
    }

    qdrc_endpoint_detach_CT(ep->node->core, ep->ep, 0);
    qdr_error_free(error);
}


static void on_second_detach(void        *link_context,
                             qdr_error_t *error)
{
    test_endpoint_t *ep = (test_endpoint_t*) link_context;

    if (!!ep) {
        if (ep->node->behavior == TEST_NODE_ECHO) {
            if (!!ep->peer) {
                qdrc_endpoint_detach_CT(ep->node->core, ep->peer->ep, error);
                error = 0;
            }
        }
    }
    qdr_error_free(error);
    // on_cleanup(ep) is called on return
}


static void on_cleanup(void *link_context)
{
    if (!link_context) return;

    test_endpoint_t *ep = (test_endpoint_t*) link_context;
    if (ep->in_action_list) {
        ep->detached = true;
        ep->ep = 0;
    } else {
        free_endpoint(ep);
    }
}


static qdrc_endpoint_desc_t descriptor = {"Core Test Hooks", on_first_attach, on_second_attach, on_flow, on_update,
                                          on_transfer, on_first_detach, on_second_detach, on_cleanup};


static test_module_t *qdrc_test_hooks_core_endpoint_setup(qdr_core_t *core, test_module_t *module)
{
    char *echo_address       = "org.apache.qpid.dispatch.router/test/echo";
    char *deny_address       = "org.apache.qpid.dispatch.router/test/deny";
    char *sink_address       = "org.apache.qpid.dispatch.router/test/sink";
    char *source_address     = "org.apache.qpid.dispatch.router/test/source";
    char *source_ps_address  = "org.apache.qpid.dispatch.router/test/source_ps";
    char *discard_address    = "org.apache.qpid.dispatch.router/test/discard";

    module->echo_node      = NEW(test_node_t);
    module->deny_node      = NEW(test_node_t);
    module->sink_node      = NEW(test_node_t);
    module->source_node    = NEW(test_node_t);
    module->source_ps_node = NEW(test_node_t);
    module->discard_node   = NEW(test_node_t);

    module->echo_node->core     = core;
    module->echo_node->module   = module;
    module->echo_node->behavior = TEST_NODE_ECHO;
    module->echo_node->desc     = &descriptor;
    DEQ_INIT(module->echo_node->in_links);
    DEQ_INIT(module->echo_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, echo_address, '0', &descriptor, module->echo_node);

    module->deny_node->core     = core;
    module->deny_node->module   = module;
    module->deny_node->behavior = TEST_NODE_DENY;
    module->deny_node->desc     = &descriptor;
    DEQ_INIT(module->deny_node->in_links);
    DEQ_INIT(module->deny_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, deny_address, '0', &descriptor, module->deny_node);

    module->sink_node->core     = core;
    module->sink_node->module   = module;
    module->sink_node->behavior = TEST_NODE_SINK;
    module->sink_node->desc     = &descriptor;
    DEQ_INIT(module->sink_node->in_links);
    DEQ_INIT(module->sink_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, sink_address, '0', &descriptor, module->sink_node);

    module->source_node->core     = core;
    module->source_node->module   = module;
    module->source_node->behavior = TEST_NODE_SOURCE;
    module->source_node->desc     = &descriptor;
    DEQ_INIT(module->source_node->in_links);
    DEQ_INIT(module->source_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, source_address, '0', &descriptor, module->source_node);

    module->source_ps_node->core     = core;
    module->source_ps_node->module   = module;
    module->source_ps_node->behavior = TEST_NODE_SOURCE_PS;
    module->source_ps_node->desc     = &descriptor;
    DEQ_INIT(module->source_ps_node->in_links);
    DEQ_INIT(module->source_ps_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, source_ps_address, '0', &descriptor, module->source_ps_node);

    module->discard_node->core     = core;
    module->discard_node->module   = module;
    module->discard_node->behavior = TEST_NODE_DISCARD;
    module->discard_node->desc     = &descriptor;
    DEQ_INIT(module->discard_node->in_links);
    DEQ_INIT(module->discard_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, discard_address, '0', &descriptor, module->discard_node);

    return module;
}


static void qdrc_test_hooks_core_endpoint_finalize(test_module_t *module)
{
    free(module->echo_node);
    free(module->deny_node);
    free(module->sink_node);
    free(module->source_node);
    free(module->source_ps_node);
    free(module->discard_node);
}


//
// Tests for in-core messaging client API
//
// Note well: this test client is used by the system_tests_core_client.py unit
// tests.  Any changes here may require updates to those tests.
//

static void _do_send(test_client_t *tc);

struct test_client_t {
    test_module_t             *module;
    qdrc_event_subscription_t *conn_events;
    qdr_connection_t          *conn;
    qdrc_client_t             *core_client;
    int                        credit;
    long                       counter;
};

static uint64_t _client_on_reply_cb(qdr_core_t    *core,
                                    qdrc_client_t *client,
                                    void          *user_context,
                                    void          *request_context,
                                    qd_iterator_t *app_properties,
                                    qd_iterator_t *body)
{
    qd_log(core->log, QD_LOG_TRACE,
           "client test reply received rc=%p", request_context);

    qd_iterator_free(app_properties);
    qd_iterator_free(body);

    return PN_ACCEPTED;
}

static void _client_on_ack_cb(qdr_core_t    *core,
                              qdrc_client_t *client,
                              void          *user_context,
                              void          *request_context,
                              uint64_t       disposition)
{
    test_client_t *tc = (test_client_t *)user_context;
    qd_log(core->log, QD_LOG_TRACE,
           "client test request ack rc=%p d=%"PRIu64,
           request_context, disposition);
    assert((long) request_context < tc->counter);
}

static void _client_on_done_cb(qdr_core_t    *core,
                               qdrc_client_t *client,
                               void          *user_context,
                               void          *request_context,
                               const char    *error)
{
    // the system_tests_core_client.py looks for the following
    // log message during the tests
    test_client_t *tc = (test_client_t *)user_context;
    qd_log_level_t level = (error) ? QD_LOG_ERROR : QD_LOG_TRACE;
    qd_log(core->log, level,
           "client test request done error=%s",
           (error) ? error : "None");
    if (!error && tc->credit > 0) {
        _do_send(tc);
    }
}

// send a single request if credit available
static void _do_send(test_client_t *tc)
{
    int rc = 0;
    if (tc->credit > 0) {

        qd_composed_field_t *props = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, 0);
        qd_composed_field_t *body = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

        qd_compose_start_map(props);
        qd_compose_insert_string(props, "action");
        qd_compose_insert_string(props, "echo");
        qd_compose_insert_string(props, "counter");
        qd_compose_insert_long(props, tc->counter);
        qd_compose_end_map(props);
        qd_compose_insert_string(body, "HI THERE");

        qdrc_client_request_CT(tc->core_client,
                               (void *)tc->counter,  // request context
                               props,
                               body,
                               5,  // timeout
                               _client_on_reply_cb,
                               _client_on_ack_cb,
                               _client_on_done_cb);
        assert(rc == 0);
        ++tc->counter;
        --tc->credit;
        qd_log(tc->module->core->log, QD_LOG_TRACE,
               "client test message sent id=%"PRIi64" c=%d", tc->counter - 1, tc->credit);
    }
}

static void _client_on_state_cb(qdr_core_t *core, qdrc_client_t *core_client,
                                void *user_context, bool active)
{
    test_client_t *tc = (test_client_t *)user_context;
    qd_log(tc->module->core->log, QD_LOG_TRACE,
           "client test on state active=%c", active ? 'T' : 'F');
}

static void _client_on_flow_cb(qdr_core_t *core, qdrc_client_t *core_client,
                               void *user_context, int available_credit,
                               bool drain)
{
    test_client_t *tc = (test_client_t *)user_context;

    if (!tc->core_client)
        return;

    qd_log(tc->module->core->log, QD_LOG_TRACE,
           "client test on flow c=%d d=%c", available_credit, drain ? 'T' : 'F');
    tc->credit = available_credit;
    if (drain) {
        while (tc->credit > 0)
            _do_send(tc);
    } else {
        _do_send(tc);
    }
}

static void _on_conn_event(void *context, qdrc_event_t type, qdr_connection_t *conn)
{
    test_client_t *tc = (test_client_t *)context;

    qd_log(tc->module->core->log, QD_LOG_TRACE, "client test on conn event");

    switch (type) {
    case QDRC_EVENT_CONN_OPENED:
        qd_log(tc->module->core->log, QD_LOG_TRACE, "client test conn open");
        if (tc->conn)  // already have a conn, ignore
            return;
        // look for the special test container id
        const char *cid = ((conn->connection_info)
                           ? conn->connection_info->container
                           : NULL);
        qd_log(tc->module->core->log, QD_LOG_TRACE, "client test container-id=%s", cid);

        if (cid && strcmp(cid, "org.apache.qpid.dispatch.test_core_client") == 0) {
            qd_log(tc->module->core->log, QD_LOG_TRACE, "client test connection opened");
            qdr_terminus_t *target = qdr_terminus(NULL);
            qdr_terminus_set_address(target, "test_client_address");
            tc->conn = conn;
            tc->core_client = qdrc_client_CT(tc->module->core,
                                             tc->conn,
                                             target,
                                             10,   // reply credit window
                                             tc,   // user context
                                             _client_on_state_cb,
                                             _client_on_flow_cb);
            assert(tc->core_client);
        }
        break;
    case QDRC_EVENT_CONN_CLOSED:
        qd_log(tc->module->core->log, QD_LOG_TRACE, "client test conn closed");
        if (tc->conn == conn) {
            tc->conn = NULL;
            tc->credit = 0;
            tc->counter = 0;
            qdrc_client_free_CT(tc->core_client);
            tc->core_client = NULL;
            qd_log(tc->module->core->log, QD_LOG_TRACE, "client test connection closed");
        }
        break;
    }
}


static void qdrc_test_client_api_setup(test_module_t *test_module)
{
    test_client_t *tc = NEW(test_client_t);
    ZERO(tc);

    tc->module = test_module;
    test_module->test_client = tc;
    tc->conn_events = qdrc_event_subscribe_CT(test_module->core,
                                              (QDRC_EVENT_CONN_OPENED | QDRC_EVENT_CONN_CLOSED),
                                              _on_conn_event,
                                              0, 0, 0, tc);

    qd_log(test_module->core->log, QD_LOG_TRACE, "client test registered %p", tc->conn_events);
}


static void qdrc_test_client_api_finalize(test_module_t *test_module)
{
    test_client_t *tc = test_module->test_client;
    if (tc) {
        if (tc->core_client)
            qdrc_client_free_CT(tc->core_client);
        if (tc->conn_events)
            qdrc_event_unsubscribe_CT(test_module->core, tc->conn_events);
        free(tc);
        test_module->test_client = NULL;
    }
}


static bool qdrc_test_hooks_enable_CT(qdr_core_t *core)
{
    return core->qd->test_hooks;
}


static void qdrc_test_hooks_init_CT(qdr_core_t *core, void **module_context)
{
    test_module_t *test_module = NEW(test_module_t);
    ZERO(test_module);
    test_module->core = core;
    qdrc_test_hooks_core_endpoint_setup(core, test_module);
    qdrc_test_client_api_setup(test_module);
    *module_context = test_module;
}


static void qdrc_test_hooks_final_CT(void *module_context)
{
    qdrc_test_hooks_core_endpoint_finalize(module_context);
    qdrc_test_client_api_finalize(module_context);
    free(module_context);
}


QDR_CORE_MODULE_DECLARE("core_test_hooks", qdrc_test_hooks_enable_CT, qdrc_test_hooks_init_CT, qdrc_test_hooks_final_CT)
