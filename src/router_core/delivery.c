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

#include "delivery.h"

#include <inttypes.h>

ALLOC_DEFINE(qdr_delivery_t);


static void qdr_update_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_delete_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_delivery_continue_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_delete_delivery_internal_CT(qdr_core_t *core, qdr_delivery_t *delivery);
static void qdr_delivery_anycast_update_CT(qdr_core_t *core, qdr_delivery_t *dlv,
                                           qdr_delivery_t *peer, bool settled);
static bool qdr_delivery_set_remote_delivery_state_CT(qdr_delivery_t *dlv, uint64_t dispo,
                                                      qd_delivery_state_t *dstate);


void qdr_delivery_set_context(qdr_delivery_t *delivery, void *context)
{
    delivery->context = context;
}


void *qdr_delivery_get_context(const qdr_delivery_t *delivery)
{
    return delivery->context;
}

qdr_link_t *qdr_delivery_link(const qdr_delivery_t *delivery)
{
    return delivery ? safe_deref_qdr_link_t(delivery->link_sp) : 0;
}


bool qdr_delivery_oversize(const qdr_delivery_t *delivery)
{
    return delivery && delivery->msg && qd_message_oversize(delivery->msg);
}


bool qdr_delivery_send_complete(const qdr_delivery_t *delivery)
{
    if (!delivery)
        return false;
    return qd_message_send_complete(delivery->msg);
}


bool qdr_delivery_tag_sent(const qdr_delivery_t *delivery)
{
    if (!delivery)
        return false;
    return qd_message_tag_sent(delivery->msg);
}


void qdr_delivery_set_tag_sent(const qdr_delivery_t *delivery, bool tag_sent)
{
    if (!delivery)
        return;

    qd_message_set_tag_sent(delivery->msg, tag_sent);
}


bool qdr_delivery_receive_complete(const qdr_delivery_t *delivery)
{
    if (!delivery)
        return false;
    return qd_message_receive_complete(delivery->msg);
}


// set the local disposition (to be send to remote endpoint)
void qdr_delivery_set_disposition(qdr_delivery_t *delivery, uint64_t disposition)
{
    if (delivery)
        delivery->disposition = disposition;
}


// get the current local disposition
uint64_t qdr_delivery_disposition(const qdr_delivery_t *delivery)
{
    if (!delivery)
        return 0;
    return delivery->disposition;
}


void qdr_delivery_incref(qdr_delivery_t *delivery, const char *label)
{
    uint32_t rc = sys_atomic_inc(&delivery->ref_count);
    qdr_link_t *link = qdr_delivery_link(delivery);
    if (link)
        qd_log(link->core->log, QD_LOG_DEBUG, DLV_FMT" Delivery incref:    rc:%"PRIu32"  %s",
               DLV_ARGS(delivery), rc + 1, label);
}


void qdr_delivery_set_aborted(const qdr_delivery_t *delivery)
{
    assert(delivery);
    qd_message_set_aborted(delivery->msg);
}

bool qdr_delivery_is_aborted(const qdr_delivery_t *delivery)
{
    if (!delivery)
        return false;
    return qd_message_aborted(delivery->msg);
}

void qdr_delivery_set_presettled(qdr_delivery_t *delivery)
{
    if (delivery)
        delivery->presettled = true;
}

void qdr_delivery_decref(qdr_core_t *core, qdr_delivery_t *delivery, const char *label)
{
    char log_prefix[DLV_ARGS_MAX] = "";
    if (qd_log_enabled(core->log, QD_LOG_DEBUG)) {
        snprintf(log_prefix, DLV_ARGS_MAX, DLV_FMT, DLV_ARGS(delivery));
    }

    uint32_t ref_count = sys_atomic_dec(&delivery->ref_count);
    assert(ref_count > 0);

    qd_log(core->log, QD_LOG_DEBUG, "%s Delivery decref:    rc:%"PRIu32"  %s",
           log_prefix, ref_count - 1, label);

    if (ref_count == 1) {
        // The ref_count was 1 and now it is zero. We are deleting the last ref.
        // The delivery deletion must occur inside the core thread.
        // Queue up an action to do the work.
        //
        qdr_action_t *action = qdr_action(qdr_delete_delivery_CT, "delete_delivery");
        action->args.delivery.delivery = delivery;
        action->label = label;
        qdr_action_enqueue(core, action);
    }
}


void qdr_delivery_tag(const qdr_delivery_t *delivery, const char **tag, int *length)
{
    *tag    = (const char*) delivery->tag;
    *length = delivery->tag_length;
}


qd_message_t *qdr_delivery_message(const qdr_delivery_t *delivery)
{
    if (!delivery)
        return 0;
    return delivery->msg;
}


bool qdr_delivery_presettled(const qdr_delivery_t *delivery)
{
    return delivery->presettled;
}


// remote endpoint modified its disposition and/or settlement
void qdr_delivery_remote_state_updated(qdr_core_t *core, qdr_delivery_t *delivery, uint64_t disposition,
                                       bool settled, qd_delivery_state_t *dstate, bool ref_given)
{
    qdr_action_t *action = qdr_action(qdr_update_delivery_CT, "update_delivery");
    action->args.delivery.delivery    = delivery;
    action->args.delivery.disposition = disposition;
    action->args.delivery.settled     = settled;
    action->args.delivery.dstate      = dstate;

    //
    // The delivery's ref_count must be incremented to protect its travels into the
    // core thread.  If the caller has given its reference to us, we can simply use
    // the given ref rather than increment a new one.
    //
    if (!ref_given)
        qdr_delivery_incref(delivery, "qdr_delivery_update_disposition - add to action list");

    qdr_action_enqueue(core, action);
}


qdr_delivery_t *qdr_delivery_continue(qdr_core_t *core,qdr_delivery_t *in_dlv, bool settled)
{

    qdr_action_t   *action = qdr_action(qdr_delivery_continue_CT, "delivery_continue");
    action->args.delivery.delivery = in_dlv;

    qd_message_t *msg = qdr_delivery_message(in_dlv);
    action->args.delivery.more = !qd_message_receive_complete(msg);
    action->args.delivery.presettled = settled;

    // This incref is for the action reference
    qdr_delivery_incref(in_dlv, "qdr_delivery_continue - add to action list");
    qdr_action_enqueue(core, action);
    return in_dlv;
}


void qdr_delivery_release_CT(qdr_core_t *core, qdr_delivery_t *dlv)
{
    bool push = false;
    bool moved = false;

    if (dlv->presettled) {
        //
        // The delivery is presettled. We simply want to call CORE_delivery_update which in turn will
        // restart stalled links if the q2_holdoff has been hit.
        // For single frame presettled deliveries, calling CORE_delivery_update does not do anything.
        //
        push = true;
    }
    else {
        push = dlv->disposition != PN_RELEASED;
        dlv->disposition = PN_RELEASED;
        dlv->settled = true;
        moved = qdr_delivery_settled_CT(core, dlv);

    }

    if (push || moved)
        qdr_delivery_push_CT(core, dlv);

    //
    // Remove the unsettled reference
    //
    if (moved)
        qdr_delivery_decref_CT(core, dlv, "qdr_delivery_release_CT - remove from unsettled list");
}


void qdr_delivery_failed_CT(qdr_core_t *core, qdr_delivery_t *dlv)
{
    bool push = dlv->disposition != PN_MODIFIED;

    dlv->disposition = PN_MODIFIED;
    dlv->settled = true;
    bool moved = qdr_delivery_settled_CT(core, dlv);

    if (push || moved)
        qdr_delivery_push_CT(core, dlv);

    //
    // Remove the unsettled reference
    //
    if (moved)
        qdr_delivery_decref_CT(core, dlv, "qdr_delivery_failed_CT - remove from unsettled list");
}


// ownership of *error is passed to dlv
void qdr_delivery_reject_CT(qdr_core_t *core, qdr_delivery_t *dlv, qdr_error_t *error)
{
    bool push = dlv->disposition != PN_REJECTED;

    dlv->disposition = PN_REJECTED;
    dlv->settled = true;
    if (error) {
        qd_delivery_state_free(dlv->local_state);
        dlv->local_state = qd_delivery_state_from_error(error);
    }

    bool moved = qdr_delivery_settled_CT(core, dlv);

    if (push || moved)
        qdr_delivery_push_CT(core, dlv);

    //
    // Remove the unsettled reference
    //
    if (moved)
        qdr_delivery_decref_CT(core, dlv, "qdr_delivery_reject_CT - remove from unsettled list");
}


bool qdr_delivery_settled_CT(qdr_core_t *core, qdr_delivery_t *dlv)
{
    //
    // Remove a delivery from its unsettled list.  Side effects include issuing
    // replacement credit and visiting the link-quiescence algorithm
    //
    qdr_link_t       *link  = qdr_delivery_link(dlv);
    qdr_connection_t *conn  = link ? link->conn : 0;
    bool              moved = false;

    if (!link || !conn)
        return false;

    //
    // The lock needs to be acquired only for outgoing links
    //
    if (link->link_direction == QD_OUTGOING)
        sys_mutex_lock(conn->work_lock);

    if (dlv->where == QDR_DELIVERY_IN_UNSETTLED) {
        DEQ_REMOVE(link->unsettled, dlv);
        dlv->where = QDR_DELIVERY_NOWHERE;
        moved = true;
    }

    if (link->link_direction == QD_OUTGOING)
        sys_mutex_unlock(conn->work_lock);

    if (dlv->tracking_addr) {
        dlv->tracking_addr->outstanding_deliveries[dlv->tracking_addr_bit]--;
        dlv->tracking_addr->tracked_deliveries--;

        if (dlv->tracking_addr->tracked_deliveries == 0)
            qdr_check_addr_CT(core, dlv->tracking_addr);

        dlv->tracking_addr = 0;
    }

    //
    // If this is an incoming link and it is not link-routed or inter-router, issue
    // one replacement credit on the link.  Note that credit on inter-router links is
    // issued immediately even for unsettled deliveries.
    //
    if (moved && link->link_direction == QD_INCOMING &&
        link->link_type != QD_LINK_ROUTER && !link->edge && !link->connected_link)
        qdr_link_issue_credit_CT(core, link, 1, false);

    return moved;
}

void qdr_delivery_increment_counters_CT(qdr_core_t *core, qdr_delivery_t *delivery)
{
    qdr_link_t *link = qdr_delivery_link(delivery);
    if (link) {
        bool do_rate = false;

        // router sets the disposition for incoming links, outgoing is set by
        // the remote
        const uint64_t outcome = (link->link_direction == QD_INCOMING)
            ? delivery->disposition   // local
            : delivery->remote_disposition;

        if (delivery->presettled) {
            do_rate = outcome != PN_RELEASED;
            link->presettled_deliveries++;
            if (link->link_direction ==  QD_INCOMING && link->link_type == QD_LINK_ENDPOINT)
                core->presettled_deliveries++;
        }
        else if (outcome == PN_ACCEPTED) {
            do_rate = true;
            link->accepted_deliveries++;
            if (link->link_direction ==  QD_INCOMING)
                core->accepted_deliveries++;
        }
        else if (outcome == PN_REJECTED) {
            do_rate = true;
            link->rejected_deliveries++;
            if (link->link_direction ==  QD_INCOMING)
                core->rejected_deliveries++;
        }
        else if (outcome == PN_RELEASED && !delivery->presettled) {
            link->released_deliveries++;
            if (link->link_direction ==  QD_INCOMING)
                core->released_deliveries++;
        }
        else if (outcome == PN_MODIFIED) {
            link->modified_deliveries++;
            if (link->link_direction ==  QD_INCOMING)
                core->modified_deliveries++;
        }

        qd_log(core->log, QD_LOG_DEBUG,DLV_FMT" Delivery outcome %s: is %s (0x%"PRIX64")",
               DLV_ARGS(delivery), delivery->presettled ? "pre-settled" : "",
               pn_disposition_type_name(outcome), outcome);

        uint32_t delay = qdr_core_uptime_ticks(core) - delivery->ingress_time;
        if (delay > 10) {
            link->deliveries_delayed_10sec++;
            if (link->link_direction ==  QD_INCOMING)
                core->deliveries_delayed_10sec++;
        } else if (delay > 1) {
            link->deliveries_delayed_1sec++;
            if (link->link_direction ==  QD_INCOMING)
                core->deliveries_delayed_1sec++;
        }

        //
        // If this delivery was marked as stuck, decrement the currently-stuck counters in the link and router.
        //
        if (delivery->stuck) {
            link->deliveries_stuck--;
            core->deliveries_stuck--;
        }

        if (qd_bitmask_valid_bit_value(delivery->ingress_index) && link->ingress_histogram)
            link->ingress_histogram[delivery->ingress_index]++;

        //
        // Compute the settlement rate
        //
        if (do_rate) {
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
            link->settled_deliveries[link->rate_cursor]++;
        }
    }
}


static void qdr_delete_delivery_internal_CT(qdr_core_t *core, qdr_delivery_t *delivery)
{
    assert(sys_atomic_get(&delivery->ref_count) == 0);

    if (delivery->msg || delivery->to_addr) {
        qdr_delivery_cleanup_t *cleanup = new_qdr_delivery_cleanup_t();

        DEQ_ITEM_INIT(cleanup);
        cleanup->msg  = delivery->msg;
        cleanup->iter = delivery->to_addr;

        DEQ_INSERT_TAIL(core->delivery_cleanup_list, cleanup);
    }

    if (delivery->tracking_addr) {
        delivery->tracking_addr->outstanding_deliveries[delivery->tracking_addr_bit]--;
        delivery->tracking_addr->tracked_deliveries--;

        if (delivery->tracking_addr->tracked_deliveries == 0)
            qdr_check_addr_CT(core, delivery->tracking_addr);

        delivery->tracking_addr = 0;
    }

    qdr_delivery_increment_counters_CT(core, delivery);

    //
    // Remove any subscription references
    //
    qdr_subscription_ref_t *sub = DEQ_HEAD(delivery->subscriptions);
    while (sub) {
        qdr_del_subscription_ref_CT(&delivery->subscriptions, sub);
        sub = DEQ_HEAD(delivery->subscriptions);
    }

    //
    // Free all the peer qdr_delivery_ref_t references
    //
    qdr_delivery_ref_t *ref = DEQ_HEAD(delivery->peers);
    while (ref) {
        qdr_del_delivery_ref(&delivery->peers, ref);
        ref = DEQ_HEAD(delivery->peers);
    }

    qdr_link_work_release(delivery->link_work);
    qd_bitmask_free(delivery->link_exclusion);
    qd_delivery_state_free(delivery->local_state);
    qd_delivery_state_free(delivery->remote_state);
    sys_mutex_free(delivery->dispo_lock);

    free_qdr_delivery_t(delivery);
}


// Returns the number of peers for dlv
int qdr_delivery_peer_count_CT(const qdr_delivery_t *dlv)
{
    return (dlv->peer) ? 1 : DEQ_SIZE(dlv->peers);
}


void qdr_delivery_link_peers_CT(qdr_delivery_t *in_dlv, qdr_delivery_t *out_dlv)
{
    // If there is no delivery or a peer, we cannot link each other.
    if (!in_dlv || !out_dlv)
        return;

    qdr_link_t *link = qdr_delivery_link(in_dlv);
    if (link) {
        qd_log(link->core->log, QD_LOG_TRACE, DLV_FMT" :in qdr_delivery_link_peers_CT out: "DLV_FMT, DLV_ARGS(in_dlv), DLV_ARGS(out_dlv));
    }

    if (qdr_delivery_peer_count_CT(in_dlv) == 0) {
        // This is the very first peer. Link them up.
        assert(!out_dlv->peer);
        in_dlv->peer = out_dlv;
    }
    else {
        if (in_dlv->peer) {
            // This is the first time we know that in_dlv is going to have more than one peer.
            // There is already a peer in the in_dlv->peer pointer, move it into a list and zero it out.
            qdr_add_delivery_ref_CT(&in_dlv->peers, in_dlv->peer);

            // Zero out the peer pointer. Since there is more than one peer, this peer has been moved to the "peers" linked list.
            // All peers will now reside in the peers linked list. No need to decref/incref here because you are transferring ownership.
            in_dlv->peer = 0;
        }

        qdr_add_delivery_ref_CT(&in_dlv->peers, out_dlv);
    }

    out_dlv->peer = in_dlv;

    qdr_delivery_incref(out_dlv, "qdr_delivery_link_peers_CT - linked to peer (out delivery)");
    qdr_delivery_incref(in_dlv, "qdr_delivery_link_peers_CT - linked to peer (in delivery)");
}


void qdr_delivery_unlink_peers_CT(qdr_core_t *core, qdr_delivery_t *dlv, qdr_delivery_t *peer)
{
    // If there is no delivery or a peer, we cannot proceed.
    if (!dlv || !peer)
        return;

    qd_log(core->log, QD_LOG_TRACE, DLV_FMT" :in qdr_delivery_unlink_peers_CT out: "DLV_FMT,
        DLV_ARGS(dlv), DLV_ARGS(peer));

    // first, drop dlv's reference to its peer
    //
    if (dlv->peer) {
        //
        // This is the easy case. One delivery has only one peer. we can simply
        // zero them out and directly decref.
        //
        assert(dlv->peer == peer);
        dlv->peer  = 0;
    } else {
        //
        // This is the not so easy case
        //
        // dlv has more than one peer, so we have to search for our target peer
        // in the list of peers
        //
        qdr_delivery_ref_t *peer_ref = DEQ_HEAD(dlv->peers);
        while (peer_ref && peer_ref->dlv != peer) {
            peer_ref = DEQ_NEXT(peer_ref);
        }
        assert(peer_ref != 0);
        if (peer_ref == dlv->next_peer_ref)
            // ok to unlink peers while iterating over them
            dlv->next_peer_ref = DEQ_NEXT(peer_ref);
        qdr_del_delivery_ref(&dlv->peers, peer_ref);
    }

    // now drop the peer's reference to dlv
    //
    if (peer->peer) {
        assert(peer->peer == dlv);
        peer->peer = 0;
    }  else {
        qdr_delivery_ref_t *peer_ref = DEQ_HEAD(peer->peers);
        while (peer_ref && peer_ref->dlv != dlv) {
            peer_ref = DEQ_NEXT(peer_ref);
        }
        assert(peer_ref != 0);
        if (peer_ref == peer->next_peer_ref)
            // ok to unlink peers while iterating over them
            peer->next_peer_ref = DEQ_NEXT(peer_ref);
        qdr_del_delivery_ref(&peer->peers, peer_ref);
    }

    qdr_delivery_decref_CT(core, dlv, "qdr_delivery_unlink_peers_CT - unlinked from peer (delivery)");
    qdr_delivery_decref_CT(core, peer, "qdr_delivery_unlink_peers_CT - unlinked from delivery (peer)");
}


qdr_delivery_t *qdr_delivery_first_peer_CT(qdr_delivery_t *dlv)
{
    // What if there are no peers for this delivery?
    if (qdr_delivery_peer_count_CT(dlv) == 0)
        return 0;

    if (dlv->peer) {
        // If there is a dlv->peer, it is the one and only peer.
        return dlv->peer;
    }
    else {
        // The delivery has more than one peer.
        qdr_delivery_ref_t *peer_ref = DEQ_HEAD(dlv->peers);

        // Save the next peer to dlv->next_peer_ref so we can use it when somebody calls qdr_delivery_next_peer_CT
        dlv->next_peer_ref = DEQ_NEXT(peer_ref);

        // Return the first peer.
        return peer_ref->dlv;
    }
}

qdr_delivery_t *qdr_delivery_next_peer_CT(qdr_delivery_t *dlv)
{
    if (dlv->peer) {
        // There is no next_peer if there is only one peer. If there is a non-zero dlv->peer, it is the only peer
        return 0;
    }
    else {
        // There is more than one peer to this delivery.
        qdr_delivery_ref_t *next_peer_ref = dlv->next_peer_ref;
        if (next_peer_ref) {
            // Save the next peer to dlv->next_peer_ref so we can use it when somebody calls qdr_delivery_next_peer_CT
            dlv->next_peer_ref = DEQ_NEXT(dlv->next_peer_ref);
            return next_peer_ref->dlv;
        }
        return 0;
    }
}


void qdr_delivery_decref_CT(qdr_core_t *core, qdr_delivery_t *dlv, const char *label)
{
    uint32_t ref_count = sys_atomic_dec(&dlv->ref_count);
    assert(ref_count > 0);

    qd_log(core->log, QD_LOG_DEBUG, DLV_FMT" Delivery decref_CT: rc:%"PRIu32" %s",
           DLV_ARGS(dlv), ref_count - 1, label);

    if (ref_count == 1)
        qdr_delete_delivery_internal_CT(core, dlv);
}


// the remote endpoint has change the state (disposition) or settlement for the
// delivery.  Update the local state/settlement accordingly.
//
static void qdr_update_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_delivery_t      *dlv      = action->args.delivery.delivery;
    qdr_delivery_t      *peer     = qdr_delivery_first_peer_CT(dlv);
    uint64_t             new_disp = action->args.delivery.disposition;
    bool                 settled  = action->args.delivery.settled;
    qd_delivery_state_t *dstate   = action->args.delivery.dstate;

    if (discard) {
        qdr_delivery_decref_CT(core, action->args.delivery.delivery,
                               "qdr_update_delivery_CT - remove from action on discard");
        qd_delivery_state_free(dstate);
        return;
    }

    if (dlv->multicast) {
        //
        // remote state change for *inbound* multicast delivery,
        // update downstream *outbound* peers
        //
        qdr_delivery_mcast_inbound_update_CT(core, dlv, new_disp, settled);
        qd_delivery_state_free(dstate);  // kgiusti(TODO): handle propagation!

    } else if (peer && peer->multicast) {
        //
        // remote state change for an *outbound* delivery to a multicast address,
        // propagate upstream to *inbound* delivery (peer)
        //
        // coverity[swapped_arguments]
        qdr_delivery_mcast_outbound_update_CT(core, peer, dlv, new_disp, settled);
        qd_delivery_state_free(dstate);  // kgiusti(TODO): handle propagation!

    } else {
        //
        // Anycast forwarding - note: peer _may_ be freed after this!
        //
        qdr_delivery_set_remote_delivery_state_CT(dlv, new_disp, dstate);
        qdr_delivery_anycast_update_CT(core, dlv, peer, settled);
    }

    //
    // Release the action reference, possibly freeing the delivery
    //
    qdr_delivery_decref_CT(core, dlv, "qdr_update_delivery_CT - remove from action");
}


// The remote delivery state (disposition) and/or remote settlement for an
// anycast delivery has changed.  Propagate the changes to its peer delivery.
//
// returns true if ownership of error parameter is taken (do not free it)
//
static void qdr_delivery_anycast_update_CT(qdr_core_t *core, qdr_delivery_t *dlv,
                                           qdr_delivery_t *peer, bool settled)
{
    bool push           = false;
    bool peer_moved     = false;
    bool dlv_moved      = false;
    qdr_link_t *dlink   = qdr_delivery_link(dlv);

    assert(!dlv->multicast);
    assert(!peer || !peer->multicast);

    if (peer)
        qdr_delivery_incref(peer, "qdr_delivery_anycast_update_CT - prevent peer from being freed");

    //
    // Non-multicast Logic:
    //
    // If the disposition or extended delivery-state has changed and there is a
    // peer link, set the disposition/delivery-state of the peer
    // If remote settled, the delivery must be unlinked and freed.
    // If remote settled and there is a peer, the peer shall be settled and
    // unlinked.  It shall not be freed until the connection-side thread
    // settles the PN delivery.
    //
    if (peer) {
        push = qdr_delivery_move_delivery_state_CT(dlv, peer);
    }

    if (settled) {
        if (peer) {
            peer->settled = true;
            if (qdr_delivery_link(peer)) {
                peer_moved = qdr_delivery_settled_CT(core, peer);
            }

            // expect: caller holds reference to dlv, so unlink will not free it
            // expect: peer refcount held locally, so unlink will not free it
            assert(sys_atomic_get(&dlv->ref_count) > 1);
            assert(sys_atomic_get(&peer->ref_count) > 1);
            qdr_delivery_unlink_peers_CT(core, dlv, peer);
        }

        if (dlink)
            // DISPATCH-1544: caller holds reference - dlv not freed
            /* coverity[pass_freed_arg] */
            dlv_moved = qdr_delivery_settled_CT(core, dlv);
    }

    //
    // If the delivery's link has a core endpoint, notify the endpoint of the update
    //
    if (dlink && dlink->core_endpoint)
        qdrc_endpoint_do_update_CT(core, dlink->core_endpoint, dlv, settled);

    if (push || peer_moved)
        // DISPATCH-1544: peer incref-ed above - will not be freed
        /* coverity[deref_arg] */
        qdr_delivery_push_CT(core, peer);

    //
    // Release the unsettled references if the deliveries were moved/settled
    //
    if (dlv_moved)
        qdr_delivery_decref_CT(core, dlv, "qdr_delivery_anycast_update CT - dlv removed from unsettled");
    if (peer_moved)
        qdr_delivery_decref_CT(core, peer, "qdr_delivery_anycast_update_CT - peer removed from unsettled");
    if (peer)
        qdr_delivery_decref_CT(core, peer, "qdr_delivery_anycast_update_CT - allow free of peer");
}


// The remote delivery state (disposition) and/or remote settlement for an
// incoming multicast delivery has changed.  Propagate the changes "downstream"
// to the outbound peers.  Once all peers have settled then settle the in_dlv
//
void qdr_delivery_mcast_inbound_update_CT(qdr_core_t *core, qdr_delivery_t *in_dlv,
                                          uint64_t new_disp, bool settled)
{
    if (!in_dlv)
        return;

    bool update_disp = new_disp && in_dlv->remote_disposition != new_disp;

    assert(in_dlv->multicast);  // expect in_dlv to be the inbound delivery

    qd_log(core->log, QD_LOG_TRACE,
           DLV_FMT" Remote updated mcast delivery disp=0x%"PRIx64" settled=%s",
           DLV_ARGS(in_dlv), new_disp, (settled) ? "True" : "False");

    if (update_disp)
        in_dlv->remote_disposition = new_disp;

    qdr_delivery_t *out_peer = qdr_delivery_first_peer_CT(in_dlv);
    while (out_peer) {
        bool push  = false;
        bool moved  = false;
        bool unlink = false;

        //
        // AMQP 1.0 allows the sender to specify a disposition
        // so forward it along
        //
        if (update_disp && out_peer->disposition != new_disp) {
            out_peer->disposition = new_disp;
            push = true;
            // extension state/error ignored, not sure how
            // that can be supported for mcast...
        }

        //
        // the sender has settled
        //
        if (settled) {
            out_peer->settled = true;
            if (qdr_delivery_link(out_peer)) {
                moved = qdr_delivery_settled_CT(core, out_peer);
            }
            unlink = true;
        }

        if (push || moved) {
            qdr_delivery_push_CT(core, out_peer);
        }

        qd_log(core->log, QD_LOG_TRACE,
               DLV_FMT" Updating mcast delivery out peer "DLV_FMT" updated disp=%s settled=%s",
               DLV_ARGS(in_dlv), DLV_ARGS(out_peer), (push) ? "True" : "False", (unlink) ? "True" : "False");

        if (moved) {
            // expect: in_dlv still references out_peer (with refcount), and
            // there remains an old refcount from being on the unsettled list,
            // so out_peer will not be freed by this decref:
            assert(sys_atomic_get(&out_peer->ref_count) > 1);
            qdr_delivery_decref_CT(core, out_peer,
                                   "qdr_delivery_mcast_inbound_update_CT - removed out_peer from unsettled");
        }

        if (unlink) {
            // expect: in_dlv should not be freed here as caller must hold reference:
            assert(sys_atomic_get(&in_dlv->ref_count) > 1);
            qdr_delivery_unlink_peers_CT(core, in_dlv, out_peer);  // may free out_peer!
        }

        // DISPATCH-1544:
        /* coverity[deref_arg] */
        out_peer = qdr_delivery_next_peer_CT(in_dlv);
    }

    if (settled) {
        assert(qdr_delivery_peer_count_CT(in_dlv) == 0);
        in_dlv->settled = true;
        if (qdr_delivery_settled_CT(core, in_dlv)) {
            qdr_delivery_decref_CT(core, in_dlv,
                                   "qdr_delivery_mcast_inbound_update CT - in_dlv removed from unsettled");
        }
    }
}


// An outgoing peer delivery of an incoming multicast delivery has settled.
// Settle the inbound delivery after all of its outbound deliveries
// have been settled.
//
// Note: this call may free either in_dlv or out_dlv by unlinking them. The
// caller must increment the reference count for these deliveries if they are
// to be referenced after this call.
//
// moved: set to true if in_dlv has been removed from the unsettled list
// return: true if in_dlv has been settled
//
static bool qdr_delivery_mcast_outbound_settled_CT(qdr_core_t *core, qdr_delivery_t *in_dlv,
                                                   qdr_delivery_t *out_dlv, bool *moved)
{
    bool push = false;
    *moved = false;

    assert(in_dlv->multicast && out_dlv->peer == in_dlv);
    assert(qdr_delivery_peer_count_CT(out_dlv) == 1);

    int peer_count = qdr_delivery_peer_count_CT(in_dlv);

    if (peer_count == 1) {
        //
        // This out_dlv is the last outgoing peer so
        // we can now settle in_dlv
        //
        in_dlv->settled = true;
        push = true;
        if (qdr_delivery_link(in_dlv)) {
            *moved = qdr_delivery_settled_CT(core, in_dlv);
        }

        qd_log(core->log, QD_LOG_TRACE,
               DLV_FMT" mcast delivery has settled, disp=0x%"PRIx64,
               DLV_ARGS(in_dlv), in_dlv->disposition);
    } else {

        qd_log(core->log, QD_LOG_TRACE,
               DLV_FMT" mcast delivery out peer "DLV_FMT" has settled, remaining peers=%d",
               DLV_ARGS(in_dlv), DLV_ARGS(out_dlv), peer_count - 1);
    }

    // now settle the peer itself and remove it from link unsettled list

    out_dlv->settled = true;
    if (qdr_delivery_settled_CT(core, out_dlv)) {
        // expect: out_dlv reference count should include the unsettled list and the peer
        assert(sys_atomic_get(&out_dlv->ref_count) > 1);
        qdr_delivery_decref_CT(core, out_dlv, "qdr_delivery_mcast_outbound_settled_CT - out_dlv removed from unsettled");
    }

    // do this last since it may free either dlv or out_dlv:
    qdr_delivery_unlink_peers_CT(core, in_dlv, out_dlv);

    return push;
}


// an outbound mcast delivery has changed its remote state (disposition)
// propagate the change back "upstream" to the inbound delivery
//
// returns true if dlv disposition has been updated
//
static bool qdr_delivery_mcast_outbound_disposition_CT(qdr_core_t *core, qdr_delivery_t *in_dlv,
                                                       qdr_delivery_t *out_dlv, uint64_t new_disp)
{
    // The AMQP 1.0 spec does not define a way to propagate disposition
    // back to the sender in the case of unsettled multicast.  In the
    // case of multiple different terminal states we have to reconcile
    // them. We assign an ad hoc priority to each terminal value and
    // set the final disposition to the highest priority returned
    // across all receivers.
    static const int priority[] = {
        2, // Accepted
        3, // Rejected - highest because reject is a hard error
        0, // Released
        1, // Modified
    };
    bool push = false;

    if (!in_dlv || !out_dlv)
        return push;

    assert(in_dlv->multicast && out_dlv->peer == in_dlv);
    assert(qdr_delivery_peer_count_CT(out_dlv) == 1);

    if (new_disp == 0x33) {  // 0x33 == Declared
        // hack alert - the transaction section of the AMQP 1.0 spec
        // defines the Declared outcome (0x33) terminal state.
        qd_log(core->log, QD_LOG_WARNING,
               DLV_FMT" Transactions are not supported for multicast messages",
               DLV_ARGS(in_dlv));
        new_disp = PN_REJECTED;
    }

    out_dlv->remote_disposition = new_disp;

    if (qd_delivery_state_is_terminal(new_disp)) {
        // our mcast impl ignores non-terminal outcomes

        qd_log(core->log, QD_LOG_TRACE,
               DLV_FMT" mcast delivery out peer "DLV_FMT" disp updated: 0x%"PRIx64,
               DLV_ARGS(in_dlv), DLV_ARGS(out_dlv), new_disp);

        if (in_dlv->mcast_disposition == 0) {
            in_dlv->mcast_disposition = new_disp;
        } else {
            int old_priority = priority[in_dlv->mcast_disposition - PN_ACCEPTED];
            int new_priority = priority[new_disp - PN_ACCEPTED];
            if (new_priority > old_priority)
                in_dlv->mcast_disposition = new_disp;
        }

        // wait until all peers have set terminal state before setting it on
        // the in_dlv
        //
        qdr_delivery_t *peer = qdr_delivery_first_peer_CT(in_dlv);
        while (peer) {
            if (!qd_delivery_state_is_terminal(peer->remote_disposition)) {
                break;
            }
            peer = qdr_delivery_next_peer_CT(in_dlv);
        }

        if (!peer) {  // all peers have set a terminal state
            // TODO(kgiusti) what about error parameter?
            in_dlv->disposition = in_dlv->mcast_disposition;
            push = true;
            qd_log(core->log, QD_LOG_TRACE,
                   DLV_FMT" mcast delivery terminal state set: 0x%"PRIx64,
                   DLV_ARGS(in_dlv), in_dlv->disposition);
        }
    }

    return push;
}


//
// The delivery state (disposition) and/or settlement state for a downstream
// peer delivery of a multicast inbound delivery has changed.  Update the
// inbound delivery to reflect these changes.
//
// Note: if settled is true then in_dlv or out_dlv *may* be released.  Callers
// should increment their reference counts if either of these deliveries are
// referenced after this call.
//
void qdr_delivery_mcast_outbound_update_CT(qdr_core_t *core,
                                           qdr_delivery_t *in_dlv, qdr_delivery_t *out_dlv,
                                           uint64_t new_disp, bool settled)
{
    bool dlv_moved = false;
    bool push_dlv = qdr_delivery_mcast_outbound_disposition_CT(core, in_dlv, out_dlv, new_disp);

    qdr_delivery_incref(in_dlv, "qdr_delivery_mcast_outbound_update_CT - prevent mcast free");

    // Note: qdr_delivery_mcast_outbound_settled_CT may free out_dlv!
    if (settled && qdr_delivery_mcast_outbound_settled_CT(core, in_dlv, out_dlv, &dlv_moved)) {
        push_dlv = true;
    }

    if (push_dlv || dlv_moved) {
        // DISPATCH-1544: in_dlv incref-ed above - will not be freed at this point
        /* coverity[deref_arg] */
        qdr_delivery_push_CT(core, in_dlv);
    }
    if (dlv_moved) {
        qdr_delivery_decref_CT(core, in_dlv, "qdr_delivery_mcast_outbound_update_CT - removed mcast peer from unsettled");
    }
    qdr_delivery_decref_CT(core, in_dlv, "qdr_delivery_mcast_outbound_update_CT - allow mcast peer free");
}


static void qdr_delete_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (!discard)
        qdr_delete_delivery_internal_CT(core, action->args.delivery.delivery);
}


void qdr_delivery_continue_peers_CT(qdr_core_t *core, qdr_delivery_t *in_dlv, bool more)
{
    qdr_delivery_t *peer = qdr_delivery_first_peer_CT(in_dlv);

    while (peer) {
        if (! peer->presettled && in_dlv->presettled) {
            peer->presettled       = in_dlv->presettled;
        }

        qdr_link_t *peer_link = qdr_delivery_link(peer);
        if (!!peer_link) {
            sys_mutex_lock(peer_link->conn->work_lock);
            qdr_link_work_t *work     = peer->link_work;
            bool             activate = false;

            if (peer_link->streaming && !more) {
                if (!peer_link->in_streaming_pool) {
                    // A streaming message has completed.  It is now safe to
                    // re-use this streaming link for the next streaming
                    // message since that new message will not be blocked
                    // indefinitely by the current message.
                    DEQ_INSERT_TAIL_N(STREAMING_POOL, peer_link->conn->streaming_link_pool, peer_link);
                    peer_link->in_streaming_pool = true;
                }
            }

            //
            // Determines if the peer connection can be activated.
            // For a large message, the peer delivery's link_work MUST be at the head of the peer link's work list. This link work is only removed
            // after the streaming message has been sent.
            //
            if (!!work) {
                if (work->processing || work == DEQ_HEAD(peer_link->work_list)) {
                    qdr_add_link_ref(&peer_link->conn->links_with_work[peer_link->priority], peer_link, QDR_LINK_LIST_CLASS_WORK);

                    //
                    // Activate the outgoing connection for later processing.
                    //
                    activate = true;
                }
            }
            sys_mutex_unlock(peer_link->conn->work_lock);

            if (activate)
                qdr_connection_activate_CT(core, peer_link->conn);
        }

        peer = qdr_delivery_next_peer_CT(in_dlv);
    }
}


static void qdr_delivery_continue_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_delivery_t *in_dlv  = action->args.delivery.delivery;
    bool more = action->args.delivery.more;
    bool presettled = action->args.delivery.presettled;

    //
    // If the delivery is already pre-settled, don't do anything with the pre-settled flag.
    //
    // If the in_delivery was not pre-settled, you can go to pre-settled.
    if (! in_dlv->presettled && presettled) {
        in_dlv->presettled = presettled;
    }

    qdr_link_t *link = qdr_delivery_link(in_dlv);

    //
    // If it is already in the undelivered list, don't try to deliver this again.
    //
    if (!!link && in_dlv->where != QDR_DELIVERY_IN_UNDELIVERED) {
        qdr_delivery_continue_peers_CT(core, in_dlv, more);

        qd_message_t *msg = qdr_delivery_message(in_dlv);

        if (!more && !qd_message_is_discard(msg)) {
            //
            // The entire message has now been received. Check to see if there are in process subscriptions that need to
            // receive this message. in process subscriptions, at this time, can deal only with full messages.
            //
            qdr_subscription_ref_t *subref = DEQ_HEAD(in_dlv->subscriptions);
            while (subref) {
                qdr_forward_on_message_CT(core, subref->sub, link, in_dlv->msg, in_dlv);
                qdr_del_subscription_ref_CT(&in_dlv->subscriptions, subref);
                subref = DEQ_HEAD(in_dlv->subscriptions);
            }

            // This is a presettled multi-frame unicast delivery.
            if (in_dlv->settled) {

                //
                // If a delivery is settled but did not go into one of the lists, that means that it is going nowhere.
                // We dont want to deal with such deliveries.
                //
                if (in_dlv->settled && in_dlv->where == QDR_DELIVERY_NOWHERE) {
                    qdr_delivery_decref_CT(core, in_dlv, "qdr_delivery_continue_CT - remove from action 1");
                    return;
                }

                assert(in_dlv->where == QDR_DELIVERY_IN_SETTLED);
                //
                // The router will settle on behalf of the receiver in the case of multicast and send out settled
                // deliveries to the receivers.
                //
                in_dlv->disposition = PN_ACCEPTED;
                qdr_delivery_push_CT(core, in_dlv);

                //
                // The in_dlv has one or more peers. These peers will have to be unlinked.
                //
                qdr_delivery_t *peer = qdr_delivery_first_peer_CT(in_dlv);
                qdr_delivery_t *next_peer = 0;
                while (peer) {
                    next_peer = qdr_delivery_next_peer_CT(in_dlv);
                    // DISPATCH-1544: ensure there is an additional ref held by action
                    // so in_dlv will not be freed when peer unlinked
                    assert(sys_atomic_get(&in_dlv->ref_count) > 1);
                    qdr_delivery_unlink_peers_CT(core, in_dlv, peer);  // peer make be freed here!
                    peer = next_peer;
                }

                // Remove the delivery from the settled list and decref the in_dlv.
                /* coverity[deref_after_free] */
                in_dlv->where = QDR_DELIVERY_NOWHERE;
                DEQ_REMOVE(link->settled, in_dlv);
                // expect: action holds a ref to in_dlv, so it should not be freed here
                assert(sys_atomic_get(&in_dlv->ref_count) > 1);
                qdr_delivery_decref_CT(core, in_dlv, "qdr_delivery_continue_CT - remove from settled list");
            }
        }
    }

    // This decref is for the action reference
    qdr_delivery_decref_CT(core, in_dlv, "qdr_delivery_continue_CT - remove from action 2");
}


void qdr_delivery_push_CT(qdr_core_t *core, qdr_delivery_t *dlv)
{
    qdr_link_t *link = qdr_delivery_link(dlv);
    if (!link)
        return;

    bool activate = false;

    sys_mutex_lock(link->conn->work_lock);
    if (dlv->where != QDR_DELIVERY_IN_UNDELIVERED) {
        qdr_delivery_incref(dlv, "qdr_delivery_push_CT - add to updated list");
        qdr_add_delivery_ref_CT(&link->updated_deliveries, dlv);
        qdr_add_link_ref(&link->conn->links_with_work[link->priority], link, QDR_LINK_LIST_CLASS_WORK);
        activate = true;
    }
    sys_mutex_unlock(link->conn->work_lock);

    //
    // Activate the connection
    //
    if (activate)
        qdr_connection_activate_CT(core, link->conn);
}


// Set remote delivery state when proton indicates that the remote has updated
// delivery. Ownership of *remote_state is passed to the delivery.
//
static bool qdr_delivery_set_remote_delivery_state_CT(qdr_delivery_t *dlv, uint64_t remote_dispo, qd_delivery_state_t *remote_state)
{
    // old state, has not been transfered to peer?
    if (dlv->remote_state) {
        qd_delivery_state_free(dlv->remote_state);
    }
    dlv->remote_state = remote_state;
    dlv->remote_disposition = remote_dispo;
    return true;
}


// Called on the I/O thread: take local delivery state from the delivery for writing to proton.
// Caller assumes ownership of state object.
//
qd_delivery_state_t *qdr_delivery_take_local_delivery_state(qdr_delivery_t *dlv, uint64_t *dispo)
{
    sys_mutex_lock(dlv->dispo_lock);

    qd_delivery_state_t *dstate = dlv->local_state;
    dlv->local_state = 0;
    *dispo = dlv->disposition;
    // if the disposition is not terminal, clear the state to allow another
    // disposition update.  Terminal dispositions are final and never update.
    if (!qd_delivery_state_is_terminal(dlv->disposition))
        dlv->disposition = 0;

    sys_mutex_unlock(dlv->dispo_lock);

    return dstate;
}


// move the REMOTE delivery state from a delivery to the LOCAL delivery state
// of its peer delivery.  This causes the delivery state data to propagate
// from one delivery to another.
//
// returns true if the state has changed and peer needs to be written to
// proton.
//
bool qdr_delivery_move_delivery_state_CT(qdr_delivery_t *dlv, qdr_delivery_t *peer)
{
    qd_delivery_state_t *dstate = dlv->remote_state;
    uint64_t dispo = dlv->remote_disposition;
    dlv->remote_state = 0;

    if (dispo) {
        // must lock peer when modifying local state since I/O thread may be reading
        // it at the same time
        sys_mutex_lock(peer->dispo_lock);

        peer->disposition = dispo;
        qd_delivery_state_t *old = peer->local_state;
        peer->local_state = dstate;

        sys_mutex_unlock(peer->dispo_lock);

        if (old) {
            // old state not consumed by I/O thread?
            qd_delivery_state_free(old);
        }
    }

    return !!dispo;
}
