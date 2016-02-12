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
#include <stdio.h>

#define QDR_PROV_NAME               0
#define QDR_PROV_IDENTITY           1
#define QDR_PROV_TYPE               2
#define QDR_PROV_OBJECT_TYPE        3
#define QDR_PROV_ADDRESS            4
#define QDR_PROV_CONNECTOR          5
#define QDR_PROV_DIRECTION          6
#define QDR_PROV_SEMANTICS          7
#define QDR_PROV_INGRESS_ADDRESS    8
#define QDR_PROV_EGRESS_ADDRESS     9
#define QDR_PROV_INGRESS_SEMANTICS  10
#define QDR_PROV_EGRESS_SEMANTICS   11

const char *qdr_provisioned_columns[] =
    {"name",
     "identity",
     "type",
     "objectType",
     "address",
     "connector",
     "direction",
     "semantics",
     "ingressAddress",
     "egressAddress",
     "ingressSemantics",
     "egressSemantics",
     0};


static void qdr_prov_insert_column_CT(qdr_provisioned_t *prov, int col, qd_composed_field_t *body, bool as_map)
{
    if (as_map)
        qd_compose_insert_string(body, qdr_provisioned_columns[col]);

    switch(col) {
    case QDR_PROV_NAME:
        if (prov->name) {
            qd_compose_insert_string(body, prov->name);
            break;
        }
        // else fall into IDENTITY

    case QDR_PROV_IDENTITY:

    case QDR_PROV_TYPE:
        qd_compose_insert_string(body, "org.apache.qpid.dispatch.router.provisioned");
        break;

    case QDR_PROV_OBJECT_TYPE:
    case QDR_PROV_ADDRESS:
    case QDR_PROV_CONNECTOR:
    case QDR_PROV_DIRECTION:
    case QDR_PROV_SEMANTICS:
    case QDR_PROV_INGRESS_ADDRESS:
    case QDR_PROV_EGRESS_ADDRESS:
    case QDR_PROV_INGRESS_SEMANTICS:
    case QDR_PROV_EGRESS_SEMANTICS:
    default:
        qd_compose_insert_null(body);
        break;
    }
}


static void qdr_agent_write_prov_CT(qdr_query_t *query,  qdr_provisioned_t *prov)
{
    qd_composed_field_t *body = query->body;

    qd_compose_start_list(body);
    int i = 0;
    while (query->columns[i] >= 0) {
        qdr_prov_insert_column_CT(prov, query->columns[i], body, false);
        i++;
    }
    qd_compose_end_list(body);
}

static void qdr_manage_advance_prov_CT(qdr_query_t *query, qdr_provisioned_t *prov)
{
    query->next_offset++;
    prov = DEQ_NEXT(prov);
    query->more = !!prov;
}


void qdra_provisioned_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = &QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->provisioned)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_provisioned_t *prov = DEQ_HEAD(core->provisioned);
    for (int i = 0; i < offset && prov; i++)
        prov = DEQ_NEXT(prov);
    assert(prov);

    //
    // Write the columns of the object into the response body.
    //
    qdr_agent_write_prov_CT(query, prov);

    //
    // Advance to the next address
    //
    query->next_offset = offset;
    qdr_manage_advance_prov_CT(query, prov);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


void qdra_provisioned_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_provisioned_t *prov = 0;

        if (query->next_offset < DEQ_SIZE(core->provisioned)) {
            prov = DEQ_HEAD(core->provisioned);
            for (int i = 0; i < query->next_offset && prov; i++)
                prov = DEQ_NEXT(prov);
        }

    if (prov) {
        //
        // Write the columns of the provisioned entity into the response body.
        //
        qdr_agent_write_prov_CT(query, prov);

        //
        // Advance to the next object
        //
        qdr_manage_advance_prov_CT(query, prov);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


static qd_address_semantics_t qdra_semantics(qd_parsed_field_t *field)
{
    qd_field_iterator_t *iter = qd_parse_raw(field);
    if (qd_field_iterator_equal(iter, (unsigned char*) "multi"))       return QD_SEMANTICS_MULTICAST_ONCE;
    if (qd_field_iterator_equal(iter, (unsigned char*) "anyClosest"))  return QD_SEMANTICS_ANYCAST_CLOSEST;
    if (qd_field_iterator_equal(iter, (unsigned char*) "anyBalanced")) return QD_SEMANTICS_ANYCAST_BALANCED;
    return QD_SEMANTICS_ANYCAST_BALANCED;
}


static qdr_address_t *qdra_configure_address_CT(qdr_core_t *core, qd_parsed_field_t *addr_field, char cls, qd_address_semantics_t semantics)
{
    if (!addr_field)
        return 0;

    qd_field_iterator_t *iter = qd_parse_raw(addr_field);
    qd_address_iterator_override_prefix(iter, cls);
    qd_address_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);

    qdr_address_t *addr = 0;
    qd_hash_retrieve(core->addr_hash, iter, (void**) &addr);
    if (addr) {
        // Log error TODO
        return 0;
    }

    addr = qdr_address_CT(core, semantics);
    if (!!addr) {
        qd_field_iterator_reset(iter);
        qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_INSERT_TAIL(core->addrs, addr);
    }

    return addr;
}


void qdra_provisioned_create_CT(qdr_core_t *core, qd_field_iterator_t *name,
                                qdr_query_t *query, qd_parsed_field_t *in_body)
{
    // TODO - reject duplicate names

    if (qd_parse_is_map(in_body)) {
        qd_parsed_field_t *type_field     = qd_parse_value_by_key(in_body, qdr_provisioned_columns[QDR_PROV_OBJECT_TYPE]);
        qd_parsed_field_t *addr_field     = qd_parse_value_by_key(in_body, qdr_provisioned_columns[QDR_PROV_ADDRESS]);
        qd_parsed_field_t *conn_field     = qd_parse_value_by_key(in_body, qdr_provisioned_columns[QDR_PROV_CONNECTOR]);
        qd_parsed_field_t *dir_field      = qd_parse_value_by_key(in_body, qdr_provisioned_columns[QDR_PROV_DIRECTION]);
        qd_parsed_field_t *sem_field      = qd_parse_value_by_key(in_body, qdr_provisioned_columns[QDR_PROV_SEMANTICS]);
        //qd_parsed_field_t *in_addr_field  = qd_parse_value_by_key(in_body, qdr_provisioned_columns[QDR_PROV_INGRESS_ADDRESS]);
        //qd_parsed_field_t *out_addr_field = qd_parse_value_by_key(in_body, qdr_provisioned_columns[QDR_PROV_EGRESS_ADDRESS]);
        //qd_parsed_field_t *in_sem_field   = qd_parse_value_by_key(in_body, qdr_provisioned_columns[QDR_PROV_INGRESS_SEMANTICS]);
        //qd_parsed_field_t *out_sem_field  = qd_parse_value_by_key(in_body, qdr_provisioned_columns[QDR_PROV_EGRESS_SEMANTICS]);

        bool still_good = true;
        qdr_provisioned_t *prov = new_qdr_provisioned_t();
        ZERO(prov);

        prov->identity = qdr_identifier(core);
        if (name)
            prov->name = (char*) qd_field_iterator_copy(name);

        if (!type_field)
            prov->object_type = QDR_PROV_TYPE_ADDRESS;
        else {
            qd_field_iterator_t *type_iter = qd_parse_raw(type_field);
            if      (qd_field_iterator_equal(type_iter, (unsigned char*) "address"))
                prov->object_type = QDR_PROV_TYPE_ADDRESS;
            else if (qd_field_iterator_equal(type_iter, (unsigned char*) "linkDestination"))
                prov->object_type = QDR_PROV_TYPE_LINK_DEST;
            else if (qd_field_iterator_equal(type_iter, (unsigned char*) "waypoint"))
                prov->object_type = QDR_PROV_TYPE_WAYPOINT;
            else
                still_good = false;
        }

        prov->semantics = qdra_semantics(sem_field);

        prov->direction_in  = true;
        prov->direction_out = true;
        if (dir_field) {
            qd_field_iterator_t *dir_iter = qd_parse_raw(dir_field);
            if (qd_field_iterator_equal(dir_iter, (unsigned char*) "in"))
                prov->direction_out = false;
            if (qd_field_iterator_equal(dir_iter, (unsigned char*) "out"))
                prov->direction_in = false;
        }

        if (conn_field) {
            qd_field_iterator_t *conn_iter  = qd_parse_raw(conn_field);
            prov->connector_label = (char*) qd_field_iterator_copy(conn_iter);
        }

        switch (prov->object_type) {
        case QDR_PROV_TYPE_ADDRESS:
            prov->addr = qdra_configure_address_CT(core, addr_field, 'Z', prov->semantics);
            break;

        case QDR_PROV_TYPE_LINK_DEST:
            if (prov->direction_in)
                prov->ingress_addr = qdra_configure_address_CT(core, addr_field, 'C', prov->semantics);
            if (prov->direction_out)
                prov->egress_addr  = qdra_configure_address_CT(core, addr_field, 'D', prov->semantics);
            break;

        case QDR_PROV_TYPE_WAYPOINT:
            break;
        }

        if (still_good) {
            // TODO - write response map
            query->status = &QD_AMQP_CREATED;
            DEQ_INSERT_TAIL(core->provisioned, prov);
        } else {
            query->status = &QD_AMQP_BAD_REQUEST;
            if (prov->name)
                free(prov->name);
            free_qdr_provisioned_t(prov);
        }
    }
    else
        query->status = &QD_AMQP_BAD_REQUEST;

    //
    // Enqueue the response.
    //
    if (query->body)
        qdr_agent_enqueue_response_CT(core, query);
    else
        free_qdr_query_t(query);
}
