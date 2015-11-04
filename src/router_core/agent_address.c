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

#include "agent_address.h"

static const char *qdr_address_columns[] =
    {"name",
     "identity",
     "type",
     "key",
     "inProcess",
     "subscriberCount",
     "remoteCount",
     "hostRouters",
     "deliveriesIngress",
     "deliveriesEgress",
     "deliveriesTransit",
     "deliveriesToContainer",
     "deliveriesFromContainer",
     0};

#define QDR_ADDRESS_NAME                      0
#define QDR_ADDRESS_IDENTITY                  1
#define QDR_ADDRESS_TYPE                      2
#define QDR_ADDRESS_KEY                       3
#define QDR_ADDRESS_IN_PROCESS                4
#define QDR_ADDRESS_SUBSCRIBER_COUNT          5
#define QDR_ADDRESS_REMOTE_COUNT              6
#define QDR_ADDRESS_HOST_ROUTERS              7
#define QDR_ADDRESS_DELIVERIES_INGRESS        8
#define QDR_ADDRESS_DELIVERIES_EGRESS         9
#define QDR_ADDRESS_DELIVERIES_TRANSIT        10
#define QDR_ADDRESS_DELIVERIES_TO_CONTAINER   11
#define QDR_ADDRESS_DELIVERIES_FROM_CONTAINER 12
#define QDR_ADDRESS_COLUMN_COUNT              13

static void qdr_manage_write_address_CT(qdr_query_t *query, qdr_address_t *addr)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    while (query->columns[i] >= 0) {
        switch(query->columns[i]) {
        case QDR_ADDRESS_NAME:
        case QDR_ADDRESS_IDENTITY:
            break;

        case QDR_ADDRESS_TYPE:
            qd_compose_insert_string(body, "org.apache.qpid.dispatch.router.address");
            break;

        case QDR_ADDRESS_KEY:
            if (addr->hash_handle)
                qd_compose_insert_string(body, (const char*) qd_hash_key_by_handle(addr->hash_handle));
            else
                qd_compose_insert_null(body);
            break;

        case QDR_ADDRESS_IN_PROCESS:
            qd_compose_insert_bool(body, addr->on_message != 0);
            break;

        case QDR_ADDRESS_SUBSCRIBER_COUNT:
            qd_compose_insert_uint(body, DEQ_SIZE(addr->rlinks));
            break;

        case QDR_ADDRESS_REMOTE_COUNT:
            qd_compose_insert_uint(body, DEQ_SIZE(addr->rnodes));
            break;

        case QDR_ADDRESS_HOST_ROUTERS:
            qd_compose_insert_null(body);  // TEMP
            break;

        case QDR_ADDRESS_DELIVERIES_INGRESS:
            qd_compose_insert_ulong(body, addr->deliveries_ingress);
            break;

        case QDR_ADDRESS_DELIVERIES_EGRESS:
            qd_compose_insert_ulong(body, addr->deliveries_egress);
            break;

        case QDR_ADDRESS_DELIVERIES_TRANSIT:
            qd_compose_insert_ulong(body, addr->deliveries_transit);
            break;

        case QDR_ADDRESS_DELIVERIES_TO_CONTAINER:
            qd_compose_insert_ulong(body, addr->deliveries_to_container);
            break;

        case QDR_ADDRESS_DELIVERIES_FROM_CONTAINER:
            qd_compose_insert_ulong(body, addr->deliveries_from_container);
            break;

        default:
            qd_compose_insert_null(body);
            break;
        }
    }
    qd_compose_end_list(body);
}


static void qdr_manage_advance_address_CT(qdr_query_t *query, qdr_address_t *addr)
{
    query->next_offset++;
    addr = DEQ_NEXT(addr);
    if (addr) {
        query->more     = true;
        query->next_key = qdr_field((const char*) qd_hash_key_by_handle(addr->hash_handle));
    } else
        query->more = false;
}


void qdra_address_set_columns(qdr_query_t *query, qd_parsed_field_t *attribute_names)
{
    if (!attribute_names ||
        (qd_parse_tag(attribute_names) != QD_AMQP_LIST8 &&
         qd_parse_tag(attribute_names) != QD_AMQP_LIST32) ||
        qd_parse_sub_count(attribute_names) == 0) {
        //
        // Either the attribute_names field is absent, it's not a list, or it's an empty list.
        // In this case, we will include all available attributes.
        //
        int i;
        for (i = 0; i < QDR_ADDRESS_COLUMN_COUNT; i++)
            query->columns[i] = i;
        query->columns[i] = -1;
        return;
    }

    //
    // We have a valid, non-empty attribute list.  Set the columns appropriately.
    //
    uint32_t count = qd_parse_sub_count(attribute_names);
    uint32_t idx;

    for (idx = 0; idx < count; idx++) {
        qd_parsed_field_t *name = qd_parse_sub_value(attribute_names, idx);
        if (!name || (qd_parse_tag(name) != QD_AMQP_STR8_UTF8 && qd_parse_tag(name) != QD_AMQP_STR32_UTF8))
            query->columns[idx] = QDR_AGENT_COLUMN_NULL;
        else {
            int j = 0;
            while (qdr_address_columns[j]) {
                qd_field_iterator_t *iter = qd_parse_raw(name);
                if (qd_field_iterator_equal(iter, (const unsigned char*) qdr_address_columns[j])) {
                    query->columns[idx] = j;
                    break;
                }
            }
        }
    }
}


void qdra_address_emit_columns(qdr_query_t *query)
{
    qd_compose_start_list(query->body);
    int i = 0;
    while (query->columns[i] >= 0) {
        assert(query->columns[i] < QDR_ADDRESS_COLUMN_COUNT);
        qd_compose_insert_string(query->body, qdr_address_columns[query->columns[i]]);
        i++;
    }
    qd_compose_end_list(query->body);
}


void qdra_address_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = &QD_AMQP_OK;

    //
    // If the offset goes beyond the set of addresses, end the query now.
    //
    if (offset >= DEQ_SIZE(core->addrs)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the address at the offset.
    //
    qdr_address_t *addr = DEQ_HEAD(core->addrs);
    for (int i = 0; i < offset && addr; i++)
        addr = DEQ_NEXT(addr);
    assert(addr != 0);

    //
    // Write the columns of the address entity into the response body.
    //
    qdr_manage_write_address_CT(query, addr);

    //
    // Advance to the next address
    //
    query->next_offset = offset;
    qdr_manage_advance_address_CT(query, addr);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_address_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_address_t *addr = 0;

    //
    // Use the stored key to try to find the next entry in the table.
    //
    if (query->next_key) {
        qd_hash_retrieve(core->addr_hash, query->next_key->iterator, (void**) &addr);
        qdr_field_free(query->next_key);
        query->next_key = 0;
    }
    if (!addr) {
        //
        // If the address was removed in the time between this get and the previous one,
        // we need to use the saved offset, which is less efficient.
        //
        if (query->next_offset < DEQ_SIZE(core->addrs)) {
            addr = DEQ_HEAD(core->addrs);
            for (int i = 0; i < query->next_offset && addr; i++)
                addr = DEQ_NEXT(addr);
        }
    }

    if (addr) {
        //
        // Write the columns of the address entity into the response body.
        //
        qdr_manage_write_address_CT(query, addr);

        //
        // Advance to the next address
        //
        qdr_manage_advance_address_CT(query, addr);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

