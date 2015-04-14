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

#include "dispatch_private.h"

/** defines a default set of forwarding behaviors based on the semantics of an
 * address.
 */


static void forward_to_direct_subscribers_LH(qd_address_t *addr,
                                             qd_router_delivery_t *delivery,
                                             qd_message_t *msg,
                                             int *fanout)
{
    qd_router_link_ref_t *dest_link_ref = DEQ_HEAD(addr->rlinks);
    while (dest_link_ref) {
        qd_routed_event_t *re = new_qd_routed_event_t();
        DEQ_ITEM_INIT(re);
        re->delivery    = 0;
        re->message     = qd_message_copy(msg);
        re->settle      = 0;
        re->disposition = 0;
        DEQ_INSERT_TAIL(dest_link_ref->link->msg_fifo, re);

        (*fanout)++;
        if (*fanout == 1) {
            re->delivery = delivery;
            qd_router_delivery_fifo_enter_LH(delivery);
        }

        addr->deliveries_egress++;
        qd_link_activate(dest_link_ref->link->link);

        //
        // If the fanout is single, exit the loop here.  We only want to send one message copy.
        //
        if (QD_FANOUT(addr->semantics) == QD_FANOUT_SINGLE)
            break;

        dest_link_ref = DEQ_NEXT(dest_link_ref);
    }

    //
    // If dest_link_ref is not null here, we exited after sending one message copy.
    // If the number of local links is greater than one, rotate the head link to the
    // tail so we balance the message deliveries.
    //
    if (dest_link_ref && DEQ_SIZE(addr->rlinks) > 1) {
        assert(DEQ_HEAD(addr->rlinks) == dest_link_ref);
        DEQ_REMOVE_HEAD(addr->rlinks);
        DEQ_INSERT_TAIL(addr->rlinks, dest_link_ref);
    }
}


static void forward_to_remote_subscribers_LH(qd_router_t *router,
                                             qd_address_t *addr,
                                             qd_router_delivery_t *delivery,
                                             qd_message_t *msg,
                                             int *fanout,
                                             qd_field_iterator_t *ingress_iter)
{
    //
    // Get the mask bit associated with the ingress router for the message.
    // This will be compared against the "valid_origin" masks for each
    // candidate destination router.
    //
    int origin = -1;
    if (ingress_iter && !(addr->semantics & QD_BYPASS_VALID_ORIGINS)) {
        qd_address_iterator_reset_view(ingress_iter, ITER_VIEW_NODE_HASH);
        qd_address_t *origin_addr;
        qd_hash_retrieve(router->addr_hash, ingress_iter, (void*) &origin_addr);
        if (origin_addr && DEQ_SIZE(origin_addr->rnodes) == 1) {
            qd_router_ref_t *rref = DEQ_HEAD(origin_addr->rnodes);
            origin = rref->router->mask_bit;
        }
    } else
        origin = 0;

    //
    // Forward to the next-hops for remote destinations.
    //
    if (origin >= 0) {
        qd_router_ref_t  *dest_node_ref = DEQ_HEAD(addr->rnodes);
        qd_router_link_t *dest_link;
        qd_bitmask_t     *link_set = qd_bitmask(0);

        //
        // Loop over the target nodes for this address.  Build a set of outgoing links
        // for which there are valid targets.  We do this to avoid sending more than one
        // message down a given link.  It's possible that there are multiple destinations
        // for this address that are all reachable over the same link.  In this case, we
        // will send only one copy of the message over the link and allow a downstream
        // router to fan the message out.
        //
        while (dest_node_ref) {
            if (dest_node_ref->router->next_hop)
                dest_link = dest_node_ref->router->next_hop->peer_link;
            else
                dest_link = dest_node_ref->router->peer_link;
            if (dest_link && qd_bitmask_value(dest_node_ref->router->valid_origins, origin))
                qd_bitmask_set_bit(link_set, dest_link->mask_bit);
            dest_node_ref = DEQ_NEXT(dest_node_ref);
        }

        //
        // Send a copy of the message outbound on each identified link.
        //
        int link_bit;
        while (qd_bitmask_first_set(link_set, &link_bit)) {
            qd_bitmask_clear_bit(link_set, link_bit);
            dest_link = router->out_links_by_mask_bit[link_bit];
            if (dest_link) {
                qd_routed_event_t *re = new_qd_routed_event_t();
                DEQ_ITEM_INIT(re);
                re->delivery    = 0;
                re->message     = qd_message_copy(msg);
                re->settle      = 0;
                re->disposition = 0;
                DEQ_INSERT_TAIL(dest_link->msg_fifo, re);

                (*fanout)++;
                if (*fanout == 1) {
                    re->delivery = delivery;
                    qd_router_delivery_fifo_enter_LH(delivery);
                }

                addr->deliveries_transit++;
                qd_link_activate(dest_link->link);
            }
        }

        qd_bitmask_free(link_set);
    }
}


/** Multicast forwarder:
 */
static bool forwarder_multicast_LH(qd_router_forwarder_t *forwarder,
                                   qd_router_t *router,
                                   qd_message_t *msg,
                                   qd_router_delivery_t *delivery,
                                   qd_address_t *addr,
                                   qd_field_iterator_t *ingress_iter,
                                   bool is_direct)
{
    int fanout = 0;
    //
    // Forward to all of the local links receiving this address.
    //
    forward_to_direct_subscribers_LH(addr, delivery, msg, &fanout);

    //
    // If the address form is direct to this router node, don't relay it on to
    // any other part of the network.
    //
    if (!is_direct)
        forward_to_remote_subscribers_LH(router, addr, delivery, msg, &fanout, ingress_iter);

    return fanout != 0;
}


/** Forward using the 'closest' bias:
 */
static bool forwarder_anycast_closest_LH(qd_router_forwarder_t *forwarder,
                                         qd_router_t *router,
                                         qd_message_t *msg,
                                         qd_router_delivery_t *delivery,
                                         qd_address_t *addr,
                                         qd_field_iterator_t *ingress_iter,
                                         bool is_direct)
{
    int fanout = 0;
    //
    // First, try to find a directly connected consumer for the address.  If
    // there is none, then look for the closest remote consumer.
    //
    forward_to_direct_subscribers_LH(addr, delivery, msg, &fanout);
    if (fanout == 0 && !is_direct)
        forward_to_remote_subscribers_LH(router, addr, delivery, msg, &fanout, ingress_iter);

    return fanout != 0;
}


/** Forwarding using a 'balanced' bias:
 */
static bool forwarder_anycast_balanced_LH(qd_router_forwarder_t *forwarder,
                                          qd_router_t *router,
                                          qd_message_t *msg,
                                          qd_router_delivery_t *delivery,
                                          qd_address_t *addr,
                                          qd_field_iterator_t *ingress_iter,
                                          bool is_direct)
{
    int fanout = 0;
    //
    // Alternate between looking first for a local consumer and looking first
    // for a remote consumer.
    //
    addr->toggle = !addr->toggle;
    if (addr->toggle) {
        forward_to_direct_subscribers_LH(addr, delivery, msg, &fanout);
        if (fanout == 0 && !is_direct)
            forward_to_remote_subscribers_LH(router, addr, delivery, msg, &fanout, ingress_iter);
    } else {
        if (!is_direct)
            forward_to_remote_subscribers_LH(router, addr, delivery, msg, &fanout, ingress_iter);
        if (fanout == 0)
            forward_to_direct_subscribers_LH(addr, delivery, msg, &fanout);
    }

    return fanout != 0;
}


/* release method for default forwarders:
 */
static void forwarder_release(qd_router_forwarder_t *forwarder)
{
    // no-op - they're static singletons!
}


/* The default forwarders:
 */
static qd_router_forwarder_t multicast_forwarder = {
    forwarder_multicast_LH,     /* forward method */
    forwarder_release,
};
static qd_router_forwarder_t anycast_closest_forwarder = {
    forwarder_anycast_closest_LH,     /* forward method */
    forwarder_release,
};
static qd_router_forwarder_t anycast_balanced_forwarder = {
    forwarder_anycast_balanced_LH,     /* forward method */
    forwarder_release,
};


/** Get the proper default forwarder for an address of the given semantics:
 */
qd_router_forwarder_t *qd_router_get_forwarder(qd_address_semantics_t semantics)
{
    switch (QD_FANOUT(semantics)) {
    case QD_FANOUT_MULTIPLE:
        return &multicast_forwarder;
    case QD_FANOUT_SINGLE:
        switch (QD_BIAS(semantics)) {
        case QD_BIAS_CLOSEST:
            return &anycast_closest_forwarder;
        case QD_BIAS_SPREAD:
            return &anycast_balanced_forwarder;
        }
    }
    assert(false);  // invalid semantics? need new forwarder?
    return 0;
}



