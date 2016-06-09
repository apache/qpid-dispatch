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

#include "router_core_private.h"
#include <qpid/dispatch/amqp.h>
#include <stdio.h>


static void qdr_link_deliver_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_link_flow_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_send_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard);
static void qdr_update_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard);

//==================================================================================
// Internal Functions
//==================================================================================


//==================================================================================
// Interface Functions
//==================================================================================

qdr_delivery_t *qdr_link_deliver(qdr_link_t *link, qd_message_t *msg, qd_field_iterator_t *ingress,
                                 bool settled, qd_bitmask_t *link_exclusion)
{
    qdr_action_t   *action = qdr_action(qdr_link_deliver_CT, "link_deliver");
    qdr_delivery_t *dlv    = new_qdr_delivery_t();

    ZERO(dlv);
    dlv->ref_count      = 1;    // referenced by the action
    dlv->link           = link;
    dlv->msg            = msg;
    dlv->to_addr        = 0;
    dlv->origin         = ingress;
    dlv->settled        = settled;
    dlv->link_exclusion = link_exclusion;

    action->args.connection.delivery = dlv;
    qdr_action_enqueue(link->core, action);
    return dlv;
}


qdr_delivery_t *qdr_link_deliver_to(qdr_link_t *link, qd_message_t *msg,
                                    qd_field_iterator_t *ingress, qd_field_iterator_t *addr,
                                    bool settled, qd_bitmask_t *link_exclusion)
{
    qdr_action_t   *action = qdr_action(qdr_link_deliver_CT, "link_deliver");
    qdr_delivery_t *dlv    = new_qdr_delivery_t();

    ZERO(dlv);
    dlv->ref_count      = 1;    // referenced by the action
    dlv->link           = link;
    dlv->msg            = msg;
    dlv->to_addr        = addr;
    dlv->origin         = ingress;
    dlv->settled        = settled;
    dlv->link_exclusion = link_exclusion;

    action->args.connection.delivery = dlv;
    qdr_action_enqueue(link->core, action);
    return dlv;
}


qdr_delivery_t *qdr_link_deliver_to_routed_link(qdr_link_t *link, qd_message_t *msg, bool settled,
                                                const uint8_t *tag, int tag_length)
{
    if (tag_length > 32)
        return 0;
    
    qdr_action_t   *action = qdr_action(qdr_link_deliver_CT, "link_deliver");
    qdr_delivery_t *dlv    = new_qdr_delivery_t();

    ZERO(dlv);
    dlv->ref_count = 1;    // referenced by the action
    dlv->link      = link;
    dlv->msg       = msg;
    dlv->settled   = settled;

    action->args.connection.delivery = dlv;
    action->args.connection.tag_length = tag_length;
    memcpy(action->args.connection.tag, tag, tag_length);
    qdr_action_enqueue(link->core, action);
    return dlv;
}


void qdr_link_process_deliveries(qdr_core_t *core, qdr_link_t *link, int credit)
{
    qdr_connection_t *conn = link->conn;
    qdr_delivery_t   *dlv;
    bool              drained = false;
    int               offer   = -1;
    bool              settled = false;

    if (link->link_direction == QD_OUTGOING) {
        while (credit > 0 && !drained) {
            sys_mutex_lock(conn->work_lock);
            dlv = DEQ_HEAD(link->undelivered);
            if (dlv) {
                DEQ_REMOVE_HEAD(link->undelivered);
                settled = dlv->settled;
                if (!settled) {
                    DEQ_INSERT_TAIL(link->unsettled, dlv);
                    dlv->where = QDR_DELIVERY_IN_UNSETTLED;
                } else
                    dlv->where = QDR_DELIVERY_NOWHERE;

                credit--;
                link->total_deliveries++;
                offer = DEQ_SIZE(link->undelivered);
            } else
                drained = true;
            sys_mutex_unlock(conn->work_lock);

            if (dlv) {
                link->credit_to_core--;
                core->deliver_handler(core->user_context, link, dlv, settled);
                if (settled)
                    qdr_delivery_decref(dlv);
            }
        }

        if (drained)
            core->drained_handler(core->user_context, link);
        else if (offer != -1)
            core->offer_handler(core->user_context, link, offer);
    }

    //
    // Handle disposition/settlement updates
    //
    qdr_delivery_ref_list_t updated_deliveries;
    sys_mutex_lock(conn->work_lock);
    DEQ_MOVE(link->updated_deliveries, updated_deliveries);
    sys_mutex_unlock(conn->work_lock);

    qdr_delivery_ref_t *ref = DEQ_HEAD(updated_deliveries);
    while (ref) {
        core->delivery_update_handler(core->user_context, ref->dlv, ref->dlv->disposition, ref->dlv->settled);
        qdr_delivery_decref(ref->dlv);
        qdr_del_delivery_ref(&updated_deliveries, ref);
        ref = DEQ_HEAD(updated_deliveries);
    }
}


void qdr_link_flow(qdr_core_t *core, qdr_link_t *link, int credit, bool drain_mode)
{
    qdr_action_t *action = qdr_action(qdr_link_flow_CT, "link_flow");

    //
    // Compute the number of credits now available that we haven't yet given
    // incrementally to the router core.  i.e. convert absolute credit to
    // incremental credit.
    //
    credit -= link->credit_to_core;
    if (credit < 0)
        credit = 0;
    link->credit_to_core += credit;

    action->args.connection.link   = link;
    action->args.connection.credit = credit;
    action->args.connection.drain  = drain_mode;

    qdr_action_enqueue(core, action);
}


void qdr_send_to1(qdr_core_t *core, qd_message_t *msg, qd_field_iterator_t *addr, bool exclude_inprocess, bool control)
{
    qdr_action_t *action = qdr_action(qdr_send_to_CT, "send_to");
    action->args.io.address           = qdr_field_from_iter(addr);
    action->args.io.message           = qd_message_copy(msg);
    action->args.io.exclude_inprocess = exclude_inprocess;
    action->args.io.control           = control;

    qdr_action_enqueue(core, action);
}


void qdr_send_to2(qdr_core_t *core, qd_message_t *msg, const char *addr, bool exclude_inprocess, bool control)
{
    qdr_action_t *action = qdr_action(qdr_send_to_CT, "send_to");
    action->args.io.address           = qdr_field(addr);
    action->args.io.message           = qd_message_copy(msg);
    action->args.io.exclude_inprocess = exclude_inprocess;
    action->args.io.control           = control;

    qdr_action_enqueue(core, action);
}


void qdr_delivery_update_disposition(qdr_core_t *core, qdr_delivery_t *delivery, uint64_t disposition,
                                     bool settled, bool ref_given)
{
    qdr_action_t *action = qdr_action(qdr_update_delivery_CT, "update_delivery");
    action->args.delivery.delivery    = delivery;
    action->args.delivery.disposition = disposition;
    action->args.delivery.settled     = settled;

    //
    // The delivery's ref_count must be incremented to protect its travels into the
    // core thread.  If the caller has given its reference to us, we can simply use
    // the given ref rather than increment a new one.
    //
    if (!ref_given)
        qdr_delivery_incref(delivery);

    qdr_action_enqueue(core, action);
}


void qdr_delivery_set_context(qdr_delivery_t *delivery, void *context)
{
    delivery->context = context;
}


void *qdr_delivery_get_context(qdr_delivery_t *delivery)
{
    return delivery->context;
}


void qdr_delivery_incref(qdr_delivery_t *delivery)
{
    qdr_connection_t *conn = delivery->link ? delivery->link->conn : 0;

    if (!!conn) {
        sys_mutex_lock(conn->work_lock);
        delivery->ref_count++;
        sys_mutex_unlock(conn->work_lock);
    }
}


void qdr_delivery_decref(qdr_delivery_t *delivery)
{
    qdr_connection_t *conn   = delivery->link ? delivery->link->conn : 0;
    bool              delete = false;
    
    if (!!conn) {
        sys_mutex_lock(conn->work_lock);
        assert(delivery->ref_count > 0);
        delete = --delivery->ref_count == 0;
        sys_mutex_unlock(conn->work_lock);
    }

    if (delete) {
        if (delivery->msg)
            qd_message_free(delivery->msg);
        if (delivery->to_addr)
            qd_field_iterator_free(delivery->to_addr);
        qd_bitmask_free(delivery->link_exclusion);
        free_qdr_delivery_t(delivery);
    }
}


void qdr_delivery_tag(const qdr_delivery_t *delivery, const char **tag, int *length)
{
    *tag    = (const char*) delivery->tag;
    *length = delivery->tag_length;
}


qd_message_t *qdr_delivery_message(const qdr_delivery_t *delivery)
{
    return delivery->msg;
}


//==================================================================================
// In-Thread Functions
//==================================================================================

void qdr_delivery_release_CT(qdr_core_t *core, qdr_delivery_t *dlv)
{
    bool push = dlv->disposition != PN_RELEASED;

    dlv->disposition = PN_RELEASED;
    dlv->settled = true;
    bool moved = qdr_delivery_settled_CT(core, dlv);

    if (push || moved)
        qdr_delivery_push_CT(core, dlv);

    //
    // Remove the unsettled reference
    //
    if (moved)
        qdr_delivery_decref(dlv);
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
        qdr_delivery_decref(dlv);
}


bool qdr_delivery_settled_CT(qdr_core_t *core, qdr_delivery_t *dlv)
{
    //
    // Remove a delivery from its unsettled list.  Side effects include issuing
    // replacement credit and visiting the link-quiescence algorithm
    //
    qdr_link_t       *link  = dlv->link;
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
        int link_bit = link->conn->mask_bit;
        dlv->tracking_addr->outstanding_deliveries[link_bit]--;
        dlv->tracking_addr->tracked_deliveries--;
        dlv->tracking_addr = 0;
    }

    //
    // If this is an incoming link and it is not link-routed or inter-router, issue
    // one replacement credit on the link.  Note that credit on inter-router links is
    // issued immediately even for unsettled deliveries.
    //
    if (moved && link->link_direction == QD_INCOMING &&
        link->link_type != QD_LINK_ROUTER && !link->connected_link)
        qdr_link_issue_credit_CT(core, link, 1, false);

    return moved;
}


static void qdr_link_flow_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_link_t *link   = action->args.connection.link;
    int  credit        = action->args.connection.credit;
    bool drain         = action->args.connection.drain;
    bool activate      = false;
    bool drain_was_set = !link->drain_mode && drain;

    link->drain_mode = drain;

    //
    // If this is an attach-routed link, propagate the flow data downrange.
    // Note that the credit value is incremental.
    //
    if (link->connected_link) {
        qdr_link_t *clink = link->connected_link;

        if (clink->link_direction == QD_INCOMING)
            qdr_link_issue_credit_CT(core, link->connected_link, credit, drain);
        else {
            sys_mutex_lock(clink->conn->work_lock);
            qdr_add_link_ref(&clink->conn->links_with_deliveries, clink, QDR_LINK_LIST_CLASS_DELIVERY);
            sys_mutex_unlock(clink->conn->work_lock);
            qdr_connection_activate_CT(core, clink->conn);
        }

        return;
    }

    //
    // Handle the replenishing of credit outbound
    //
    if (link->link_direction == QD_OUTGOING && (credit > 0 || drain_was_set)) {
        sys_mutex_lock(link->conn->work_lock);
        if (DEQ_SIZE(link->undelivered) > 0 || drain_was_set) {
            qdr_add_link_ref(&link->conn->links_with_deliveries, link, QDR_LINK_LIST_CLASS_DELIVERY);
            activate = true;
        }
        sys_mutex_unlock(link->conn->work_lock);
    }

    //
    // Activate the connection if we have deliveries to send or drain mode was set.
    //
    if (activate)
        qdr_connection_activate_CT(core, link->conn);
}


static int qdr_link_forward_CT(qdr_core_t *core, qdr_link_t *link, qdr_delivery_t *dlv, qdr_address_t *addr)
{
    int fanout = 0;

    if (addr) {
        fanout = qdr_forward_message_CT(core, addr, dlv->msg, dlv, false, link->link_type == QD_LINK_CONTROL);
        if (link->link_type != QD_LINK_CONTROL && link->link_type != QD_LINK_ROUTER)
            addr->deliveries_ingress++;
        link->total_deliveries++;
    }

    if (fanout == 0) {
        if (link->owning_addr) {
            //
            // Message was not delivered and the link is not anonymous.
            // Queue the message for later delivery (when the address gets
            // a valid destination).
            //
            // Use the action-reference as the reference for undelivered rather
            // than decrementing and incrementing the delivery ref_count.
            //
            DEQ_INSERT_TAIL(link->undelivered, dlv);
            dlv->where = QDR_DELIVERY_IN_UNDELIVERED;
        } else {
            //
            // Release the delivery
            //
            qdr_delivery_release_CT(core, dlv);
            qdr_delivery_decref(dlv);
            if (link->link_type == QD_LINK_ROUTER)
                qdr_link_issue_credit_CT(core, link, 1, false);
        }
    } else if (fanout > 0) {
        if (dlv->settled) {
            //
            // The delivery is settled.  Keep it off the unsettled list and issue
            // replacement credit for it now.
            //
            qdr_link_issue_credit_CT(core, link, 1, false);

            //
            // If the delivery has no more references, free it now.
            //
            assert(!dlv->peer);
            qdr_delivery_decref(dlv);
        } else {
            //
            // Again, don't bother decrementing then incrementing the ref_count
            //
            DEQ_INSERT_TAIL(link->unsettled, dlv);
            dlv->where = QDR_DELIVERY_IN_UNSETTLED;

            //
            // If the delivery was received on an inter-router link, issue the credit
            // now.  We don't want to tie inter-router link flow control to unsettled
            // deliveries because it increases the risk of credit starvation if there
            // are many addresses sharing the link.
            //
            if (link->link_type == QD_LINK_ROUTER)
                qdr_link_issue_credit_CT(core, link, 1, false);
        }
    }

    return fanout;
}


static void qdr_link_deliver_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_delivery_t *dlv  = action->args.connection.delivery;
    qdr_link_t     *link = dlv->link;

    //
    // If this is an attach-routed link, put the delivery directly onto the peer link
    //
    if (link->connected_link) {
        qdr_delivery_t *peer = qdr_forward_new_delivery_CT(core, dlv, link->connected_link, dlv->msg);

        //
        // Copy the delivery tag.  For link-routing, the delivery tag must be preserved.
        //
        peer->tag_length = action->args.connection.tag_length;
        memcpy(peer->tag, action->args.connection.tag, peer->tag_length);

        qdr_forward_deliver_CT(core, link->connected_link, peer);
        qd_message_free(dlv->msg);
        dlv->msg = 0;
        link->total_deliveries++;
        if (!dlv->settled) {
            DEQ_INSERT_TAIL(link->unsettled, dlv);
            dlv->where = QDR_DELIVERY_IN_UNSETTLED;

            //
            // Note, in this case the ref_count is left unchanged as we are transferring
            // the action's reference to the unsettled list's reference.
            //
        } else {
            //
            // If the delivery is settled, decrement the ref_count on the delivery.
            // This count was the owned-by-action count.
            //
            qdr_delivery_decref(dlv);
        }
        return;
    }

    //
    // NOTE: The link->undelivered list does not need to be protected by the
    //       connection's work lock for incoming links.  This protection is only
    //       needed for outgoing links.
    //

    if (DEQ_IS_EMPTY(link->undelivered)) {
        qdr_address_t *addr = link->owning_addr;
        if (!addr && dlv->to_addr)
            qd_hash_retrieve(core->addr_hash, dlv->to_addr, (void**) &addr);

        //
        // Give the action reference to the qdr_link_forward function.
        //
        qdr_link_forward_CT(core, link, dlv, addr);
    } else {
        //
        // Take the action reference and use it for undelivered.  Don't decref/incref.
        //
        DEQ_INSERT_TAIL(link->undelivered, dlv);
        dlv->where = QDR_DELIVERY_IN_UNDELIVERED;
    }
}


static void qdr_send_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_field_t  *addr_field = action->args.io.address;
    qd_message_t *msg        = action->args.io.message;

    if (!discard) {
        qdr_address_t *addr = 0;

        qd_address_iterator_reset_view(addr_field->iterator, ITER_VIEW_ADDRESS_HASH);
        qd_hash_retrieve(core->addr_hash, addr_field->iterator, (void**) &addr);
        if (addr) {
            //
            // Forward the message.  We don't care what the fanout count is.
            //
            (void) qdr_forward_message_CT(core, addr, msg, 0, action->args.io.exclude_inprocess,
                                          action->args.io.control);
            addr->deliveries_from_container++;
        } else
            qd_log(core->log, QD_LOG_DEBUG, "In-process send to an unknown address");
    }

    qdr_field_free(addr_field);
    qd_message_free(msg);
}


static void qdr_update_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_delivery_t *dlv        = action->args.delivery.delivery;
    qdr_delivery_t *peer       = dlv->peer;
    bool            push       = false;
    bool            peer_moved = false;
    bool            dlv_moved  = false;
    uint64_t        disp       = action->args.delivery.disposition;
    bool            settled    = action->args.delivery.settled;

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
            push = true;
        }
    }

    if (settled) {
        if (peer) {
            peer->settled = true;
            peer->peer = 0;
            dlv->peer  = 0;

            qdr_delivery_decref(dlv);
            qdr_delivery_decref(peer);

            if (peer->link) {
                peer_moved = qdr_delivery_settled_CT(core, peer);
                if (peer_moved)
                    push = true;
            }
        }

        if (dlv->link)
            dlv_moved = qdr_delivery_settled_CT(core, dlv);
    }

    if (push)
        qdr_delivery_push_CT(core, peer);

    //
    // Release the action reference, possibly freeing the delivery
    //
    qdr_delivery_decref(dlv);

    //
    // Release the unsettled references if the deliveries were moved
    //
    if (dlv_moved)
        qdr_delivery_decref(dlv);
    if (peer_moved)
        qdr_delivery_decref(peer);
}


/**
 * Check the link's accumulated credit.  If the credit given to the connection thread
 * has been issued to Proton, provide the next batch of credit to the connection thread.
 */
void qdr_link_issue_credit_CT(qdr_core_t *core, qdr_link_t *link, int credit, bool drain)
{
    bool drain_changed = link->drain_mode |= drain;
    bool activate      = drain_changed;

    link->drain_mode = drain;
    link->drain_mode_changed = drain_changed;

    link->incremental_credit_CT += credit;
    link->flow_started = true;

    if (link->incremental_credit_CT && link->incremental_credit == 0) {
        //
        // Move the credit from the core-thread value to the connection-thread value.
        //
        link->incremental_credit    = link->incremental_credit_CT;
        link->incremental_credit_CT = 0;
        activate = true;
    }

    if (activate) {
        //
        // Put this link on the connection's has-credit list.
        //
        sys_mutex_lock(link->conn->work_lock);
        qdr_add_link_ref(&link->conn->links_with_credit, link, QDR_LINK_LIST_CLASS_FLOW);
        sys_mutex_unlock(link->conn->work_lock);

        //
        // Activate the connection
        //
        qdr_connection_activate_CT(core, link->conn);
    }
}


/**
 * This function should be called after adding a new destination (subscription, local link,
 * or remote node) to an address.  If this address now has exactly one destination (i.e. it
 * transitioned from unreachable to reachable), make sure any unstarted in-links are issued
 * initial credit.
 *
 * Also, check the inlinks to see if there are undelivered messages.  If so, drain them to
 * the forwarder.
 */
void qdr_addr_start_inlinks_CT(qdr_core_t *core, qdr_address_t *addr)
{
    //
    // If there aren't any inlinks, there's no point in proceeding.
    //
    if (DEQ_SIZE(addr->inlinks) == 0)
        return;

    if (DEQ_SIZE(addr->subscriptions) + DEQ_SIZE(addr->rlinks) + qd_bitmask_cardinality(addr->rnodes) == 1) {
        qdr_link_ref_t *ref = DEQ_HEAD(addr->inlinks);
        while (ref) {
            qdr_link_t *link = ref->link;

            //
            // Issue credit to stalled links
            //
            if (!link->flow_started)
                qdr_link_issue_credit_CT(core, link, link->capacity, false);

            //
            // Drain undelivered deliveries via the forwarder
            //
            if (DEQ_SIZE(link->undelivered) > 0) {
                //
                // Move all the undelivered to a local list in case not all can be delivered.
                // We don't want to loop here forever putting the same messages on the undelivered
                // list.
                //
                qdr_delivery_list_t deliveries;
                DEQ_MOVE(link->undelivered, deliveries);

                qdr_delivery_t *dlv = DEQ_HEAD(deliveries);
                while (dlv) {
                    DEQ_REMOVE_HEAD(deliveries);
                    qdr_link_forward_CT(core, link, dlv, addr);
                    dlv = DEQ_HEAD(deliveries);
                }
            }

            ref = DEQ_NEXT(ref);
        }
    }
}


void qdr_delivery_push_CT(qdr_core_t *core, qdr_delivery_t *dlv)
{
    if (!dlv || !dlv->link)
        return;

    qdr_link_t *link = dlv->link;
    bool activate = false;

    sys_mutex_lock(link->conn->work_lock);
    if (dlv->where != QDR_DELIVERY_IN_UNDELIVERED) {
        dlv->ref_count++; // We have the lock, don't use the incref function
        qdr_add_delivery_ref(&link->updated_deliveries, dlv);
        qdr_add_link_ref(&link->conn->links_with_deliveries, link, QDR_LINK_LIST_CLASS_DELIVERY);
        activate = true;
    }
    sys_mutex_unlock(link->conn->work_lock);

    //
    // Activate the connection
    //
    if (activate)
        qdr_connection_activate_CT(core, link->conn);
}
