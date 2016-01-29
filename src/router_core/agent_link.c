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

#define QDR_LINK_NAME               0
#define QDR_LINK_IDENTITY           1
#define QDR_LINK_TYPE               2
#define QDR_LINK_LINK_NAME          3
#define QDR_LINK_LINK_TYPE          4
#define QDR_LINK_LINK_DIR           5
#define QDR_LINK_OWNING_ADDR        6
#define QDR_LINK_CAPACITY           7
#define QDR_LINK_UNDELIVERED_COUNT  8
#define QDR_LINK_UNSETTLED_COUNT    9
#define QDR_LINK_DELIVERY_COUNT     10

static const char *qd_link_type_name(qd_link_type_t lt)
{
    switch (lt) {
    case QD_LINK_ENDPOINT : return "endpoint";
    case QD_LINK_WAYPOINT : return "waypoint";
    case QD_LINK_CONTROL  : return "router-control";
    case QD_LINK_ROUTER   : return "inter-router";
    }

    return "";
}


static const char *address_key(qdr_address_t *addr)
{
    return addr && addr->hash_handle ? (const char*) qd_hash_key_by_handle(addr->hash_handle) : NULL;
}


static void qdr_agent_write_link_CT(qdr_query_t *query,  qdr_link_t *link)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    while (query->columns[i] >= 0) {
        switch(query->columns[i]) {
        case QDR_LINK_IDENTITY:
        case QDR_LINK_NAME:
            // TODO - This needs to be fixed (use connection_id + link_name)
            qd_compose_insert_string(body, "fix-me-hardcoded-for-now" );
            break;
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
            if (link->owning_addr)
                qd_compose_insert_string(body, address_key(link->owning_addr));
            else
                qd_compose_insert_null(body);
            break;

        case QDR_LINK_CAPACITY:
            qd_compose_insert_uint(body, link->capacity);
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

        default:
            qd_compose_insert_null(body);
            break;
        }
        i++;
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
    query->status = &QD_AMQP_OK;

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

    //
    // Write the columns of the link into the response body.
    //
    qdr_agent_write_link_CT(query, link);

    //
    // Advance to the next address
    //
    query->next_offset = offset;
    qdr_manage_advance_link_CT(query, link);

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
        qdr_agent_write_link_CT(query, link);

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
