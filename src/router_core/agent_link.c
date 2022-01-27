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

#include <inttypes.h>
#include <stdio.h>

#define QDR_LINK_NAME                     0
#define QDR_LINK_IDENTITY                 1
#define QDR_LINK_TYPE                     2
#define QDR_LINK_LINK_NAME                3
#define QDR_LINK_LINK_TYPE                4
#define QDR_LINK_LINK_DIR                 5
#define QDR_LINK_OWNING_ADDR              6
#define QDR_LINK_CAPACITY                 7
#define QDR_LINK_PEER                     8
#define QDR_LINK_UNDELIVERED_COUNT        9
#define QDR_LINK_UNSETTLED_COUNT          10
#define QDR_LINK_DELIVERY_COUNT           11
#define QDR_LINK_CONNECTION_ID            12
#define QDR_LINK_ADMIN_STATE              13
#define QDR_LINK_OPER_STATE               14
#define QDR_LINK_PRESETTLED_COUNT         15
#define QDR_LINK_DROPPED_PRESETTLED_COUNT 16
#define QDR_LINK_ACCEPTED_COUNT           17
#define QDR_LINK_REJECTED_COUNT           18
#define QDR_LINK_RELEASED_COUNT           19
#define QDR_LINK_MODIFIED_COUNT           20
#define QDR_LINK_DELAYED_1SEC             21
#define QDR_LINK_DELAYED_10SEC            22
#define QDR_LINK_DELIVERIES_STUCK         23
#define QDR_LINK_INGRESS_HISTOGRAM        24
#define QDR_LINK_PRIORITY                 25
#define QDR_LINK_SETTLE_RATE              26
#define QDR_LINK_CREDIT_AVAILABLE         27
#define QDR_LINK_ZERO_CREDIT_SECONDS      28

const char *qdr_link_columns[] =
    {"name",
     "identity",
     "type",
     "linkName",
     "linkType",
     "linkDir",
     "owningAddr",
     "capacity",
     "peer",
     "undeliveredCount",
     "unsettledCount",
     "deliveryCount",
     "connectionId", // The connection id of the owner connection
     "adminStatus",
     "operStatus",
     "presettledCount",
     "droppedPresettledCount",
     "acceptedCount",
     "rejectedCount",
     "releasedCount",
     "modifiedCount",
     "deliveriesDelayed1Sec",
     "deliveriesDelayed10Sec",
     "deliveriesStuck",
     "ingressHistogram",
     "priority",
     "settleRate",
     "creditAvailable",
     "zeroCreditSeconds",
     0};

static const char *qd_link_type_name(qd_link_type_t lt)
{
    switch (lt) {
    case QD_LINK_ENDPOINT      : return "endpoint";
    case QD_LINK_CONTROL       : return "router-control";
    case QD_LINK_ROUTER        : return "inter-router";
    case QD_LINK_EDGE_DOWNLINK : return "edge-downlink";
    }

    return "";
}


static const char *address_key(qdr_address_t *addr)
{
    return addr && addr->hash_handle ? (const char*) qd_hash_key_by_handle(addr->hash_handle) : NULL;
}

static void qdr_agent_write_column_CT(qdr_core_t *core, qd_composed_field_t *body, int col, qdr_link_t *link)
{
    char *text = 0;

    if (!link)
        return;

    switch(col) {
    case QDR_LINK_NAME: {
        if (link->name)
            qd_compose_insert_string(body, link->name);
        else
            qd_compose_insert_null(body);
        break;
    }

    case QDR_LINK_IDENTITY: {
        char id[100];
        snprintf(id, 100, "%"PRId64, link->identity);
        qd_compose_insert_string(body, id);
        break;
    }

    case QDR_LINK_TYPE:
        qd_compose_insert_string(body, "org.apache.qpid.dispatch.router.link");
        break;

    case QDR_LINK_LINK_NAME:
        qd_compose_insert_string(body, link->name);
        break;

    case QDR_LINK_LINK_TYPE:
        qd_compose_insert_string(body, qd_link_type_name(link->link_type));
        break;

    case QDR_LINK_LINK_DIR:
        qd_compose_insert_string(body, link->link_direction == QD_INCOMING ? "in" : "out");
        break;

    case QDR_LINK_OWNING_ADDR:
        if(link->owning_addr)
            qd_compose_insert_string(body, address_key(link->owning_addr));
        else if (link->connected_link && link->connected_link->terminus_addr)
            qd_compose_insert_string(body, link->connected_link->terminus_addr);
        else if (link->terminus_addr)
            qd_compose_insert_string(body, link->terminus_addr);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_LINK_CAPACITY:
        qd_compose_insert_uint(body, link->capacity);
        break;

    case QDR_LINK_PEER:
        if (link->connected_link) {
            char id[100];
            snprintf(id, 100, "%"PRId64, link->connected_link->identity);
            qd_compose_insert_string(body, id);
        } else
            qd_compose_insert_null(body);
        break;

    case QDR_LINK_UNDELIVERED_COUNT:
        qd_compose_insert_ulong(body, DEQ_SIZE(link->undelivered));
        break;

    case QDR_LINK_UNSETTLED_COUNT:
        qd_compose_insert_ulong(body, DEQ_SIZE(link->unsettled));
        break;

    case QDR_LINK_DELIVERY_COUNT:
        qd_compose_insert_ulong(body, link->total_deliveries);
        break;

    case QDR_LINK_CONNECTION_ID: {
        char id[100];
        snprintf(id, 100, "%"PRId64, link->conn->identity);
        qd_compose_insert_string(body, id);
        break;
    }

    case QDR_LINK_ADMIN_STATE:
        text = link->admin_enabled ? "enabled" : "disabled";
        qd_compose_insert_string(body, text);
        break;

    case QDR_LINK_OPER_STATE:
        switch (link->oper_status) {
        case QDR_LINK_OPER_UP:        text = "up";        break;
        case QDR_LINK_OPER_DOWN:      text = "down";      break;
        case QDR_LINK_OPER_QUIESCING: text = "quiescing"; break;
        case QDR_LINK_OPER_IDLE:      text = "idle";      break;
        default:
            text = 0;
        }
        if (!!text)
            qd_compose_insert_string(body, text);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_LINK_PRESETTLED_COUNT:
        qd_compose_insert_ulong(body, link->presettled_deliveries);
        break;

    case QDR_LINK_DROPPED_PRESETTLED_COUNT: {
        qd_compose_insert_ulong(body, link->dropped_presettled_deliveries);
        break;
    }

    case QDR_LINK_ACCEPTED_COUNT:
        qd_compose_insert_ulong(body, link->accepted_deliveries);
        break;

    case QDR_LINK_REJECTED_COUNT:
        qd_compose_insert_ulong(body, link->rejected_deliveries);
        break;

    case QDR_LINK_RELEASED_COUNT:
        qd_compose_insert_ulong(body, link->released_deliveries);
        break;

    case QDR_LINK_MODIFIED_COUNT:
        qd_compose_insert_ulong(body, link->modified_deliveries);
        break;

    case QDR_LINK_DELAYED_1SEC:
        qd_compose_insert_ulong(body, link->deliveries_delayed_1sec);
        break;

    case QDR_LINK_DELAYED_10SEC:
        qd_compose_insert_ulong(body, link->deliveries_delayed_10sec);
        break;

    case QDR_LINK_DELIVERIES_STUCK:
        qd_compose_insert_ulong(body, link->deliveries_stuck);
        break;

    case QDR_LINK_INGRESS_HISTOGRAM:
        if (link->ingress_histogram) {
            qd_compose_start_list(body);
            for (int i = 0; i < qd_bitmask_width(); i++)
                qd_compose_insert_ulong(body, link->ingress_histogram[i]);
            qd_compose_end_list(body);
        } else
            qd_compose_insert_null(body);
        break;

    case QDR_LINK_PRIORITY:
        qd_compose_insert_uint(body, link->priority);
        break;

    case QDR_LINK_SETTLE_RATE: {
        uint32_t delta_time = qdr_core_uptime_ticks(core) - link->core_ticks;
        if (delta_time > 0) {
            if (delta_time > QDR_LINK_RATE_DEPTH)
                delta_time = QDR_LINK_RATE_DEPTH;
            for (uint8_t delta_slots = 0; delta_slots < delta_time; delta_slots++) {
                link->rate_cursor = (link->rate_cursor + 1) % QDR_LINK_RATE_DEPTH;
                link->settled_deliveries[link->rate_cursor] = 0;
            }
            link->core_ticks = qdr_core_uptime_ticks(core);
        }

        uint64_t total = 0;
        for (uint8_t i = 0; i < QDR_LINK_RATE_DEPTH; i++)
            total += link->settled_deliveries[i];
        qd_compose_insert_uint(body, total / QDR_LINK_RATE_DEPTH);
    }
        break;

    case QDR_LINK_CREDIT_AVAILABLE:
        qd_compose_insert_uint(body, link->credit_reported);
        break;

    case QDR_LINK_ZERO_CREDIT_SECONDS:
        if (link->zero_credit_time == 0)
            qd_compose_insert_uint(body, 0);
        else
            qd_compose_insert_uint(body, qdr_core_uptime_ticks(core) - link->zero_credit_time);
        break;

    default:
        qd_compose_insert_null(body);
        break;
    }
}

static void qdr_agent_write_link_CT(qdr_core_t *core, qdr_query_t *query,  qdr_link_t *link)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    if (link) {
        int i = 0;
        while (query->columns[i] >= 0) {
            qdr_agent_write_column_CT(core, body, query->columns[i], link);
            i++;
        }
    }
    qd_compose_end_list(body);
}

static void qdr_manage_advance_link_CT(qdr_query_t *query, qdr_link_t *link)
{
    query->next_offset++;
    link = DEQ_NEXT(link);
    if (link) {
        query->more     = true;
        //query->next_key = qdr_field((const char*) qd_hash_key_by_handle(link->owning_addr->hash_handle));
    } else
        query->more = false;
}


void qdra_link_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    //
    // If the offset goes beyond the set of links, end the query now.
    //
    if (offset >= DEQ_SIZE(core->open_links)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the address at the offset.
    //
    qdr_link_t *link = DEQ_HEAD(core->open_links);
    for (int i = 0; i < offset && link; i++)
        link = DEQ_NEXT(link);
    assert(link);

    if (link) {
        //
        // Write the columns of the link into the response body.
        //
        qdr_agent_write_link_CT(core, query, link);

        //
        // Advance to the next address
        //
        query->next_offset = offset;
        qdr_manage_advance_link_CT(query, link);
    }
    else {
        query->more = false;
    }

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_link_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_link_t *link = 0;

        if (query->next_offset < DEQ_SIZE(core->open_links)) {
            link = DEQ_HEAD(core->open_links);
            for (int i = 0; i < query->next_offset && link; i++)
                link = DEQ_NEXT(link);
        }

    if (link) {
        //
        // Write the columns of the link entity into the response body.
        //
        qdr_agent_write_link_CT(core, query, link);

        //
        // Advance to the next link
        //
        qdr_manage_advance_link_CT(query, link);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static void qdr_manage_write_response_map_CT(qdr_core_t *core, qd_composed_field_t *body, qdr_link_t *link)
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_LINK_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_link_columns[i]);
        qdr_agent_write_column_CT(core, body, i, link);
    }

    qd_compose_end_map(body);
}


static qdr_link_t *qdr_link_find_by_identity(qdr_core_t *core, qd_iterator_t *identity)
{
    if (!identity)
        return 0;

    qdr_link_t *link = DEQ_HEAD(core->open_links);

    while(link) {
        char id[100];
        if (link->identity) {
            snprintf(id, 100, "%"PRId64, link->identity);
            if (qd_iterator_equal(identity, (const unsigned char *)id))
                break;
        }
        link = DEQ_NEXT(link);
    }

    return link;

}


static qdr_link_t *qdr_link_find_by_name(qdr_core_t *core, qd_iterator_t *name)
{
    if(!name)
        return 0;

    qdr_link_t *link = DEQ_HEAD(core->open_links);

    while(link) {
        if (link->name && qd_iterator_equal(name, (const unsigned char *)link->name))
            break;
        link = DEQ_NEXT(link);
    }

    return link;
}


static void qdra_link_update_set_status(qdr_core_t *core, qdr_query_t *query, qdr_link_t *link)
{
    if (link) {
        //link->admin_state = qd_iterator_copy(adm_state);
        qdr_manage_write_response_map_CT(core, query->body, link);
        query->status = QD_AMQP_OK;
    }
    else {
        query->status = QD_AMQP_NOT_FOUND;
        qd_compose_start_map(query->body);
        qd_compose_end_map(query->body);
    }
}

static void qdra_link_set_bad_request(qdr_query_t *query)
{
    query->status = QD_AMQP_BAD_REQUEST;
    qd_compose_start_map(query->body);
    qd_compose_end_map(query->body);
}

void qdra_link_update_CT(qdr_core_t            *core,
                             qd_iterator_t     *name,
                             qd_iterator_t     *identity,
                             qdr_query_t       *query,
                             qd_parsed_field_t *in_body)

{
    // If the request was successful then the statusCode MUST contain 200 (OK) and the body of the message
    // MUST contain a map containing the actual attributes of the entity updated. These MAY differ from those
    // requested.
    // A map containing attributes that are not applicable for the entity being created, or invalid values for a
    // given attribute, MUST result in a failure response with a statusCode of 400 (Bad Request).
    if (qd_parse_is_map(in_body)) {
        // The absence of an attribute name implies that the entity should retain its existing value.
        // If the map contains a key-value pair where the value is null then the updated entity should have no value
        // for that attribute, removing any previous value.

        qd_parsed_field_t *admin_state = qd_parse_value_by_key(in_body, qdr_link_columns[QDR_LINK_ADMIN_STATE]);
        if (admin_state) { //admin state is the only field that can be updated via the update management request
            //qd_iterator_t *adm_state = qd_parse_raw(admin_state);

            if (identity) {
                qdr_link_t *link = qdr_link_find_by_identity(core, identity);
                // TODO - set the adm_state on the link
                qdra_link_update_set_status(core, query, link);
            }
            else if (name) {
                qdr_link_t *link = qdr_link_find_by_name(core, name);
                // TODO - set the adm_state on the link
                qdra_link_update_set_status(core, query, link);
            }
            else {
                qdra_link_set_bad_request(query);
            }
        }
        else
            qdra_link_set_bad_request(query);

    }
    else
        query->status = QD_AMQP_BAD_REQUEST;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

