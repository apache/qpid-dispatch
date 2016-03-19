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


qdr_delivery_t *qdr_link_deliver_to_routed_link(qdr_link_t *link, qd_message_t *msg, bool settled)
{
    qdr_action_t   *action = qdr_action(qdr_link_deliver_CT, "link_deliver");
    qdr_delivery_t *dlv    = new_qdr_delivery_t();

    ZERO(dlv);
    dlv->link    = link;
    dlv->msg     = msg;
    dlv->settled = settled;

    action->args.connection.delivery = dlv;
    qdr_action_enqueue(link->core, action);
    return dlv;
}


void qdr_link_process_deliveries(qdr_core_t *core, qdr_link_t *link, int credit)
{
    qdr_connection_t *conn = link->conn;
    qdr_delivery_t   *dlv;
    bool              drained = false;
    int               offer;

    while (credit > 0 && !drained) {
        sys_mutex_lock(conn->work_lock);
        dlv = DEQ_HEAD(link->undelivered);
        if (dlv) {
            DEQ_REMOVE_HEAD(link->undelivered);
            if (!dlv->settled)
                DEQ_INSERT_TAIL(link->unsettled, dlv);
            credit--;
            link->total_deliveries++;
            offer = DEQ_SIZE(link->undelivered);
        } else
            drained = true;
        sys_mutex_unlock(conn->work_lock);

        if (dlv) {
            link->credit_to_core--;
            core->deliver_handler(core->user_context, link, dlv, dlv->settled);
            if (dlv->settled)
                qdr_delivery_free(dlv);
        }
    }

    if (drained)
        core->drained_handler(core->user_context, link);
    else
        core->offer_handler(core->user_context, link, offer);

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
        if (ref->dlv->settled)
            qdr_delivery_free(ref->dlv);
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


void qdr_delivery_free(qdr_delivery_t *delivery)
{
    if (delivery->msg)
        qd_message_free(delivery->msg);
    if (delivery->to_addr)
        qd_field_iterator_free(delivery->to_addr);
    qd_bitmask_free(delivery->link_exclusion);
    free_qdr_delivery_t(delivery);
}


void qdr_delivery_release_CT(qdr_core_t *core, qdr_delivery_t *delivery)
{
}


void qdr_delivery_remove_unsettled_CT(qdr_core_t *core, qdr_delivery_t *delivery)
{
    //
    // Remove a delivery from its unsettled list.  Side effects include issuing
    // replacement credit and visiting the link-quiescence algorithm
    //
}


void qdr_delivery_update_disposition(qdr_core_t *core, qdr_delivery_t *delivery, uint64_t disposition, bool settled)
{
    qdr_action_t *action = qdr_action(qdr_update_delivery_CT, "update_delivery");
    action->args.delivery.delivery    = delivery;
    action->args.delivery.disposition = disposition;
    action->args.delivery.settled     = settled;

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


void qdr_delivery_tag(const qdr_delivery_t *delivery, const char **tag, int *length)
{
    *tag    = (const char*) &delivery->tag;
    *length = sizeof(uint64_t);
}


qd_message_t *qdr_delivery_message(const qdr_delivery_t *delivery)
{
    return delivery->msg;
}


//==================================================================================
// In-Thread Functions
//==================================================================================

static void qdr_link_flow_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    if (discard)
        return;

    qdr_link_t *link = action->args.connection.link;
    int  credit      = action->args.connection.credit;
    bool drain       = action->args.connection.drain;
    bool activate    = false;

    //
    // If this is an attach-routed link, propagate the flow data downrange.
    // Note that the credit value is incremental.
    //
    if (link->connected_link)
        qdr_link_issue_credit_CT(core, link->connected_link, credit);

    //
    // Handle the replenishing of credit outbound
    //
    if (link->link_direction == QD_OUTGOING && credit > 0) {
        sys_mutex_lock(link->conn->work_lock);
        if (DEQ_SIZE(link->undelivered) > 0) {
            qdr_add_link_ref(&link->conn->links_with_deliveries, link, QDR_LINK_LIST_CLASS_DELIVERY);
            activate = true;
        }
        sys_mutex_unlock(link->conn->work_lock);
    }

    //
    // Record the drain mode for the link
    //
    link->drain_mode = drain;

    if (activate)
        qdr_connection_activate_CT(core, link->conn);
}


static int qdr_link_forward_CT(qdr_core_t *core, qdr_link_t *link, qdr_delivery_t *dlv, qdr_address_t *addr)
{
    int  fanout     = 0;
    bool presettled = dlv->settled;

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
            DEQ_INSERT_TAIL(link->undelivered, dlv);
        } else {
            //
            // TODO - Release the delivery
            //
        }
    } else if (fanout == 1) {
        qd_bitmask_free(dlv->link_exclusion);
        dlv->link_exclusion = 0;
        if (dlv->settled) {
            //
            // The delivery is settled.  Keep it off the unsettled list and issue
            // replacement credit for it now.
            //
            qdr_link_issue_credit_CT(core, link, 1);

            //
            // If the delivery was pre-settled, free it now.
            //
            if (presettled) {
                assert(!dlv->peer);
                qdr_delivery_free(dlv);
            }
        } else
            DEQ_INSERT_TAIL(link->unsettled, dlv);
    } else {
        //
        // The fanout is greater than one.  Do something!  TODO
        //
        qd_bitmask_free(dlv->link_exclusion);
        dlv->link_exclusion = 0;

        if (presettled) {
            qdr_link_issue_credit_CT(core, link, 1);
            assert(!dlv->peer);
            qdr_delivery_free(dlv);
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
        qdr_forward_deliver_CT(core, link->connected_link, peer);
        qd_message_free(dlv->msg);
        dlv->msg = 0;
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
        qdr_link_forward_CT(core, link, dlv, addr);
    } else
        DEQ_INSERT_TAIL(link->undelivered, dlv);
}


static void qdr_send_to_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_field_t  *addr_field = action->args.io.address;
    qd_message_t *msg        = action->args.io.message;

    if (!discard) {
        qdr_address_t *addr;

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
    qdr_delivery_t *dlv     = action->args.delivery.delivery;
    qdr_delivery_t *peer    = dlv->peer;
    bool            push    = false;
    uint64_t        disp    = action->args.delivery.disposition;
    bool            settled = action->args.delivery.settled;

    bool link_routed = dlv && dlv->link && dlv->link->connected_link;

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
            push = true;
            peer->peer = 0;
            dlv->peer  = 0;
            if (peer->link) {
                sys_mutex_lock(peer->link->conn->work_lock);
                DEQ_REMOVE(peer->link->unsettled, peer);
                sys_mutex_unlock(peer->link->conn->work_lock);
                if (peer->link->link_direction == QD_INCOMING && !link_routed)
                    qdr_link_issue_credit_CT(core, peer->link, 1);
            }
        }

        if (dlv->link) {
            sys_mutex_lock(dlv->link->conn->work_lock);
            DEQ_REMOVE(dlv->link->unsettled, dlv);
            sys_mutex_unlock(dlv->link->conn->work_lock);
            if (dlv->link->link_direction == QD_INCOMING && !link_routed)
                qdr_link_issue_credit_CT(core, dlv->link, 1);
        }

        qdr_delivery_free(dlv);
    }

    if (push)
        qdr_delivery_push_CT(core, peer);
}


/**
 * Check the link's accumulated credit.  If the credit given to the connection thread
 * has been issued to Proton, provide the next batch of credit to the connection thread.
 */
void qdr_link_issue_credit_CT(qdr_core_t *core, qdr_link_t *link, int credit)
{
    link->incremental_credit_CT += credit;
    link->flow_started = true;

    if (link->incremental_credit_CT && link->incremental_credit == 0) {
        //
        // Move the credit from the core-thread value to the connection-thread value.
        //
        link->incremental_credit    = link->incremental_credit_CT;
        link->incremental_credit_CT = 0;

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
                qdr_link_issue_credit_CT(core, link, link->capacity);

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

    sys_mutex_lock(link->conn->work_lock);
    qdr_add_delivery_ref(&link->updated_deliveries, dlv);

    //
    // Put this link on the connection's list of links with delivery activity.
    //
    qdr_add_link_ref(&link->conn->links_with_deliveries, link, QDR_LINK_LIST_CLASS_DELIVERY);
    sys_mutex_unlock(link->conn->work_lock);

    //
    // Activate the connection
    //
    qdr_connection_activate_CT(core, link->conn);
}
