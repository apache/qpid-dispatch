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

#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/message.h"
#include "qpid/dispatch/compose.h"
#include "core_test_hooks.h"
#include "core_link_endpoint.h"
#include <stdio.h>
#include <inttypes.h>

typedef enum {
    TEST_NODE_ECHO,
    TEST_NODE_DENY,
    TEST_NODE_SINK,
    TEST_NODE_SOURCE,
    TEST_NODE_SOURCE_PS,
    TEST_NODE_DISCARD
} test_node_behavior_t;

typedef struct test_node_t test_node_t;

typedef struct test_endpoint_t {
    DEQ_LINKS(struct test_endpoint_t);
    test_node_t         *node;
    qdrc_endpoint_t     *ep;
    qdr_delivery_list_t  deliveries;
    int                  credit;
    bool                 in_action_list;
    bool                 detached;
} test_endpoint_t;

DEQ_DECLARE(test_endpoint_t, test_endpoint_list_t);

struct test_node_t {
    qdr_core_t           *core;
    test_node_behavior_t  behavior;
    qdrc_endpoint_desc_t *desc;
    test_endpoint_list_t  in_links;
    test_endpoint_list_t  out_links;
};

static test_node_t *echo_node;
static test_node_t *deny_node;
static test_node_t *sink_node;
static test_node_t *source_node;
static test_node_t *source_ps_node;
static test_node_t *discard_node;


static void endpoint_action(qdr_core_t *core, qdr_action_t *action, bool discard);


static void source_send(test_endpoint_t *ep, bool presettled)
{
    static uint32_t      sequence = 0;
    static char          stringbuf[100];
    qdr_delivery_t      *dlv;
    qd_message_t        *msg   = qd_message();
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

    dlv = qdrc_endpoint_delivery_CT(ep->node->core, ep->ep, msg);
    qd_message_compose_2(msg, field);
    qd_compose_free(field);
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

    if (qdrc_endpoint_get_direction_CT(ep->ep) == QD_INCOMING)
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


static bool first_attach(void             *bind_context,
                         qdrc_endpoint_t  *endpoint,
                         void            **link_context,
                         qdr_error_t     **error)
{
    test_node_t     *node     = (test_node_t*) bind_context;
    test_endpoint_t *test_ep  = 0;
    bool             incoming = qdrc_endpoint_get_direction_CT(endpoint) == QD_INCOMING;

    switch (node->behavior) {
    case TEST_NODE_DENY :
        *error = qdr_error("qd:forbidden", "Connectivity to the deny node is forbidden");
        return false;

    case TEST_NODE_ECHO :
        break;

    case TEST_NODE_SINK :
        if (incoming) {
            qdrc_endpoint_flow_CT(node->core, endpoint, 1, false);
        } else {
            *error = qdr_error("qd:forbidden", "Sink function only accepts incoming links");
            return false;
        }
        break;

    case TEST_NODE_SOURCE :
    case TEST_NODE_SOURCE_PS :
        if (incoming) {
            *error = qdr_error("qd:forbidden", "Source function only accepts outgoing links");
            return false;
        }
        break;

    case TEST_NODE_DISCARD :
        if (incoming) {
            qdrc_endpoint_flow_CT(node->core, endpoint, 1, false);
        } else {
            *error = qdr_error("qd:forbidden", "Discard function only accepts incoming links");
            return false;
        }
        break;
    }

    test_ep = NEW(test_endpoint_t);
    ZERO(test_ep);
    test_ep->node = node;
    test_ep->ep   = endpoint;
    *link_context = test_ep;

    if (incoming)
        DEQ_INSERT_TAIL(node->in_links, test_ep);
    else
        DEQ_INSERT_TAIL(node->out_links, test_ep);

    return true;
}


static void second_attach(void           *link_context,
                          qdr_terminus_t *remote_source,
                          qdr_terminus_t *remote_target)
{
}


static void flow(void *link_context,
                 int   available_credit,
                 bool  drain)
{
    test_endpoint_t *ep = (test_endpoint_t*) link_context;
    if (available_credit == 0)
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


static void update(void           *link_context,
                   qdr_delivery_t *delivery,
                   bool            settled,
                   uint64_t        disposition)
{
}


static void transfer(void           *link_context,
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


static void detach(void        *link_context,
                   qdr_error_t *error)
{
    test_endpoint_t *ep = (test_endpoint_t*) link_context;

    if (ep->in_action_list) {
        ep->detached = true;
    } else {
        free_endpoint(ep);
    }
}


static void cleanup(void *link_context)
{
}


static qdrc_endpoint_desc_t descriptor = {first_attach, second_attach, flow, update, transfer, detach, cleanup};


static void qdrc_test_hooks_core_endpoint_setup(qdr_core_t *core)
{
    char *echo_address       = "org.apache.qpid.dispatch.router/test/echo";
    char *deny_address       = "org.apache.qpid.dispatch.router/test/deny";
    char *sink_address       = "org.apache.qpid.dispatch.router/test/sink";
    char *source_address     = "org.apache.qpid.dispatch.router/test/source";
    char *source_ps_address  = "org.apache.qpid.dispatch.router/test/source_ps";
    char *discard_address    = "org.apache.qpid.dispatch.router/test/discard";

    echo_node       = NEW(test_node_t);
    deny_node       = NEW(test_node_t);
    sink_node       = NEW(test_node_t);
    source_node     = NEW(test_node_t);
    source_ps_node  = NEW(test_node_t);
    discard_node    = NEW(test_node_t);

    echo_node->core     = core;
    echo_node->behavior = TEST_NODE_ECHO;
    echo_node->desc     = &descriptor;
    DEQ_INIT(echo_node->in_links);
    DEQ_INIT(echo_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, echo_address, '0', &descriptor, echo_node);

    deny_node->core     = core;
    deny_node->behavior = TEST_NODE_DENY;
    deny_node->desc     = &descriptor;
    DEQ_INIT(deny_node->in_links);
    DEQ_INIT(deny_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, deny_address, '0', &descriptor, deny_node);

    sink_node->core     = core;
    sink_node->behavior = TEST_NODE_SINK;
    sink_node->desc     = &descriptor;
    DEQ_INIT(sink_node->in_links);
    DEQ_INIT(sink_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, sink_address, '0', &descriptor, sink_node);

    source_node->core     = core;
    source_node->behavior = TEST_NODE_SOURCE;
    source_node->desc     = &descriptor;
    DEQ_INIT(source_node->in_links);
    DEQ_INIT(source_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, source_address, '0', &descriptor, source_node);

    source_ps_node->core     = core;
    source_ps_node->behavior = TEST_NODE_SOURCE_PS;
    source_ps_node->desc     = &descriptor;
    DEQ_INIT(source_ps_node->in_links);
    DEQ_INIT(source_ps_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, source_ps_address, '0', &descriptor, source_ps_node);

    discard_node->core     = core;
    discard_node->behavior = TEST_NODE_DISCARD;
    discard_node->desc     = &descriptor;
    DEQ_INIT(discard_node->in_links);
    DEQ_INIT(discard_node->out_links);
    qdrc_endpoint_bind_mobile_address_CT(core, discard_address, '0', &descriptor, discard_node);
}


static void qdrc_test_hooks_core_endpoint_finalize(qdr_core_t *core)
{
    free(echo_node);
    free(deny_node);
    free(sink_node);
    free(source_node);
    free(source_ps_node);
    free(discard_node);
}


void qdrc_test_hooks_init_CT(qdr_core_t *core)
{
    //
    // Exit if the test hooks are not enabled (by the --test-hooks command line option)
    //
    if (!core->qd->test_hooks)
        return;

    qd_log(core->log, QD_LOG_INFO, "Core thread system test hooks enabled");

    qdrc_test_hooks_core_endpoint_setup(core);
}


void qdrc_test_hooks_final_CT(qdr_core_t *core)
{
    //
    // Exit if the test hooks are not enabled (by the --test-hooks command line option)
    //
    if (!core->qd->test_hooks)
        return;

    qdrc_test_hooks_core_endpoint_finalize(core);
}

