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
/** Encapsulates a proton message delivery */

#include <qpid/dispatch.h>
#include "dispatch_private.h"
#include "router_private.h"

struct qd_router_delivery_t {
    DEQ_LINKS(qd_router_delivery_t);
    pn_delivery_t        *pn_delivery;
    qd_router_delivery_t *peer;
    void                 *context;
    uint64_t              disposition;
    qd_router_link_t     *link;
    int                   in_fifo;
    bool                  pending_delete;
};
ALLOC_DECLARE(qd_router_delivery_t);
ALLOC_DEFINE(qd_router_delivery_t);

// create a router delivery from a proton delivery received on rlink
qd_router_delivery_t *qd_router_delivery(qd_router_link_t *rlink, pn_delivery_t *pnd)
{
    assert(pn_delivery_get_context(pnd) == 0);
    qd_router_delivery_t *delivery = new_qd_router_delivery_t();
    if (delivery) {
        DEQ_ITEM_INIT(delivery);
        delivery->pn_delivery    = pnd;
        delivery->peer           = 0;
        delivery->context        = 0;
        delivery->disposition    = 0;
        delivery->link           = rlink;
        delivery->in_fifo        = 0;
        delivery->pending_delete = false;
        DEQ_INSERT_TAIL(rlink->deliveries, delivery);
        pn_delivery_set_context(pnd, delivery);
    }

    return delivery;
}


// generate a new router delivery for rlink
qd_router_delivery_t *qd_router_link_new_delivery(qd_router_link_t *rlink, pn_delivery_tag_t tag)
{
    qd_link_t *link = rlink->link;
    pn_link_t *pnl = qd_link_pn(link);

    //
    // If there is a current delivery on this outgoing link, something
    // is wrong with the delivey algorithm.  We assume that the current
    // delivery ('pnd' below) is the one created by pn_delivery.  If it is
    // not, then my understanding of how proton works is incorrect.
    //
    assert(!pn_link_current(pnl));

    pn_delivery(pnl, tag);
    pn_delivery_t *pnd = pn_link_current(pnl);

    if (!pnd)
        return 0;

    return qd_router_delivery(rlink, pnd);
}

// mark the delivery as 'undeliverable-here' so peers won't re-forward it to
// us.
void qd_router_delivery_set_undeliverable_LH(qd_router_delivery_t *delivery)
{
    if (delivery->pn_delivery) {
        pn_disposition_t *dp = pn_delivery_local(delivery->pn_delivery);
        if (dp) {
            pn_disposition_set_undeliverable(dp, true);
        }
    }
}

void qd_router_delivery_free_LH(qd_router_delivery_t *delivery, uint64_t final_disposition)
{
    if (delivery->pn_delivery) {
        if (final_disposition > 0)
            pn_delivery_update(delivery->pn_delivery, final_disposition);
        pn_delivery_set_context(delivery->pn_delivery, 0);
        pn_delivery_settle(delivery->pn_delivery);
        delivery->pn_delivery = 0;
    }

    if (delivery->peer)
        qd_router_delivery_unlink_LH(delivery);

    if (delivery->link) {
        DEQ_REMOVE(delivery->link->deliveries, delivery);
        delivery->link = 0;
    }
    if (delivery->in_fifo)
        delivery->pending_delete = true;
    else {
        free_qd_router_delivery_t(delivery);
    }
}


void qd_router_delivery_link_peers_LH(qd_router_delivery_t *right, qd_router_delivery_t *left)
{
    right->peer = left;
    left->peer  = right;
}


void qd_router_delivery_unlink_LH(qd_router_delivery_t *delivery)
{
    if (delivery->peer) {
        delivery->peer->peer = 0;
        delivery->peer       = 0;
    }
}


void qd_router_delivery_fifo_enter_LH(qd_router_delivery_t *delivery)
{
    delivery->in_fifo++;
}


bool qd_router_delivery_fifo_exit_LH(qd_router_delivery_t *delivery)
{
    delivery->in_fifo--;
    if (delivery->in_fifo == 0 && delivery->pending_delete) {
        free_qd_router_delivery_t(delivery);
        return false;
    }

    return true;
}


void qd_router_delivery_set_context(qd_router_delivery_t *delivery, void *context)
{
    delivery->context = context;
}


void *qd_router_delivery_context(qd_router_delivery_t *delivery)
{
    return delivery->context;
}


qd_router_delivery_t *qd_router_delivery_peer(qd_router_delivery_t *delivery)
{
    return delivery->peer;
}


pn_delivery_t *qd_router_delivery_pn(qd_router_delivery_t *delivery)
{
    return delivery->pn_delivery;
}


void qd_router_delivery_settle(qd_router_delivery_t *delivery)
{
    if (delivery->pn_delivery) {
        pn_delivery_set_context(delivery->pn_delivery, 0);
        pn_delivery_settle(delivery->pn_delivery);
        delivery->pn_delivery = 0;
    }
}


bool qd_router_delivery_settled(qd_router_delivery_t *delivery)
{
    return pn_delivery_settled(delivery->pn_delivery);
}


bool qd_router_delivery_disp_changed(qd_router_delivery_t *delivery)
{
    return delivery->disposition != pn_delivery_remote_state(delivery->pn_delivery);
}


uint64_t qd_router_delivery_disp(qd_router_delivery_t *delivery)
{
    delivery->disposition = pn_delivery_remote_state(delivery->pn_delivery);
    return delivery->disposition;
}


qd_router_link_t *qd_router_delivery_link(qd_router_delivery_t *delivery)
{
    return delivery->link;
}
