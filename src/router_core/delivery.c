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

typedef struct qdr_delivery_in_t qdr_delivery_in_t;
typedef struct qdr_delivery_out_t qdr_delivery_out_t;

DEQ_DECLARE(qdr_delivery_out_t, qdr_delivery_out_list_t);

/**
 * An Inbound delivery (received by router)
 *
 * Notes:
 * - lock must be held when out_dlvs are referenced as they may be owned by
 *   other I/O threads
 * - an inbound delivery must exist until all outbound deliveries have been
 *   cleaned up (since the inbound has the lock).  So the inbound delivery is
 *   refcounted by all outbound deliveries
 */
struct qdr_delivery_in_t {
    qdr_delivery_t           base;

    qdr_delivery_out_list_t  out_dlvs;    // corresponding outbound deliveries
};
ALLOC_DECLARE(qdr_delivery_in_t);
ALLOC_DEFINE(qdr_delivery_in_t);


/**
 * An Outbound delivery (sent from router)
 *
 * Notes:
 * - in_dlv will be null if this outbound delivery is for a core link endpoint
 *   outgoing message
 * - the inbound delivery in_dlv is refcounted so it remains present until the
 *   out_dlv releases it
 *
 */
struct qdr_delivery_out_t {
    qdr_delivery_t          base;

    qdr_delivery_in_t      *in_dlv;       // corresponding inbound delivery
    DEQ_LINKS(qdr_delivery_out_t);        // peers of same in_dlv
};
ALLOC_DECLARE(qdr_delivery_out_t);
ALLOC_DEFINE(qdr_delivery_out_t);


static void qdr_delivery_update_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_delete_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_deliver_continue_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static pn_data_t *qdr_delivery_extension_state(qdr_delivery_t *delivery);
static void qdr_delivery_free_extension_state(qdr_delivery_t *delivery);
static void qdr_delete_delivery_internal_CT(qdr_core_t *core, qdr_delivery_t *delivery);


/**
 * Constructor
 */
qdr_delivery_t *qdr_delivery(qdr_link_t *link)
{
    assert(link);
    if (link->link_direction == QD_INCOMING) {
        qdr_delivery_in_t *in_dlv = new_qdr_delivery_in_t();
        ZERO(in_dlv);
        set_safe_ptr_qdr_link_t(link, &in_dlv->base.link_sp);
        in_dlv->base.incoming = true;
        return &in_dlv->base;
    } else {
        qdr_delivery_out_t *out_dlv = new_qdr_delivery_out_t();
        ZERO(out_dlv);
        set_safe_ptr_qdr_link_t(link, &out_dlv->base.link_sp);
        return &out_dlv->base;
    }
}


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


void qdr_delivery_set_disposition(qdr_delivery_t *delivery, uint64_t disposition)
{
    if (delivery)
        delivery->disposition = disposition;
}


uint64_t qdr_delivery_disposition(const qdr_delivery_t *delivery)
{
    if (!delivery)
        return 0;
    return delivery->disposition;
}


void qdr_delivery_incref(qdr_delivery_t *delivery, const char *label)
{
    uint32_t rc = sys_atomic_inc(&delivery->ref_count);
    assert(rc > 0 || !delivery->ref_counted);
    delivery->ref_counted = true;
    qdr_link_t *link = qdr_delivery_link(delivery);
    if (link)
        qd_log(link->core->log, QD_LOG_DEBUG,
               "Delivery incref:    dlv:%lx rc:%"PRIu32" %s", (long) delivery, rc + 1, label);
}


void qdr_delivery_set_aborted(const qdr_delivery_t *delivery, bool aborted)
{
    assert(delivery);
    qd_message_set_aborted(delivery->msg, aborted);
}

bool qdr_delivery_is_aborted(const qdr_delivery_t *delivery)
{
    if (!delivery)
        return false;
    return qd_message_aborted(delivery->msg);
}


void qdr_delivery_decref(qdr_core_t *core, qdr_delivery_t *delivery, const char *label)
{
    uint32_t ref_count = sys_atomic_dec(&delivery->ref_count);
    assert(ref_count > 0);
    qd_log(core->log, QD_LOG_DEBUG, "Delivery decref:    dlv:%lx rc:%"PRIu32" %s", (long) delivery, ref_count - 1, label);

    if (ref_count == 1) {
        //
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


qdr_error_t *qdr_delivery_error(const qdr_delivery_t *delivery)
{
    return delivery->error;
}


bool qdr_delivery_presettled(const qdr_delivery_t *delivery)
{
    return delivery->presettled;
}


/**
 * Proton has updated the disposition and/or the settlement state for the given
 * delivery
 */
void qdr_delivery_update_disposition(qdr_core_t *core, qdr_delivery_t *delivery, uint64_t disposition,
                                     bool settled, qdr_error_t *error, pn_data_t *ext_state, bool ref_given)
{
    qdr_action_t *action = qdr_action(qdr_delivery_update_CT, "update_delivery");
    action->args.delivery.delivery    = delivery;
    action->args.delivery.disposition = disposition;
    action->args.delivery.settled     = settled;
    action->args.delivery.error       = error;

    // handle delivery-state extensions e.g. declared, transactional-state
    qdr_delivery_read_extension_state(delivery, disposition, ext_state, false);

    //
    // The delivery's ref_count must be incremented to protect its travels into the
    // core thread.  If the caller has given its reference to us, we can simply use
    // the given ref rather than increment a new one.
    //
    if (!ref_given)
        qdr_delivery_incref(delivery, "qdr_delivery_update_disposition - add to action list");

    qdr_action_enqueue(core, action);
}


qdr_delivery_t *qdr_deliver_continue(qdr_core_t *core,qdr_delivery_t *in_dlv)
{
    qdr_action_t   *action = qdr_action(qdr_deliver_continue_CT, "deliver_continue");
    action->args.connection.delivery = in_dlv;

    qd_message_t *msg = qdr_delivery_message(in_dlv);
    action->args.connection.more = !qd_message_receive_complete(msg);

    // This incref is for the action reference
    qdr_delivery_incref(in_dlv, "qdr_deliver_continue - add to action list");
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


static void qdr_delivery_increment_counters_CT(qdr_core_t *core, qdr_delivery_t *delivery)
{
    qdr_link_t *link = qdr_delivery_link(delivery);
    if (link) {
        bool do_rate = false;

        if (delivery->presettled) {
            do_rate = delivery->disposition != PN_RELEASED;
            link->presettled_deliveries++;
            if (link->link_direction ==  QD_INCOMING && link->link_type == QD_LINK_ENDPOINT)
                core->presettled_deliveries++;
        }
        else if (delivery->disposition == PN_ACCEPTED) {
            do_rate = true;
            link->accepted_deliveries++;
            if (link->link_direction ==  QD_INCOMING)
                core->accepted_deliveries++;
        }
        else if (delivery->disposition == PN_REJECTED) {
            do_rate = true;
            link->rejected_deliveries++;
            if (link->link_direction ==  QD_INCOMING)
                core->rejected_deliveries++;
        }
        else if (delivery->disposition == PN_RELEASED && !delivery->presettled) {
            link->released_deliveries++;
            if (link->link_direction ==  QD_INCOMING)
                core->released_deliveries++;
        }
        else if (delivery->disposition == PN_MODIFIED) {
            link->modified_deliveries++;
            if (link->link_direction ==  QD_INCOMING)
                core->modified_deliveries++;
        }

        uint32_t delay = core->uptime_ticks - delivery->ingress_time;
        if (delay > 10) {
            link->deliveries_delayed_10sec++;
            if (link->link_direction ==  QD_INCOMING)
                core->deliveries_delayed_10sec++;
        } else if (delay > 1) {
            link->deliveries_delayed_1sec++;
            if (link->link_direction ==  QD_INCOMING)
                core->deliveries_delayed_1sec++;
        }

        if (qd_bitmask_valid_bit_value(delivery->ingress_index) && link->ingress_histogram)
            link->ingress_histogram[delivery->ingress_index]++;

        //
        // Compute the settlement rate
        //
        if (do_rate) {
            uint32_t delta_time = core->uptime_ticks - link->core_ticks;
            if (delta_time > 0) {
                if (delta_time > QDR_LINK_RATE_DEPTH)
                    delta_time = QDR_LINK_RATE_DEPTH;
                for (uint8_t delta_slots = 0; delta_slots < delta_time; delta_slots++) {
                    link->rate_cursor = (link->rate_cursor + 1) % QDR_LINK_RATE_DEPTH;
                    link->settled_deliveries[link->rate_cursor] = 0;
                }
                link->core_ticks = core->uptime_ticks;
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

    qd_bitmask_free(delivery->link_exclusion);
    qdr_error_free(delivery->error);

    if (delivery->incoming) {
        qdr_delivery_in_t *in_dlv = (qdr_delivery_in_t *)delivery;
        assert(DEQ_SIZE(in_dlv->out_dlvs) == 0);
        free_qdr_delivery_in_t(in_dlv);

    } else {
        qdr_delivery_out_t *out_dlv = (qdr_delivery_out_t *)delivery;
        assert(!out_dlv->in_dlv);
        free_qdr_delivery_out_t(out_dlv);
    }
}


/**
 * Set the in delivery's corresponding outbound delivery.
 * Used in the non-multicast case only.
 */
bool qdr_delivery_set_outgoing_CT(qdr_core_t *core, qdr_delivery_t *in_delivery, qdr_delivery_t *out_delivery)
{
    // If there is no delivery or a peer, we cannot link each other.
    if (!in_delivery || !out_delivery)
        return false;

    assert(in_delivery->incoming && !in_delivery->multicast);
    assert(!out_delivery->incoming);

    qdr_delivery_in_t  *in_dlv  = (qdr_delivery_in_t *)in_delivery;
    qdr_delivery_out_t *out_dlv = (qdr_delivery_out_t *)out_delivery;

    // Create peer linkage when:
    // 1) the outgoing delivery is unsettled. This peer linkage is necessary
    // for propagating dispositions from the consumer back to the sender.
    // 2) if the message is not yet been completely received. This linkage will
    // help us stream large pre-settled messages.
    //
    if (!out_dlv->base.settled || !qd_message_receive_complete(out_dlv->base.msg)) {

        DEQ_INSERT_TAIL(in_dlv->out_dlvs, out_dlv);
        qdr_delivery_incref(&out_dlv->base, "qdr_delivery_set_outgoing_CT - linked to peer (out delivery)");
        assert(!out_dlv->in_dlv);
        out_dlv->in_dlv = in_dlv;
        qdr_delivery_incref(&in_dlv->base, "qdr_delivery_set_outgoing_CT - linked to peer (in delivery)");

        return true;
    }
    return false;
}


/**
 * atomically set all outgoing delivery peers of an incoming multicast delivery
 */
void qdr_delivery_set_mcasts_CT(qdr_core_t *core, qdr_delivery_t *in_delivery, const qdr_delivery_mcast_list_t *out_deliveries)
{
    if (!in_delivery) {
        // core generated outbound message, these are simply forwarded
        return;
    }

    assert(in_delivery->incoming && in_delivery->multicast);

    qdr_delivery_in_t  *in_dlv  = (qdr_delivery_in_t *)in_delivery;

    qdr_delivery_mcast_node_t *node = DEQ_HEAD(*out_deliveries);
    while (node) {
        qdr_delivery_out_t *out_dlv = (qdr_delivery_out_t *)node->out_dlv;
        assert(out_dlv && !out_dlv->base.incoming && !out_dlv->in_dlv);
        DEQ_INSERT_TAIL(in_dlv->out_dlvs, out_dlv);
        qdr_delivery_incref(&out_dlv->base, "qdr_delivery_set_mcasts_CT - added to in delivery's out_dlvs");
        out_dlv->in_dlv = in_dlv;
        qdr_delivery_incref(&in_dlv->base, "qdr_delivery_set_mcasts_CT - linked to out delivery");
        node = DEQ_NEXT(node);
    }
}


static qdr_delivery_t *qdr_delivery_peer_CT(qdr_delivery_t *dlv)
{
    if (dlv->incoming) {
        return (qdr_delivery_t *) DEQ_HEAD(((qdr_delivery_in_t *)dlv)->out_dlvs);
    } else {
        return (qdr_delivery_t *) ((qdr_delivery_out_t *)dlv)->in_dlv;
    }
}


static void qdr_delivery_unlink_peers_CT(qdr_core_t *core, qdr_delivery_t *delivery, qdr_delivery_t *peer)
{
    if (!peer || !delivery)
        return;

    qdr_delivery_in_t  *in_dlv; 
    qdr_delivery_out_t *out_dlv;

    if (delivery->incoming) {
        in_dlv  = (qdr_delivery_in_t *) delivery;
        out_dlv = (qdr_delivery_out_t *) peer;
    } else {
        in_dlv  = (qdr_delivery_in_t *) peer;
        out_dlv = (qdr_delivery_out_t *) delivery;
    }
    assert(out_dlv->in_dlv == in_dlv);

    out_dlv->in_dlv = 0;
    DEQ_REMOVE(in_dlv->out_dlvs, out_dlv);

    qdr_delivery_decref_CT(core, &in_dlv->base, "qdr_delivery_unlink_peers_CT - unlink in_dlv");
    qdr_delivery_decref_CT(core, &out_dlv->base, "qdr_delivery_unlink_peers_CT - unlink out_dlv");
}


void qdr_delivery_decref_CT(qdr_core_t *core, qdr_delivery_t *dlv, const char *label)
{
    uint32_t ref_count = sys_atomic_dec(&dlv->ref_count);
    qd_log(core->log, QD_LOG_DEBUG, "Delivery decref_CT: dlv:%lx rc:%"PRIu32" %s", (long) dlv, ref_count - 1, label);
    assert(ref_count > 0);

    if (ref_count == 1)
        qdr_delete_delivery_internal_CT(core, dlv);
}


/**
 * Core handling of proton disposition or settlement change
 */
static void qdr_delivery_update_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_delivery_t *dlv        = action->args.delivery.delivery;
    qdr_delivery_t *peer       = qdr_delivery_peer_CT(dlv);
    bool            push       = false;
    bool            peer_moved = false;
    bool            dlv_moved  = false;
    uint64_t        disp       = action->args.delivery.disposition;
    bool            settled    = action->args.delivery.settled;
    qdr_error_t    *error      = action->args.delivery.error;
    bool error_unassigned      = true;

    qdr_link_t *dlv_link  = qdr_delivery_link(dlv);
    qdr_link_t *peer_link = qdr_delivery_link(peer);

    if (discard) {
        qdr_error_free(error);
        return;
    }

    //
    // Logic:
    //
    // If disposition has changed and there is a peer link, set the disposition of the peer
    // If settled, the delivery must be unlinked and freed.
    // If settled and there is a peer, the peer shall be settled and unlinked.  It shall not
    //   be freed until the connection-side thread settles the PN delivery.
    //
    if (disp != dlv->disposition) {
        //
        // Disposition has changed, propagate the change to the peer delivery.
        //
        dlv->disposition = disp;
        if (peer) {
            peer->disposition = disp;
            peer->error       = error;
            push = true;
            error_unassigned = false;
            qdr_delivery_move_extension_state(dlv, peer, false);
        }
    }

    if (settled) {
        if (peer) {
            peer->settled = true;
            if (peer_link) {
                peer_moved = qdr_delivery_settled_CT(core, peer);
                if (peer_moved)
                    push = true;
            }
            qdr_delivery_unlink_peers_CT(core, dlv, peer);
        }

        if (dlv_link)
            dlv_moved = qdr_delivery_settled_CT(core, dlv);
    }

    //
    // If the delivery's link has a core endpoint, notify the endpoint of the update
    //
    if (dlv_link && dlv_link->core_endpoint)
        qdrc_endpoint_do_update_CT(core, dlv_link->core_endpoint, dlv, settled);

    if (push)
        qdr_delivery_push_CT(core, peer);

    //
    // Release the unsettled references if the deliveries were moved
    //
    if (dlv_moved)
        qdr_delivery_decref_CT(core, dlv, "qdr_update_delivery_CT - removed from unsettled (1)");
    if (peer_moved)
        qdr_delivery_decref_CT(core, peer, "qdr_update_delivery_CT - removed from unsettled (2)");
    if (error_unassigned)
        qdr_error_free(error);

    //
    // Release the action reference, possibly freeing the delivery
    //
    qdr_delivery_decref_CT(core, dlv, "qdr_update_delivery_CT - remove from action");
}


static void qdr_delete_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (!discard)
        qdr_delete_delivery_internal_CT(core, action->args.delivery.delivery);
}


void qdr_delivery_continue_transfer_CT(qdr_core_t *core, qdr_delivery_t *in_delivery)
{
    assert(in_delivery->incoming);

    qdr_delivery_in_t  *in_dlv = (qdr_delivery_in_t *)in_delivery;
    qdr_delivery_out_t *out_dlv = DEQ_HEAD(in_dlv->out_dlvs);
    while (out_dlv) {
        qdr_link_work_t *work     = out_dlv->base.link_work;
        qdr_link_t      *out_link = qdr_delivery_link(&out_dlv->base);

        //
        // Determine if the out connection can be activated.
        // For a large message, the outgoing delivery's link_work MUST be at
        // the head of the outgoing link's work list. This link work is only
        // removed after the streaming message has been sent.
        //
        if (!!work && !!out_link) {
            sys_mutex_lock(out_link->conn->work_lock);
            if (work->processing || work == DEQ_HEAD(out_link->work_list)) {
                // Adding this work at priority 0.
                qdr_add_link_ref(out_link->conn->links_with_work, out_link, QDR_LINK_LIST_CLASS_WORK);
                sys_mutex_unlock(out_link->conn->work_lock);

                //
                // Activate the outgoing connection for later processing.
                //
                qdr_connection_activate_CT(core, out_link->conn);
            }
            else
                sys_mutex_unlock(out_link->conn->work_lock);
        }

        out_dlv = DEQ_NEXT(out_dlv);
    }
}


static void qdr_deliver_continue_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_delivery_t *in_dlv  = action->args.connection.delivery;
    bool more = action->args.connection.more;
    qdr_link_t *link = qdr_delivery_link(in_dlv);

    //
    // If it is already in the undelivered list, don't try to deliver this again.
    //
    if (!!link && in_dlv->where != QDR_DELIVERY_IN_UNDELIVERED) {
        qdr_delivery_continue_transfer_CT(core, in_dlv);

        qd_message_t *msg = qdr_delivery_message(in_dlv);

        if (!more && !qd_message_is_discard(msg)) {
            //
            // The entire message has now been received. Check to see if there are in process subscriptions that need to
            // receive this message. in process subscriptions, at this time, can deal only with full messages.
            //
            qdr_subscription_t *sub = DEQ_HEAD(in_dlv->subscriptions);
            while (sub) {
                DEQ_REMOVE_HEAD(in_dlv->subscriptions);
                qdr_forward_on_message_CT(core, sub, link, in_dlv->msg);
                sub = DEQ_HEAD(in_dlv->subscriptions);
            }

            // This is a multicast delivery or if this is a presettled multi-frame unicast delivery.
            if (in_dlv->multicast || in_dlv->settled) {

                //
                // If a delivery is settled but did not go into one of the lists, that means that it is going nowhere.
                // We dont want to deal with such deliveries.
                //
                if (in_dlv->settled && in_dlv->where == QDR_DELIVERY_NOWHERE) {
                    qdr_delivery_decref_CT(core, in_dlv, "qdr_deliver_continue_CT - remove from action 1");
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
                assert(in_dlv->incoming);
                qdr_delivery_out_list_t out_dlvs;

                DEQ_MOVE(((qdr_delivery_in_t *)in_dlv)->out_dlvs, out_dlvs);

                qdr_delivery_out_t *out_dlv = DEQ_HEAD(out_dlvs);
                while (out_dlv) {
                    DEQ_REMOVE_HEAD(out_dlvs);
                    assert(out_dlv->in_dlv == (qdr_delivery_in_t *)in_dlv);
                    out_dlv->in_dlv = 0;
                    qdr_delivery_decref(core, &out_dlv->base, "qdr_deliver_continue_CT - release out_dlv");
                    qdr_delivery_decref(core, in_dlv, "qdr_deliver_continue_CT - release in_dlv from out_dlv");
                    out_dlv = DEQ_HEAD(out_dlvs);
                }

                // Remove the delivery from the settled list and decref the in_dlv.
                in_dlv->where = QDR_DELIVERY_NOWHERE;
                DEQ_REMOVE(link->settled, in_dlv);
                qdr_delivery_decref_CT(core, in_dlv, "qdr_deliver_continue_CT - remove from settled list");
            }
        }
    }

    // This decref is for the action reference
    qdr_delivery_decref_CT(core, in_dlv, "qdr_deliver_continue_CT - remove from action 2");
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
        // Adding this work at priority 0.
        qdr_add_link_ref(link->conn->links_with_work, link, QDR_LINK_LIST_CLASS_WORK);
        activate = true;
    }
    sys_mutex_unlock(link->conn->work_lock);

    //
    // Activate the connection
    //
    if (activate)
        qdr_connection_activate_CT(core, link->conn);
}

pn_data_t* qdr_delivery_extension_state(qdr_delivery_t *delivery)
{
    if (!delivery->extension_state) {
        delivery->extension_state = pn_data(0);
    }
    pn_data_rewind(delivery->extension_state);
    return delivery->extension_state;
}

void qdr_delivery_free_extension_state(qdr_delivery_t *delivery)
{
    if (delivery->extension_state) {
        pn_data_free(delivery->extension_state);
        delivery->extension_state = 0;
    }
}

void qdr_delivery_write_extension_state(qdr_delivery_t *dlv, pn_delivery_t* pdlv, bool update_disposition)
{
    if (dlv->disposition > PN_MODIFIED) {
        pn_data_copy(pn_disposition_data(pn_delivery_local(pdlv)), qdr_delivery_extension_state(dlv));
        if (update_disposition) pn_delivery_update(pdlv, dlv->disposition);
        qdr_delivery_free_extension_state(dlv);
    }
}


void qdr_delivery_move_extension_state(qdr_delivery_t *src, qdr_delivery_t *dest, bool update_disposition)
{
    if (src->disposition > PN_MODIFIED) {
        pn_data_copy(qdr_delivery_extension_state(dest), qdr_delivery_extension_state(src));
        if (update_disposition) dest->disposition = src->disposition;
        qdr_delivery_free_extension_state(src);
    }
}


void qdr_delivery_copy_extension_state(qdr_delivery_t *src, qdr_delivery_t *dest, bool update_disposition)
{
    if (src->disposition > PN_MODIFIED) {
        pn_data_copy(qdr_delivery_extension_state(dest), qdr_delivery_extension_state(src));
        if (update_disposition) dest->disposition = src->disposition;
    }
}


void qdr_delivery_read_extension_state(qdr_delivery_t *dlv, uint64_t disposition, pn_data_t* disposition_data, bool update_disposition)
{
    if (disposition > PN_MODIFIED) {
        pn_data_rewind(disposition_data);
        pn_data_copy(qdr_delivery_extension_state(dlv), disposition_data);
        if (update_disposition) dlv->disposition = disposition;
    }
}


/**
 * The delivery's link has gone down.
 *
 * Update all of dlv's 'peer' deliveries properly based on their state. If dlv
 * is outgoing the release flag determines if the corresponding inbound
 * delivery can be released.
 */
void qdr_delivery_link_dropped_CT(qdr_core_t *core, qdr_delivery_t *dlv, bool release)
{
    if (dlv->incoming) {

        if (!qdr_delivery_receive_complete(dlv)) {
            qdr_delivery_set_aborted(dlv, true);
            qdr_delivery_continue_transfer_CT(core, dlv);
        }

        // fake a settle to all outbound deliveries in case remote expects
        // settlement
        qdr_delivery_in_t *in_dlv = (qdr_delivery_in_t *)dlv;
        in_dlv->base.settled = true;

        qdr_delivery_out_t *out_dlv = DEQ_HEAD(in_dlv->out_dlvs);
        while (out_dlv) {
            out_dlv->base.settled = true;
            if (qdr_delivery_settled_CT(core, &out_dlv->base))
                qdr_delivery_push_CT(core, &out_dlv->base);
            qdr_delivery_unlink_peers_CT(core, dlv, &out_dlv->base);
            out_dlv = DEQ_HEAD(in_dlv->out_dlvs);
        }

    } else {    // dlv is outgoing

        qdr_delivery_out_t *out_dlv = (qdr_delivery_out_t *)dlv;
        qdr_delivery_in_t  *in_dlv  = out_dlv->in_dlv;

        if (in_dlv) {
            // only update if last outstanding delivery
            if (DEQ_SIZE(in_dlv->out_dlvs) == 1) {
                if (release)
                    qdr_delivery_release_CT(core, &in_dlv->base);
                else
                    qdr_delivery_failed_CT(core, &in_dlv->base);
            }
            qdr_delivery_unlink_peers_CT(core, &in_dlv->base, &out_dlv->base);
        }
    }

    //
    // Updates global and link level delivery counters like
    // presettled_deliveries, accepted_deliveries, released_deliveries etc
    //
    qdr_delivery_increment_counters_CT(core, dlv);
    qd_nullify_safe_ptr(&dlv->link_sp);
}
