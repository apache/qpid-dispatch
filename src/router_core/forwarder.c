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

#include "forwarder.h"

#include "delivery.h"
#include "router_core_private.h"

#include <inttypes.h>
#include <strings.h>

typedef struct qdr_forward_deliver_info_t {
    DEQ_LINKS(struct qdr_forward_deliver_info_t);
    qdr_link_t     *out_link;
    qdr_delivery_t *out_dlv;
} qdr_forward_deliver_info_t;

ALLOC_DECLARE(qdr_forward_deliver_info_t);
DEQ_DECLARE(qdr_forward_deliver_info_t, qdr_forward_deliver_info_list_t);

ALLOC_DEFINE(qdr_forward_deliver_info_t);


// get the control link for a given inter-router connection
static inline qdr_link_t *peer_router_control_link(qdr_core_t *core, int conn_mask)
{
    return (conn_mask >= 0) ? core->control_links_by_mask_bit[conn_mask] : 0;
}


// find the proper outgoing data link on a connection using the given priority
static inline qdr_link_t *peer_router_data_link(qdr_core_t *core,
                                                int         conn_mask,
                                                int         priority)
{
    if (conn_mask < 0 || priority < 0)
        return 0;

    // Try to return the requested priority link, but if it does
    // not exist, return the closest one that is lower.
    qdr_link_t * link = 0;
    while (1) {
        if ((link = core->data_links_by_mask_bit[conn_mask].links[priority]))
            return link;
        if (-- priority < 0)
            return 0;
    }
    return link;
}


// Get an idle anonymous link for a streaming message. This link will come from
// either the connections free link pool or it will be dynamically created on
// the given connection.
static inline qdr_link_t *get_outgoing_streaming_link(qdr_core_t *core, qdr_connection_t *conn)
{
    if (!conn) return 0;

    qdr_link_t *out_link = DEQ_HEAD(conn->streaming_link_pool);
    if (out_link) {
        DEQ_REMOVE_HEAD_N(STREAMING_POOL, conn->streaming_link_pool);
        out_link->in_streaming_pool = false;
    } else {
        // no free links - create a new one
        out_link = qdr_connection_new_streaming_link_CT(core, conn);
        if (!out_link) {
            qd_log(core->log, QD_LOG_WARNING, "[C%"PRIu64"] Unable to setup new outgoing streaming message link", conn->identity);
            return 0;
        }
    }
    return out_link;
}


//==================================================================================
// Built-in Forwarders
//==================================================================================

static int qdr_forward_message_null_CT(qdr_core_t      *core,
                                       qdr_address_t   *addr,
                                       qd_message_t    *msg,
                                       qdr_delivery_t  *in_delivery,
                                       bool             exclude_inprocess,
                                       bool             control)
{
    qd_log(core->log, QD_LOG_CRITICAL, "NULL Message Forwarder Invoked");
    return 0;
}


static bool qdr_forward_attach_null_CT(qdr_core_t     *core,
                                       qdr_address_t  *addr,
                                       qdr_link_t     *link,
                                       qdr_terminus_t *source,
                                       qdr_terminus_t *target)
{
    qd_log(core->log, QD_LOG_CRITICAL, "NULL Attach Forwarder Invoked");
    return false;
}


static void qdr_forward_find_closest_remotes_CT(qdr_core_t *core, qdr_address_t *addr)
{
    qdr_node_t *rnode       = DEQ_HEAD(core->routers);
    int         lowest_cost = 0;

    if (!addr->closest_remotes)
        addr->closest_remotes = qd_bitmask(0);
    addr->cost_epoch  = core->cost_epoch;
    addr->next_remote = -1;

    qd_bitmask_clear_all(addr->closest_remotes);
    while (rnode) {
        if (qd_bitmask_value(addr->rnodes, rnode->mask_bit)) {
            if (lowest_cost == 0) {
                lowest_cost = rnode->cost;
                addr->next_remote = rnode->mask_bit;
            }
            if (lowest_cost == rnode->cost)
                qd_bitmask_set_bit(addr->closest_remotes, rnode->mask_bit);
            else
                break;
        }
        rnode = DEQ_NEXT(rnode);
    }
}


qdr_delivery_t *qdr_forward_new_delivery_CT(qdr_core_t *core, qdr_delivery_t *in_dlv, qdr_link_t *out_link, qd_message_t *msg)
{
    qdr_delivery_t *out_dlv = new_qdr_delivery_t();
    if (out_link->conn)
        out_link->conn->last_delivery_time = qdr_core_uptime_ticks(core);

    ZERO(out_dlv);
    set_safe_ptr_qdr_link_t(out_link, &out_dlv->link_sp);
    out_dlv->msg        = qd_message_copy(msg);
    out_dlv->delivery_id = next_delivery_id();
    out_dlv->link_id     = out_link->identity;
    out_dlv->conn_id     = out_link->conn_id;
    out_dlv->dispo_lock  = sys_mutex();
    qd_log(core->log, QD_LOG_DEBUG, DLV_FMT" Delivery created qdr_forward_new_delivery_CT", DLV_ARGS(out_dlv));

    if (in_dlv) {
        out_dlv->settled       = in_dlv->settled;
        out_dlv->ingress_time  = in_dlv->ingress_time;
        out_dlv->ingress_index = in_dlv->ingress_index;
        if (in_dlv->remote_disposition) {
            // propagate disposition state from remote to peer
            qdr_delivery_move_delivery_state_CT(in_dlv, out_dlv);
        }
    } else {
        out_dlv->settled       = true;
        out_dlv->ingress_time  = qdr_core_uptime_ticks(core);
        out_dlv->ingress_index = -1;
    }

    out_dlv->presettled = out_dlv->settled;

    uint64_t tag = core->next_tag++;
    memcpy(out_dlv->tag, &tag, sizeof(tag));
    out_dlv->tag_length = sizeof(tag);

    //
    // Add one to the message fanout. This will later be used in the qd_message_send function that sends out messages.
    //
    qd_message_add_fanout(out_dlv->msg);

    //
    // Create peer linkage if the outgoing delivery is unsettled. This peer linkage is necessary to deal with dispositions that show up in the future.
    // Also create peer linkage if the message is not yet been completely received. This linkage will help us stream large pre-settled multicast messages.
    //
    if (!out_dlv->settled || !qd_message_receive_complete(msg))
        qdr_delivery_link_peers_CT(in_dlv, out_dlv);

    return out_dlv;
}


//
// Drop all pre-settled deliveries pending on the link's
// undelivered list.
//
static void qdr_forward_drop_presettled_CT_LH(qdr_core_t *core, qdr_link_t *link)
{
    qdr_delivery_t *dlv = DEQ_HEAD(link->undelivered);

    if (!dlv)
        return;
    //
    // Remove leading delivery from consideration.
    // Parts of this message may have been transmitted already and dropping
    // it may corrupt outbound data.
    //
    dlv = DEQ_NEXT(dlv);

    qdr_delivery_t *next;

    while (dlv) {
        next = DEQ_NEXT(dlv);
        //
        // Remove pre-settled deliveries unless they are in a link_work
        // record that is being processed.  If it's being processed, it is
        // too late to drop the delivery.
        //
        if (dlv->settled && dlv->link_work && !dlv->link_work->processing) {
            DEQ_REMOVE(link->undelivered, dlv);
            dlv->where = QDR_DELIVERY_NOWHERE;

            //
            // The link-work item representing this pending delivery must be
            // updated to reflect the removal of the delivery.  If the item
            // has no other deliveries associated with it, it can be removed
            // from the work list.
            //
            if (--dlv->link_work->value == 0) {
                DEQ_REMOVE(link->work_list, dlv->link_work);
                qdr_link_work_release(dlv->link_work); // for work_list
            }
            qdr_link_work_release(dlv->link_work); // for dlv ref
            dlv->link_work = 0;
            dlv->disposition = PN_RELEASED;
            qdr_delivery_decref_CT(core, dlv, "qdr_forward_drop_presettled_CT_LH - remove from link-work list");

            // Increment the presettled_dropped_deliveries on the out_link
            link->dropped_presettled_deliveries++;
            core->dropped_presettled_deliveries++;
        }
        dlv = next;
    }
}


void qdr_forward_deliver_CT(qdr_core_t *core, qdr_link_t *out_link, qdr_delivery_t *out_dlv)
{
    sys_mutex_lock(out_link->conn->work_lock);

    //
    // If the delivery is pre-settled and the outbound link is at or above capacity,
    // discard all pre-settled deliveries on the undelivered list prior to enqueuing
    // the new delivery.
    //
    if (out_dlv->settled && out_link->capacity > 0 && DEQ_SIZE(out_link->undelivered) >= out_link->capacity)
        qdr_forward_drop_presettled_CT_LH(core, out_link);

    DEQ_INSERT_TAIL(out_link->undelivered, out_dlv);
    out_dlv->where = QDR_DELIVERY_IN_UNDELIVERED;

    // This incref is for putting the delivery in the undelivered list
    qdr_delivery_incref(out_dlv, "qdr_forward_deliver_CT - add to undelivered list");

    //
    // We must put a work item on the link's work list to represent this pending delivery.
    // If there's already a delivery item on the tail of the work list, simply join that item
    // by incrementing the value.
    //
    qdr_link_work_t *work = DEQ_TAIL(out_link->work_list);
    if (work && work->work_type == QDR_LINK_WORK_DELIVERY) {
        work->value++;
    } else {
        work            = qdr_link_work(QDR_LINK_WORK_DELIVERY);
        work->value     = 1;
        DEQ_INSERT_TAIL(out_link->work_list, work);
    }
    qdr_add_link_ref(&out_link->conn->links_with_work[out_link->priority], out_link, QDR_LINK_LIST_CLASS_WORK);

    out_dlv->link_work = qdr_link_work_getref(work);
    sys_mutex_unlock(out_link->conn->work_lock);

    //
    // We are dealing here only with link routed deliveries
    // If the out_link has a connected link and if the out_link is an inter-router link, increment the global deliveries_transit
    // If the out_link is a route container link, add to the global deliveries_egress
    //
    if (out_link->connected_link) {
        if (out_link->conn->role == QDR_ROLE_INTER_ROUTER) {
            core->deliveries_transit++;
        }
        else {
            core->deliveries_egress++;
        }
    }

    //
    // Activate the outgoing connection for later processing.
    //
    qdr_connection_activate_CT(core, out_link->conn);
}


static void qdr_settle_subscription_delivery_CT(qdr_core_t *core, qdr_action_t *action, bool discard)
{
    qdr_delivery_t *in_delivery = action->args.delivery.delivery;

    if (!in_delivery)
        return;

    if (!discard) {
        in_delivery->disposition = action->args.delivery.disposition;
        in_delivery->settled     = true;

        bool moved = qdr_delivery_settled_CT(core, in_delivery);
        if (moved) {
            // expect: in_delivery has at least 2 refcounts - one from being on
            // the unsettled list and another from the action
            assert(sys_atomic_get(&in_delivery->ref_count) > 1);
            qdr_delivery_decref_CT(core, in_delivery, "qdr_settle_subscription_delivery_CT - removed from unsettled");
            qdr_delivery_push_CT(core, in_delivery);
        }
    }

    qdr_delivery_decref_CT(core, in_delivery, "qdr_settle_subscription_delivery_CT - removed from action");
}


void qdr_forward_on_message(qdr_core_t *core, qdr_general_work_t *work)
{
    qdr_error_t *error = 0;
    uint64_t disposition = work->on_message(work->on_message_context, work->msg, work->maskbit,
                                            work->inter_router_cost, work->in_conn_id, work->policy_spec, &error);
    qd_message_free(work->msg);

    if (!work->delivery) {
        qdr_error_free(error);
        return;
    }

    if (!work->delivery->multicast) {
        qdr_action_t*action = qdr_action(qdr_settle_subscription_delivery_CT, "settle_subscription_delivery");
        action->args.delivery.delivery    = work->delivery;
        action->args.delivery.disposition = disposition;
        if (error) {
            // setting the local state will cause proton to send this
            // error to the remote
            qd_delivery_state_free(work->delivery->local_state);
            work->delivery->local_state = qd_delivery_state_from_error(error);
        }

        qdr_action_enqueue(core, action);
        // Transfer the delivery reference from work protection to action protection
    } else {
        qdr_error_free(error);
        qdr_delivery_decref(core, work->delivery, "qdr_forward_on_message - remove from general work");
    }
}


void qdr_forward_on_message_CT(qdr_core_t *core, qdr_subscription_t *sub, qdr_link_t *link, qd_message_t *msg, qdr_delivery_t *in_delivery)
{
    int          mask_bit = link ? link->conn->mask_bit : 0;
    int          cost     = link ? link->conn->inter_router_cost : 1;
    uint64_t     identity = link ? link->conn->identity : 0;
    qdr_error_t *error    = 0;

    if (sub->in_core) {
        //
        // The handler runs in-core.  Invoke it right now.
        //
        uint64_t disposition = sub->on_message(sub->on_message_context, msg, mask_bit, cost,
                                               identity, link ? link->conn->policy_spec : 0, &error);
        if (!!in_delivery) {
            in_delivery->disposition = disposition;
            in_delivery->settled     = true;
            if (error) {
                // setting the local state will cause proton to send this
                // error to the remote
                qd_delivery_state_free(in_delivery->local_state);
                in_delivery->local_state = qd_delivery_state_from_error(error);
            }
            qdr_delivery_push_CT(core, in_delivery);
        } else {
            qdr_error_free(error);
        }
    } else {
        //
        // The handler runs in an IO thread.  Defer its invocation.
        //
        if (!!in_delivery)
            qdr_delivery_incref(in_delivery, "qdr_forward_on_message_CT - adding to general work item");

        qdr_general_work_t *work = qdr_general_work(qdr_forward_on_message);
        work->on_message         = sub->on_message;
        work->on_message_context = sub->on_message_context;
        work->msg                = qd_message_copy(msg);
        work->maskbit            = mask_bit;
        work->inter_router_cost  = cost;
        work->in_conn_id         = identity;
        work->policy_spec        = link ? link->conn->policy_spec : 0;
        work->delivery           = in_delivery;
        qdr_post_general_work_CT(core, work);
    }
}


/**
 * Get the effective priority for a message.
 *
 * This function returns a priority value for a message (and address).  If the message
 * has no priority header, the default priority is chosen.
 *
 * If the address has a defined priority, that value takes precedence.
 * Otherwise the message priority is used, which has a default value
 * if none was explicitly set.
 */
static uint8_t qdr_forward_effective_priority(qd_message_t *msg, qdr_address_t *addr)
{
    return addr->priority >= 0 ? addr->priority : qd_message_get_priority(msg);
}


/**
 * Determine if forwarding a delivery onto a link will result in edge-echo.
 */
static inline bool qdr_forward_edge_echo_CT(qdr_delivery_t *in_dlv, qdr_link_t *out_link)
{
    qdr_link_t *link = in_dlv ? safe_deref_qdr_link_t(in_dlv->link_sp) : 0;
    return (in_dlv && in_dlv->via_edge && link && link->conn == out_link->conn);
}


/**
 * Handle forwarding to a subscription
 */
static void qdr_forward_to_subscriber_CT(qdr_core_t *core, qdr_subscription_t *sub, qdr_delivery_t *in_dlv, qd_message_t *in_msg, bool receive_complete)
{
    qd_message_add_fanout(in_msg);

    //
    // Only if the message has been completely received, forward it to the subscription.
    // Subscriptions don't have the ability to deal with partial messages.
    //
    if (receive_complete) {
        qdr_link_t *link = in_dlv ? safe_deref_qdr_link_t(in_dlv->link_sp) : 0;
        qdr_forward_on_message_CT(core, sub, link, in_msg, in_dlv);
    } else {
        //
        // Receive is not complete, we will store the sub in
        // in_dlv->subscriptions so we can send the message to the subscription
        // after the message fully arrives
        //
        assert(in_dlv);
        qdr_add_subscription_ref_CT(&in_dlv->subscriptions, sub);
        qd_message_Q2_holdoff_disable(in_msg);
    }
}


int qdr_forward_multicast_CT(qdr_core_t      *core,
                             qdr_address_t   *addr,
                             qd_message_t    *msg,
                             qdr_delivery_t  *in_delivery,
                             bool             exclude_inprocess,
                             bool             control)
{
    bool          bypass_valid_origins = addr->forwarder->bypass_valid_origins;
    int           fanout               = 0;
    qd_bitmask_t *link_exclusion       = !!in_delivery ? in_delivery->link_exclusion : 0;
    bool          receive_complete     = qd_message_receive_complete(msg);

    qdr_forward_deliver_info_list_t deliver_info_list;
    DEQ_INIT(deliver_info_list);

    //
    // Forward to local subscribers
    //
    if (!addr->local || exclude_inprocess) {
        qdr_link_ref_t *link_ref = DEQ_HEAD(addr->rlinks);
        while (link_ref) {
            qdr_link_t *out_link = link_ref->link;

            //
            // Only forward via links that don't result in edge-echo.
            //
            if (!qdr_forward_edge_echo_CT(in_delivery, out_link)) {

                if (!receive_complete && out_link->conn->connection_info->streaming_links) {
                    out_link = get_outgoing_streaming_link(core, out_link->conn);
                }

                if (out_link) {
                    qdr_delivery_t *out_delivery = qdr_forward_new_delivery_CT(core, in_delivery, out_link, msg);

                    // Store the out_link and out_delivery so we can forward the delivery later on
                    qdr_forward_deliver_info_t *deliver_info = new_qdr_forward_deliver_info_t();
                    ZERO(deliver_info);
                    deliver_info->out_dlv = out_delivery;
                    deliver_info->out_link = out_link;
                    DEQ_INSERT_TAIL(deliver_info_list, deliver_info);

                    fanout++;
                    if (out_link->link_type != QD_LINK_CONTROL && out_link->link_type != QD_LINK_ROUTER && !out_link->fallback) {
                        addr->deliveries_egress++;
                        core->deliveries_egress++;
                    }
                }
            }

            link_ref = DEQ_NEXT(link_ref);
        }
    }

    //
    // Forward to remote routers with subscribers using the appropriate
    // link for the traffic class: control or data
    //

    //
    // Get the mask bit associated with the ingress router for the message.
    // This will be compared against the "valid_origin" masks for each
    // candidate destination router.
    //
    int origin = -1;
    qd_iterator_t *ingress_iter = in_delivery ? in_delivery->origin : 0;

    if (ingress_iter && !bypass_valid_origins) {
        qd_iterator_reset_view(ingress_iter, ITER_VIEW_NODE_HASH);
        qdr_address_t *origin_addr;
        qd_hash_retrieve(core->addr_hash, ingress_iter, (void*) &origin_addr);
        if (origin_addr && qd_bitmask_cardinality(origin_addr->rnodes) == 1)
            qd_bitmask_first_set(origin_addr->rnodes, &origin);
    } else
        origin = 0;

    //
    // Forward to the next-hops for remote destinations.
    //
    if (origin >= 0) {
        int           dest_bit;
        qd_bitmask_t *conn_set = qd_bitmask(0);  // connections to the next-hops

        //
        // Loop over the target nodes for this address.  Build a set of outgoing connections
        // for which there are valid targets.  We do this to avoid sending more than one
        // message to a given next-hop.  It's possible that there are multiple destinations
        // for this address that are all reachable via the same next-hop.  In this case, we
        // will send only one copy of the message to the next-hop and allow the downstream
        // routers to fan the message out.
        //
        int c;
        for (QD_BITMASK_EACH(addr->rnodes, dest_bit, c)) {
            qdr_node_t *rnode = core->routers_by_mask_bit[dest_bit];
            if (!rnode)
                continue;

            // get the inter-router connection associated with path to rnode:
            int conn_bit = (rnode->next_hop) ? rnode->next_hop->conn_mask_bit : rnode->conn_mask_bit;
            if (conn_bit >= 0 && (!link_exclusion || qd_bitmask_value(link_exclusion, conn_bit) == 0)) {
                qd_bitmask_set_bit(conn_set, conn_bit);
            }
        }

        //
        // Send a copy of the message over the inter-router connection to the next hop
        //
        int conn_bit;
        while (qd_bitmask_first_set(conn_set, &conn_bit)) {
            qd_bitmask_clear_bit(conn_set, conn_bit);

            qdr_link_t  *dest_link;
            if (control) {
                dest_link = peer_router_control_link(core, conn_bit);
            } else if (!receive_complete) {  // inter-router conns support dynamic streaming links
                dest_link = get_outgoing_streaming_link(core, core->rnode_conns_by_mask_bit[conn_bit]);
            } else {
                dest_link = peer_router_data_link(core, conn_bit, qdr_forward_effective_priority(msg, addr));
            }

            if (dest_link) {
                qdr_delivery_t *out_delivery = qdr_forward_new_delivery_CT(core, in_delivery, dest_link, msg);

                // Store the out_link and out_delivery so we can forward the delivery later on
                qdr_forward_deliver_info_t *deliver_info = new_qdr_forward_deliver_info_t();
                ZERO(deliver_info);
                deliver_info->out_dlv = out_delivery;
                deliver_info->out_link = dest_link;
                DEQ_INSERT_TAIL(deliver_info_list, deliver_info);

                fanout++;
                addr->deliveries_transit++;
                if (dest_link->link_type == QD_LINK_ROUTER)
                    core->deliveries_transit++;
            }
        }

        qd_bitmask_free(conn_set);
    }

    if (!exclude_inprocess) {
        //
        // Forward to in-process subscribers
        //
        qdr_subscription_t *sub = DEQ_HEAD(addr->subscriptions);
        while (sub) {
            qdr_forward_to_subscriber_CT(core, sub, in_delivery, msg, receive_complete);
            fanout++;
            addr->deliveries_to_container++;
            sub = DEQ_NEXT(sub);
        }
    }

    qdr_forward_deliver_info_t *deliver_info = DEQ_HEAD(deliver_info_list);
    while (deliver_info) {
        qdr_forward_deliver_CT(core, deliver_info->out_link, deliver_info->out_dlv);
        DEQ_REMOVE_HEAD(deliver_info_list);
        free_qdr_forward_deliver_info_t(deliver_info);
        deliver_info = DEQ_HEAD(deliver_info_list);
    }

    return fanout;
}


int qdr_forward_closest_CT(qdr_core_t      *core,
                           qdr_address_t   *addr,
                           qd_message_t    *msg,
                           qdr_delivery_t  *in_delivery,
                           bool             exclude_inprocess,
                           bool             control)
{
    const bool receive_complete = qd_message_receive_complete(msg);
    //
    // Forward to an in-process subscriber if there is one.
    //
    if (!exclude_inprocess) {
        qdr_subscription_t *sub = DEQ_HEAD(addr->subscriptions);
        if (sub) {
            qdr_forward_to_subscriber_CT(core, sub, in_delivery, msg, receive_complete);

            //
            // Rotate this subscription to the end of the list to get round-robin distribution.
            //
            if (DEQ_SIZE(addr->subscriptions) > 1) {
                DEQ_REMOVE_HEAD(addr->subscriptions);
                DEQ_INSERT_TAIL(addr->subscriptions, sub);
            }

            addr->deliveries_to_container++;
            return 1;
        }
    }

    //
    // Forward to a locally attached subscriber.
    //
    qdr_link_ref_t *link_ref = DEQ_HEAD(addr->rlinks);

    //
    // If this link results in edge-echo, skip to the next link in the list.
    //
    while (link_ref && qdr_forward_edge_echo_CT(in_delivery, link_ref->link))
        link_ref = DEQ_NEXT(link_ref);

    if (link_ref) {
        qdr_link_t *out_link = link_ref->link;

        if (!receive_complete && out_link->conn->connection_info->streaming_links) {
            out_link = get_outgoing_streaming_link(core, out_link->conn);
        }

        if (out_link) {
            qdr_delivery_t *out_delivery = qdr_forward_new_delivery_CT(core, in_delivery, out_link, msg);
            qdr_forward_deliver_CT(core, out_link, out_delivery);

            //
            // If there are multiple local subscribers, rotate the list of link references
            // so deliveries will be distributed among the subscribers in a round-robin pattern.
            //
            if (DEQ_SIZE(addr->rlinks) > 1) {
                link_ref = DEQ_HEAD(addr->rlinks);
                DEQ_REMOVE_HEAD(addr->rlinks);
                DEQ_INSERT_TAIL(addr->rlinks, link_ref);
            }

            if (!out_link->fallback) {
                addr->deliveries_egress++;
                core->deliveries_egress++;
            }

            if (qdr_connection_route_container(out_link->conn)) {
                core->deliveries_egress_route_container++;
                addr->deliveries_egress_route_container++;
            }

            return 1;
        }
    }

    //
    // If the cached list of closest remotes is stale (i.e. cost data has changed),
    // recompute the closest remote routers.
    //
    if (addr->cost_epoch != core->cost_epoch)
        qdr_forward_find_closest_remotes_CT(core, addr);

    //
    // Forward to remote routers with subscribers using the appropriate
    // link for the traffic class: control or data
    //
    if (addr->next_remote >= 0) {
        qdr_node_t *rnode = core->routers_by_mask_bit[addr->next_remote];
        if (rnode) {
            _qdbm_next(addr->closest_remotes, &addr->next_remote);
            if (addr->next_remote == -1)
                qd_bitmask_first_set(addr->closest_remotes, &addr->next_remote);

            // get the inter-router connection associated with path to rnode:
            const int conn_bit = (rnode->next_hop) ? rnode->next_hop->conn_mask_bit : rnode->conn_mask_bit;
            if (conn_bit >= 0) {
                qdr_link_t *out_link;
                if (control) {
                    out_link = peer_router_control_link(core, conn_bit);
                } else if (!receive_complete) {
                    out_link = get_outgoing_streaming_link(core, core->rnode_conns_by_mask_bit[conn_bit]);
                } else {
                    out_link = peer_router_data_link(core, conn_bit, qdr_forward_effective_priority(msg, addr));
                }

                if (out_link) {
                    qdr_delivery_t *out_delivery = qdr_forward_new_delivery_CT(core, in_delivery, out_link, msg);
                    qdr_forward_deliver_CT(core, out_link, out_delivery);
                    addr->deliveries_transit++;
                    if (out_link->link_type == QD_LINK_ROUTER)
                        core->deliveries_transit++;
                    return 1;
                }
            }
        }
    }

    return 0;
}


int qdr_forward_balanced_CT(qdr_core_t      *core,
                            qdr_address_t   *addr,
                            qd_message_t    *msg,
                            qdr_delivery_t  *in_delivery,
                            bool             exclude_inprocess,
                            bool             control)
{
    //
    // Control messages should never use balanced treatment.
    //
    assert(!control);

    //
    // If this is the first time through here, allocate the array for outstanding delivery counts.
    //
    if (addr->outstanding_deliveries == 0) {
        addr->outstanding_deliveries = NEW_ARRAY(int, qd_bitmask_width());
        for (int i = 0; i < qd_bitmask_width(); i++)
            addr->outstanding_deliveries[i] = 0;
    }

    qdr_link_t *best_eligible_link       = 0;
    int         best_eligible_conn_bit   = -1;
    uint32_t    eligible_link_value      = UINT32_MAX;
    qdr_link_t *best_ineligible_link     = 0;
    int         best_ineligible_conn_bit = -1;
    uint32_t    ineligible_link_value    = UINT32_MAX;

    //
    // Find all the possible outbound links for this delivery, searching for the one with the
    // smallest eligible value.  Value = outstanding_deliveries + minimum_downrange_cost.
    // A link is ineligible if the outstanding_deliveries is equal to or greater than the
    // link's capacity.
    //
    // If there are no eligible links, use the best ineligible link.  Zero fanout should be returned
    // only if there are no available destinations.
    //

    //
    // Start with the local links
    //
    qdr_link_ref_t *link_ref = DEQ_HEAD(addr->rlinks);
    while (link_ref && eligible_link_value != 0) {
        qdr_link_t *link     = link_ref->link;
        sys_mutex_lock(link->conn->work_lock);
        uint32_t    value    = DEQ_SIZE(link->undelivered) + DEQ_SIZE(link->unsettled);
        sys_mutex_unlock(link->conn->work_lock);
        bool        eligible = link->capacity > value;

        //
        // Only consider links that do not result in edge-echo.
        //
        if (!qdr_forward_edge_echo_CT(in_delivery, link)) {
            //
            // If this is the best eligible link so far, record the fact.
            // Otherwise, if this is the best ineligible link, make note of that.
            //
            if (eligible && eligible_link_value > value) {
                best_eligible_link  = link;
                eligible_link_value = value;
            } else if (!eligible && ineligible_link_value > value) {
                best_ineligible_link  = link;
                ineligible_link_value = value;
            }
        }

        link_ref = DEQ_NEXT(link_ref);
    }

    //
    // If we haven't already found a link with zero (best possible) value, check the
    // inter-router links as well.
    //
    if (!best_eligible_link || eligible_link_value > 0) {
        //
        // Get the mask bit associated with the ingress router for the message.
        // This will be compared against the "valid_origin" masks for each
        // candidate destination router.
        //
        int origin = 0;  // default to this router
        qd_iterator_t *ingress_iter = in_delivery ? in_delivery->origin : 0;

        if (ingress_iter) {
            qd_iterator_reset_view(ingress_iter, ITER_VIEW_NODE_HASH);
            qdr_address_t *origin_addr;
            qd_hash_retrieve(core->addr_hash, ingress_iter, (void*) &origin_addr);
            if (origin_addr && qd_bitmask_cardinality(origin_addr->rnodes) == 1)
                qd_bitmask_first_set(origin_addr->rnodes, &origin);
        }

        int c;
        int node_bit;
        for (QD_BITMASK_EACH(addr->rnodes, node_bit, c)) {
            qdr_node_t *rnode     = core->routers_by_mask_bit[node_bit];

            if (qd_bitmask_value(rnode->valid_origins, origin)) {

                qdr_node_t *next_node = rnode->next_hop ? rnode->next_hop : rnode;
                int         conn_bit  = next_node->conn_mask_bit;
                uint8_t     priority  = qdr_forward_effective_priority(msg, addr);
                qdr_link_t *link      = peer_router_data_link(core, conn_bit, priority);
                if (!link) continue;

                int         value     = addr->outstanding_deliveries[conn_bit];
                bool        eligible  = link->capacity > value;

                //
                // Link is a candidate, adjust the value by the bias (node cost).
                //
                value += rnode->cost;
                if (eligible && eligible_link_value > value) {
                    best_eligible_link     = link;
                    best_eligible_conn_bit = conn_bit;
                    eligible_link_value    = value;
                } else if (!eligible && ineligible_link_value > value) {
                    best_ineligible_link     = link;
                    best_ineligible_conn_bit = conn_bit;
                    ineligible_link_value    = value;
                }
            }
        }
    } else if (best_eligible_link) {
        //
        // Rotate the rlinks list to enhance the appearance of balance when there is
        // little load (see DISPATCH-367)
        //
        if (DEQ_SIZE(addr->rlinks) > 1) {
            link_ref = DEQ_HEAD(addr->rlinks);
            DEQ_REMOVE_HEAD(addr->rlinks);
            DEQ_INSERT_TAIL(addr->rlinks, link_ref);
        }
    }

    qdr_link_t *chosen_link     = 0;
    int         chosen_conn_bit = -1;

    if (best_eligible_link) {
        chosen_link     = best_eligible_link;
        chosen_conn_bit = best_eligible_conn_bit;
    } else if (best_ineligible_link) {
        chosen_link     = best_ineligible_link;
        chosen_conn_bit = best_ineligible_conn_bit;
    }

    if (chosen_link) {

        // DISPATCH-1545 (head of line blocking): if the message is streaming,
        // see if the allows us to open a dedicated link for streaming
        if (!qd_message_receive_complete(msg) && chosen_link->conn->connection_info->streaming_links) {
            chosen_link = get_outgoing_streaming_link(core, chosen_link->conn);
            if (!chosen_link)
                return 0;
        }

        qdr_delivery_t *out_delivery = qdr_forward_new_delivery_CT(core, in_delivery, chosen_link, msg);
        qdr_forward_deliver_CT(core, chosen_link, out_delivery);

        //
        // Bump the appropriate counter based on where we sent the delivery.
        //
        if (chosen_conn_bit >= 0) {  // sent to peer router
            //
            // If the delivery is unsettled account for the outstanding delivery sent inter-router.
            //
            if (in_delivery && !in_delivery->settled) {
                addr->outstanding_deliveries[chosen_conn_bit]++;
                out_delivery->tracking_addr     = addr;
                out_delivery->tracking_addr_bit = chosen_conn_bit;
                addr->tracked_deliveries++;
            }

            addr->deliveries_transit++;
            if (chosen_link->link_type == QD_LINK_ROUTER)
                core->deliveries_transit++;
        }
        else {
            if (!chosen_link->fallback) {
                addr->deliveries_egress++;
                core->deliveries_egress++;
            }

            if (qdr_connection_route_container(chosen_link->conn)) {
                core->deliveries_egress_route_container++;
                addr->deliveries_egress_route_container++;
            }
        }
        return 1;
    }

    return 0;
}

bool qdr_forward_link_balanced_CT(qdr_core_t     *core,
                                  qdr_address_t  *addr,
                                  qdr_link_t     *in_link,
                                  qdr_terminus_t *source,
                                  qdr_terminus_t *target)
{
    qdr_connection_ref_t *conn_ref = DEQ_HEAD(addr->conns);
    qdr_connection_t     *conn     = 0;
    char                 *strip    = 0;
    char                 *insert   = 0;

    //
    // Check for locally connected containers that can handle this link attach.
    //
    if (conn_ref) {
        conn = conn_ref->conn;
        qdr_terminus_t *remote_terminus = in_link->link_direction == QD_OUTGOING ? source : target;
        if (addr->del_prefix) {
            insert = strdup(addr->del_prefix);
            qdr_terminus_strip_address_prefix(remote_terminus, addr->del_prefix);
        }
        if (addr->add_prefix) {
            strip = strdup(addr->add_prefix);
            qdr_terminus_insert_address_prefix(remote_terminus, addr->add_prefix);
        }

        //
        // If there are more than one local connections available for handling this link,
        // rotate the list so the attaches are balanced across the containers.
        //
        if (DEQ_SIZE(addr->conns) > 1) {
            DEQ_REMOVE_HEAD(addr->conns);
            DEQ_INSERT_TAIL(addr->conns, conn_ref);
        }
    } else {
        //
        // Look for a next-hop we can use to forward the link-attach.
        //
        if (addr->cost_epoch != core->cost_epoch) {
            addr->next_remote = -1;
            addr->cost_epoch  = core->cost_epoch;
        }

        if (addr->next_remote < 0) {
            qd_bitmask_first_set(addr->rnodes, &addr->next_remote);
        }

        if (addr->next_remote >= 0) {

            qdr_node_t *rnode = core->routers_by_mask_bit[addr->next_remote];

            if (rnode) {
                //
                // Advance the addr->next_remote so there will be link balance across containers
                //
                _qdbm_next(addr->rnodes, &addr->next_remote);
                if (addr->next_remote == -1)
                    qd_bitmask_first_set(addr->rnodes, &addr->next_remote);

                int conn_bit = (rnode->next_hop) ? rnode->next_hop->conn_mask_bit : rnode->conn_mask_bit;
                conn = (conn_bit >= 0) ? core->rnode_conns_by_mask_bit[conn_bit] : 0;
            }
        }
    }

    if (conn) {
        qdr_forward_link_direct_CT(core, conn, in_link, source, target, strip, insert);
        return true;
    }

    free(insert);
    free(strip);
    return false;
}


void qdr_forward_link_direct_CT(qdr_core_t       *core,
                                qdr_connection_t *conn,
                                qdr_link_t       *in_link,
                                qdr_terminus_t   *source,
                                qdr_terminus_t   *target,
                                char             *strip,
                                char             *insert)
{
    qdr_link_t *out_link = new_qdr_link_t();
    ZERO(out_link);
    out_link->core           = core;
    out_link->identity       = qdr_identifier(core);
    out_link->conn           = conn;
    out_link->conn_id        = conn->identity;
    out_link->link_type      = QD_LINK_ENDPOINT;
    out_link->link_direction = qdr_link_direction(in_link) == QD_OUTGOING ? QD_INCOMING : QD_OUTGOING;
    out_link->admin_enabled  = true;
    out_link->attach_count   = 1;
    out_link->core_ticks     = qdr_core_uptime_ticks(core);
    out_link->zero_credit_time = out_link->core_ticks;
    out_link->strip_annotations_in  = conn->strip_annotations_in;
    out_link->strip_annotations_out = conn->strip_annotations_out;
    out_link->priority       = in_link->priority;

    if (strip) {
        out_link->strip_prefix = strip;
    }
    if (insert) {
        out_link->insert_prefix = insert;
    }

    out_link->oper_status    = QDR_LINK_OPER_DOWN;

    out_link->name = strdup(in_link->disambiguated_name ? in_link->disambiguated_name : in_link->name);

    out_link->connected_link = in_link;
    in_link->connected_link  = out_link;

    DEQ_INSERT_TAIL(core->open_links, out_link);
    qdr_add_link_ref(&conn->links, out_link, QDR_LINK_LIST_CLASS_CONNECTION);

    qdr_connection_work_t *work = new_qdr_connection_work_t();
    ZERO(work);
    work->work_type = QDR_CONNECTION_WORK_FIRST_ATTACH;
    work->link      = out_link;
    work->source    = source;
    work->target    = target;
    work->ssn_class = QD_SSN_LINK_ROUTE;

    qdr_connection_enqueue_work_CT(core, conn, work);

    if (qdr_link_direction(in_link) == QD_OUTGOING && in_link->credit_to_core > 0) {
        qdr_link_issue_credit_CT(core, out_link, in_link->credit_stored, in_link->drain_mode);
        in_link->credit_stored = 0;
    }
}


//==================================================================================
// In-Thread API Functions
//==================================================================================

qdr_forwarder_t *qdr_new_forwarder(qdr_forward_message_t fm, qdr_forward_attach_t fa, bool bypass_valid_origins)
{
    qdr_forwarder_t *forw = NEW(qdr_forwarder_t);

    forw->forward_message      = fm ? fm : qdr_forward_message_null_CT;
    forw->forward_attach       = fa ? fa : qdr_forward_attach_null_CT;
    forw->bypass_valid_origins = bypass_valid_origins;

    return forw;
}


void qdr_forwarder_setup_CT(qdr_core_t *core)
{
    //
    // Create message forwarders
    //
    core->forwarders[QD_TREATMENT_MULTICAST_FLOOD]  = qdr_new_forwarder(qdr_forward_multicast_CT, 0, true);
    core->forwarders[QD_TREATMENT_MULTICAST_ONCE]   = qdr_new_forwarder(qdr_forward_multicast_CT, 0, false);
    core->forwarders[QD_TREATMENT_ANYCAST_CLOSEST]  = qdr_new_forwarder(qdr_forward_closest_CT,   0, false);
    core->forwarders[QD_TREATMENT_ANYCAST_BALANCED] = qdr_new_forwarder(qdr_forward_balanced_CT,  0, false);

    //
    // Create link forwarders
    //
    core->forwarders[QD_TREATMENT_LINK_BALANCED] = qdr_new_forwarder(0, qdr_forward_link_balanced_CT, false);
}


qdr_forwarder_t *qdr_forwarder_CT(qdr_core_t *core, qd_address_treatment_t treatment)
{
    if (treatment <= QD_TREATMENT_LINK_BALANCED)
        return core->forwarders[treatment];
    return 0;
}


int qdr_forward_message_CT(qdr_core_t *core, qdr_address_t *addr, qd_message_t *msg, qdr_delivery_t *in_delivery,
                           bool exclude_inprocess, bool control)
{
    int fanout = 0;
    if (addr->forwarder)
        fanout = addr->forwarder->forward_message(core, addr, msg, in_delivery, exclude_inprocess, control);
    return fanout;
}


bool qdr_forward_attach_CT(qdr_core_t *core, qdr_address_t *addr, qdr_link_t *in_link,
                           qdr_terminus_t *source, qdr_terminus_t *target)
{
    if (addr->forwarder)
        return addr->forwarder->forward_attach(core, addr, in_link, source, target);
    return false;
}

