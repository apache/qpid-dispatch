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

#include "core_link_endpoint.h"

#include "delivery.h"

#include "qpid/dispatch/alloc.h"

#include <inttypes.h>

struct qdrc_endpoint_t {
    qdrc_endpoint_desc_t *desc;
    void                 *link_context;
    qdr_link_t           *link;
};

ALLOC_DECLARE(qdrc_endpoint_t);
ALLOC_DEFINE(qdrc_endpoint_t);

void qdrc_endpoint_bind_mobile_address_CT(qdr_core_t           *core,
                                          const char           *address,
                                          char                  phase,
                                          qdrc_endpoint_desc_t *desc,
                                          void                 *bind_context)
{
    qdr_address_t *addr = 0;
    qd_iterator_t *iter = qd_iterator_string(address, ITER_VIEW_ADDRESS_HASH);
    qd_iterator_annotate_phase(iter, phase);

    qd_hash_retrieve(core->addr_hash, iter, (void*) &addr);
    if (!addr) {
        qdr_address_config_t   *addr_config = qdr_config_for_address_CT(core, 0, iter);
        qd_address_treatment_t  treatment   = addr_config ? addr_config->treatment : QD_TREATMENT_ANYCAST_BALANCED;
        if (treatment == QD_TREATMENT_UNAVAILABLE)
            treatment = QD_TREATMENT_ANYCAST_BALANCED;
        addr = qdr_address_CT(core, treatment, addr_config);
        DEQ_INSERT_TAIL(core->addrs, addr);
        qd_hash_insert(core->addr_hash, iter, addr, &addr->hash_handle);
    }

    assert(addr->core_endpoint == 0);
    addr->core_endpoint         = desc;
    addr->core_endpoint_context = bind_context;

    qd_iterator_free(iter);
}


qdrc_endpoint_t *qdrc_endpoint_create_link_CT(qdr_core_t           *core,
                                              qdr_connection_t     *conn,
                                              qd_direction_t        dir,
                                              qdr_terminus_t       *source,
                                              qdr_terminus_t       *target,
                                              qdrc_endpoint_desc_t *desc,
                                              void                 *link_context)
{
    qdrc_endpoint_t *ep = new_qdrc_endpoint_t();

    ep->desc         = desc;
    ep->link_context = link_context;
    ep->link         = qdr_create_link_CT(core, conn, QD_LINK_ENDPOINT, dir, source, target,
                                          QD_SSN_CORE_ENDPOINT, QDR_DEFAULT_PRIORITY);
    ep->link->core_endpoint = ep;
    return ep;
}


qd_direction_t qdrc_endpoint_get_direction_CT(const qdrc_endpoint_t *ep)
{
    return !!ep ? (!!ep->link ? ep->link->link_direction : QD_INCOMING) : QD_INCOMING;
}


qdr_connection_t *qdrc_endpoint_get_connection_CT(qdrc_endpoint_t *ep)
{
    return !!ep ? (!!ep->link ? ep->link->conn : 0) : 0;
}


void qdrc_endpoint_second_attach_CT(qdr_core_t *core, qdrc_endpoint_t *ep, qdr_terminus_t *source, qdr_terminus_t *target)
{
    qdr_link_outbound_second_attach_CT(core, ep->link, source, target);
}


void qdrc_endpoint_detach_CT(qdr_core_t *core, qdrc_endpoint_t *ep, qdr_error_t *error)
{
    qdr_link_outbound_detach_CT(core, ep->link, error, QDR_CONDITION_NONE, true);
    if (ep->link->detach_count == 2) {
        qdrc_endpoint_do_cleanup_CT(core, ep);
    }
}


void qdrc_endpoint_flow_CT(qdr_core_t *core, qdrc_endpoint_t *ep, int credit, bool drain)
{
    qdr_link_issue_credit_CT(core, ep->link, credit, drain);
}


void qdrc_endpoint_send_CT(qdr_core_t *core, qdrc_endpoint_t *ep, qdr_delivery_t *dlv, bool presettled)
{
    set_safe_ptr_qdr_link_t(ep->link, &dlv->link_sp);
    dlv->settled       = presettled;
    dlv->presettled    = presettled;
    dlv->tag_length    = 8;
    uint64_t next_tag  = core->next_tag++;
    memcpy(dlv->tag, &next_tag, dlv->tag_length);
    dlv->ingress_index = -1;

    qdr_forward_deliver_CT(core, ep->link, dlv);
}


qdr_delivery_t *qdrc_endpoint_delivery_CT(qdr_core_t *core, qdrc_endpoint_t *endpoint, qd_message_t *message)
{
    qdr_delivery_t *dlv = new_qdr_delivery_t();

    if (endpoint->link->conn)
        endpoint->link->conn->last_delivery_time = qdr_core_uptime_ticks(core);

    ZERO(dlv);
    set_safe_ptr_qdr_link_t(endpoint->link, &dlv->link_sp);
    dlv->msg            = message;
    dlv->tag_length     = 8;
    uint64_t next_tag = core->next_tag++;
    memcpy(dlv->tag, &next_tag, dlv->tag_length);
    dlv->ingress_index  = -1;
    dlv->delivery_id = next_delivery_id();
    dlv->link_id     = endpoint->link->identity;
    dlv->conn_id     = endpoint->link->conn_id;
    dlv->dispo_lock  = sys_mutex();
    qd_log(core->log, QD_LOG_DEBUG, DLV_FMT" Delivery created qdrc_endpoint_delivery_CT", DLV_ARGS(dlv));
    return dlv;
}


void qdrc_endpoint_settle_CT(qdr_core_t *core, qdr_delivery_t *dlv, uint64_t disposition)
{
    //
    // Set the new delivery state
    //
    dlv->disposition = disposition;
    dlv->settled     = true;

    //
    // Activate the connection to push this update out
    //
    qdr_delivery_push_CT(core, dlv);

    //
    // Remove the endpoint's reference
    //
    qdr_delivery_decref_CT(core, dlv, "qdrc_endpoint_settle_CT - no longer held by endpoint");
}


void qdrc_endpoint_do_bound_attach_CT(qdr_core_t *core, qdr_address_t *addr, qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target)
{
    qdrc_endpoint_t *ep = new_qdrc_endpoint_t();
    ZERO(ep);
    ep->desc = addr->core_endpoint;
    ep->link = link;

    link->core_endpoint = ep;
    link->owning_addr   = addr;

    ep->desc->on_first_attach(addr->core_endpoint_context, ep, &ep->link_context, source, target);
}


void qdrc_endpoint_do_second_attach_CT(qdr_core_t *core, qdrc_endpoint_t *ep, qdr_terminus_t *source, qdr_terminus_t *target)
{
    if (!!ep->desc->on_second_attach)
        ep->desc->on_second_attach(ep->link_context, source, target);
}


void qdrc_endpoint_do_deliver_CT(qdr_core_t *core, qdrc_endpoint_t *ep, qdr_delivery_t *dlv)
{
    if (!!ep->desc->on_transfer)
        ep->desc->on_transfer(ep->link_context, dlv, dlv->msg);
}


void qdrc_endpoint_do_update_CT(qdr_core_t *core, qdrc_endpoint_t *ep, qdr_delivery_t *dlv, bool settled)
{
    if (!!ep->desc->on_update)
        ep->desc->on_update(ep->link_context, dlv, settled, dlv->remote_disposition);
}


void qdrc_endpoint_do_flow_CT(qdr_core_t *core, qdrc_endpoint_t *ep, int credit, bool drain)
{
    if (!!ep->desc->on_flow)
        ep->desc->on_flow(ep->link_context, credit, drain);
}


void qdrc_endpoint_do_detach_CT(qdr_core_t *core, qdrc_endpoint_t *ep, qdr_error_t *error, qd_detach_type_t dt)
{
    if (dt == QD_LOST) {
        qdrc_endpoint_do_cleanup_CT(core, ep);
        qdr_error_free(error);

    } else if (ep->link->detach_count == 1) {
        if (!!ep->desc->on_first_detach)
            ep->desc->on_first_detach(ep->link_context, error);
        else {
            qdr_link_outbound_detach_CT(core, ep->link, 0, QDR_CONDITION_NONE, true);
            qdr_error_free(error);
        }
    } else {
        if (!!ep->desc->on_second_detach)
            ep->desc->on_second_detach(ep->link_context, error);
        else
            qdr_error_free(error);
        qdrc_endpoint_do_cleanup_CT(core, ep);
    }
}


void qdrc_endpoint_do_cleanup_CT(qdr_core_t *core, qdrc_endpoint_t *ep)
{
    if (!!ep->desc->on_cleanup)
        ep->desc->on_cleanup(ep->link_context);
    ep->link->core_endpoint = 0;
    free_qdrc_endpoint_t(ep);
}


