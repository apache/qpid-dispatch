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

#define QDR_LINK_LINK_TYPE          0
#define QDR_LINK_LINK_NAME          1
#define QDR_LINK_LINK_DIR           2 //done
#define QDR_LINK_MSG_FIFO_DEPTH     3 //done
#define QDR_LINK_OWNING_ADDR        4 //done
#define QDR_LINK_REMOTE_CONTAINER   5
#define QDR_LINK_NAME               6
#define QDR_LINK_EVENT_FIFO_DEPTH   7 //done
#define QDR_LINK_TYPE               8
#define QDR_LINK_IDENTITY           9

static const char *qd_link_type_names[] = { "endpoint", "waypoint", "inter-router", "inter-area" };
ENUM_DEFINE(qd_link_type, qd_link_type_names);

static const char *address_key(qdr_address_t *addr) {
    return addr && addr->hash_handle ? (const char*) qd_hash_key_by_handle(addr->hash_handle) : NULL;
}

static void qdr_agent_write_link_CT(qdr_query_t *query,  qdr_link_t *link )
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

        case QDR_LINK_REMOTE_CONTAINER:
            qd_compose_insert_null(body); // FIXME
            break;

        case QDR_LINK_LINK_NAME:
            qd_compose_insert_null(body); // FIXME
            break;

        case QDR_LINK_LINK_TYPE:
            qd_compose_insert_string(body, qd_link_type_name(link->link_type));
            break;

        case QDR_LINK_OWNING_ADDR:
            qd_compose_insert_string(body, address_key(link->owning_addr));
            break;

        case QDR_LINK_LINK_DIR:
            qd_compose_insert_string(body, link->link_direction == QD_INCOMING ? "in" : "out");
            break;

        case QDR_LINK_MSG_FIFO_DEPTH:
            qd_compose_insert_ulong(body, DEQ_SIZE(link->msg_fifo));
            break;

        case QDR_LINK_EVENT_FIFO_DEPTH:
            qd_compose_insert_ulong(body, DEQ_SIZE(link->event_fifo));
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
    if (link)
        query->more     = true;
        //query->next_key = qdr_field((const char*) qd_hash_key_by_handle(link->owning_addr->hash_handle));
    else
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
    if (true /*offset >= DEQ_SIZE(core->links)*/) {  // FIXME
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the address at the offset.
    //
    qdr_link_t *link = 0; // DEQ_HEAD(core->links);  FIXME
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

    if (!link) {
        //
        // If the address was removed in the time between this get and the previous one,
        // we need to use the saved offset, which is less efficient.
        //
        if (false /*query->next_offset < DEQ_SIZE(core->links)*/) {  // FIXME
            link = 0; //DEQ_HEAD(core->links);
            for (int i = 0; i < query->next_offset && link; i++)
                link = DEQ_NEXT(link);
        }
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
