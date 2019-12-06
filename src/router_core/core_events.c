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

#include "core_events.h"


struct qdrc_event_subscription_t {
    DEQ_LINKS_N(CONN,   qdrc_event_subscription_t);
    DEQ_LINKS_N(LINK,   qdrc_event_subscription_t);
    DEQ_LINKS_N(ADDR,   qdrc_event_subscription_t);
    DEQ_LINKS_N(ROUTER, qdrc_event_subscription_t);
    void                    *context;
    qdrc_event_t             events;
    qdrc_connection_event_t  on_conn_event;
    qdrc_link_event_t        on_link_event;
    qdrc_address_event_t     on_addr_event;
    qdrc_router_event_t      on_router_event;
};


qdrc_event_subscription_t *qdrc_event_subscribe_CT(qdr_core_t             *core,
                                                   qdrc_event_t            events,
                                                   qdrc_connection_event_t on_conn_event,
                                                   qdrc_link_event_t       on_link_event,
                                                   qdrc_address_event_t    on_addr_event,
                                                   qdrc_router_event_t     on_router_event,
                                                   void                   *context)
{
    qdrc_event_subscription_t *sub = NEW(qdrc_event_subscription_t);
    ZERO(sub);

    sub->context         = context;
    sub->events          = events;
    sub->on_conn_event   = on_conn_event;
    sub->on_link_event   = on_link_event;
    sub->on_addr_event   = on_addr_event;
    sub->on_router_event = on_router_event;

    assert((events & ~(_QDRC_EVENT_CONN_RANGE | _QDRC_EVENT_LINK_RANGE | _QDRC_EVENT_ADDR_RANGE | _QDRC_EVENT_ROUTER_RANGE)) == 0);
    assert(!(events & _QDRC_EVENT_CONN_RANGE)   || on_conn_event);
    assert(!(events & _QDRC_EVENT_LINK_RANGE)   || on_link_event);
    assert(!(events & _QDRC_EVENT_ADDR_RANGE)   || on_addr_event);
    assert(!(events & _QDRC_EVENT_ROUTER_RANGE) || on_router_event);

    if (events & _QDRC_EVENT_CONN_RANGE)
        DEQ_INSERT_TAIL_N(CONN, core->conn_event_subscriptions, sub);

    if (events & _QDRC_EVENT_LINK_RANGE)
        DEQ_INSERT_TAIL_N(LINK, core->link_event_subscriptions, sub);

    if (events & _QDRC_EVENT_ADDR_RANGE)
        DEQ_INSERT_TAIL_N(ADDR, core->addr_event_subscriptions, sub);

    if (events & _QDRC_EVENT_ROUTER_RANGE)
        DEQ_INSERT_TAIL_N(ROUTER, core->router_event_subscriptions, sub);

    return sub;
}


void qdrc_event_unsubscribe_CT(qdr_core_t *core, qdrc_event_subscription_t *sub)
{
    if (sub->events & _QDRC_EVENT_CONN_RANGE)
        DEQ_REMOVE_N(CONN, core->conn_event_subscriptions, sub);

    if (sub->events & _QDRC_EVENT_LINK_RANGE)
        DEQ_REMOVE_N(LINK, core->link_event_subscriptions, sub);

    if (sub->events & _QDRC_EVENT_ADDR_RANGE)
        DEQ_REMOVE_N(ADDR, core->addr_event_subscriptions, sub);

    if (sub->events & _QDRC_EVENT_ROUTER_RANGE)
        DEQ_REMOVE_N(ROUTER, core->router_event_subscriptions, sub);

    free(sub);
}


void qdrc_event_conn_raise(qdr_core_t *core, qdrc_event_t event, qdr_connection_t *conn)
{
    qdrc_event_subscription_t *sub = DEQ_HEAD(core->conn_event_subscriptions);

    while (sub) {
        if (sub->events & event)
            sub->on_conn_event(sub->context, event, conn);
        sub = DEQ_NEXT_N(CONN, sub);
    }
}


void qdrc_event_link_raise(qdr_core_t *core, qdrc_event_t event, qdr_link_t *link)
{
    qdrc_event_subscription_t *sub = DEQ_HEAD(core->link_event_subscriptions);

    while (sub) {
        if (sub->events & event)
            sub->on_link_event(sub->context, event, link);
        sub = DEQ_NEXT_N(LINK, sub);
    }
}


void qdrc_event_addr_raise(qdr_core_t *core, qdrc_event_t event, qdr_address_t *addr)
{
    qdrc_event_subscription_t *sub = DEQ_HEAD(core->addr_event_subscriptions);

    while (sub) {
        if (sub->events & event)
            sub->on_addr_event(sub->context, event, addr);
        sub = DEQ_NEXT_N(ADDR, sub);
    }
}


void qdrc_event_router_raise(qdr_core_t *core, qdrc_event_t event, qdr_node_t *router)
{
    qdrc_event_subscription_t *sub = DEQ_HEAD(core->router_event_subscriptions);

    while (sub) {
        if (sub->events & event)
            sub->on_router_event(sub->context, event, router);
        sub = DEQ_NEXT_N(ROUTER, sub);
    }
}

