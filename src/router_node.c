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

#include <qpid/dispatch/python_embedded.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <qpid/dispatch.h>
#include "dispatch_private.h"
#include "entity_cache.h"
#include "router_private.h"
#include "waypoint_private.h"
#include "router_core/management_agent_private.h"

const char *QD_ROUTER_NODE_TYPE = "router.node";
const char *QD_ROUTER_ADDRESS_TYPE = "router.address";
const char *QD_ROUTER_LINK_TYPE = "router.link";
const char *CORE_AGENT_ADDRESS = "$management";

static char *router_role    = "inter-router";
static char *on_demand_role = "on-demand";
static char *local_prefix   = "_local/";
static char *direct_prefix;
static char *node_id;

ALLOC_DEFINE(qd_routed_event_t);
ALLOC_DEFINE(qd_router_link_t);
ALLOC_DEFINE(qd_router_node_t);
ALLOC_DEFINE(qd_router_ref_t);
ALLOC_DEFINE(qd_router_link_ref_t);
ALLOC_DEFINE(qd_router_lrp_ref_t);
ALLOC_DEFINE(qd_address_t);
ALLOC_DEFINE(qd_router_conn_t);


qd_address_t* qd_address(qd_address_semantics_t semantics)
{
    qd_address_t* addr = new_qd_address_t();
    memset(addr, 0, sizeof(qd_address_t));
    DEQ_ITEM_INIT(addr);
    DEQ_INIT(addr->lrps);
    DEQ_INIT(addr->rlinks);
    DEQ_INIT(addr->rnodes);
    addr->semantics = semantics;
    addr->forwarder = 0; //qd_router_get_forwarder(semantics);
    return addr;
}

const char* qd_address_logstr(qd_address_t* address) {
    return (char*)qd_hash_key_by_handle(address->hash_handle);
}

void qd_router_add_link_ref_LH(qd_router_link_ref_list_t *ref_list, qd_router_link_t *link)
{
    qd_router_link_ref_t *ref = new_qd_router_link_ref_t();
    DEQ_ITEM_INIT(ref);
    ref->link = link;
    link->ref = ref;
    DEQ_INSERT_TAIL(*ref_list, ref);
}


void qd_router_del_link_ref_LH(qd_router_link_ref_list_t *ref_list, qd_router_link_t *link)
{
    if (link->ref) {
        DEQ_REMOVE(*ref_list, link->ref);
        free_qd_router_link_ref_t(link->ref);
        link->ref = 0;
    }
}


void qd_router_add_node_ref_LH(qd_router_ref_list_t *ref_list, qd_router_node_t *rnode)
{
    qd_router_ref_t *ref = new_qd_router_ref_t();
    DEQ_ITEM_INIT(ref);
    ref->router = rnode;
    rnode->ref_count++;
    DEQ_INSERT_TAIL(*ref_list, ref);
}


void qd_router_del_node_ref_LH(qd_router_ref_list_t *ref_list, qd_router_node_t *rnode)
{
    qd_router_ref_t *ref = DEQ_HEAD(*ref_list);
    while (ref) {
        if (ref->router == rnode) {
            DEQ_REMOVE(*ref_list, ref);
            free_qd_router_ref_t(ref);
            rnode->ref_count--;
            break;
        }
        ref = DEQ_NEXT(ref);
    }
}


void qd_router_add_lrp_ref_LH(qd_router_lrp_ref_list_t *ref_list, qd_lrp_t *lrp)
{
    qd_router_lrp_ref_t *ref = new_qd_router_lrp_ref_t();
    DEQ_ITEM_INIT(ref);
    ref->lrp = lrp;
    DEQ_INSERT_TAIL(*ref_list, ref);
}


void qd_router_del_lrp_ref_LH(qd_router_lrp_ref_list_t *ref_list, qd_lrp_t *lrp)
{
    qd_router_lrp_ref_t *ref = DEQ_HEAD(*ref_list);
    while (ref) {
        if (ref->lrp == lrp) {
            DEQ_REMOVE(*ref_list, ref);
            free_qd_router_lrp_ref_t(ref);
            break;
        }
        ref = DEQ_NEXT(ref);
    }
}


/**
 * Determine the role of a connection
 */
static qdr_connection_role_t qd_router_connection_role(const qd_connection_t *conn)
{
    if (conn) {
        const qd_server_config_t *cf = qd_connection_config(conn);
        if (cf && strcmp(cf->role, router_role) == 0)
            return QDR_ROLE_INTER_ROUTER;
        if (cf && strcmp(cf->role, on_demand_role) == 0)
            return QDR_ROLE_ON_DEMAND;
    }

    return QDR_ROLE_NORMAL;
}


void qd_router_link_free_LH(qd_router_link_t *rlink)
{
    qd_link_t *link = rlink->link;
    if (link) {
        qd_link_set_context(link, 0);
        qd_link_free_LH(link);
        rlink->link = 0;
    }

    if (rlink->target)
        free(rlink->target);

    assert(rlink->ref == 0);

    qd_routed_event_t      *re;

    re = DEQ_HEAD(rlink->event_fifo);
    while (re) {
        DEQ_REMOVE_HEAD(rlink->event_fifo);
        if (re->delivery && qd_router_delivery_fifo_exit_LH(re->delivery)) {
            qd_router_delivery_unlink_LH(re->delivery);
        }
        free_qd_routed_event_t(re);
        re = DEQ_HEAD(rlink->event_fifo);
    }

    re = DEQ_HEAD(rlink->msg_fifo);
    while (re) {
        DEQ_REMOVE_HEAD(rlink->msg_fifo);
        if (re->delivery)
            qd_router_delivery_fifo_exit_LH(re->delivery);
        if (re->message)
            qd_message_free(re->message);
        free_qd_routed_event_t(re);
        re = DEQ_HEAD(rlink->msg_fifo);
    }

    qd_router_delivery_t *delivery = DEQ_HEAD(rlink->deliveries);
    while (delivery) {
        // this unlinks the delivery from the rlink:
        qd_router_delivery_free_LH(delivery, PN_RELEASED);
        delivery = DEQ_HEAD(rlink->deliveries);
    }
    free_qd_router_link_t(rlink);
}


static int router_writable_conn_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qdr_connection_t *qconn = (qdr_connection_t*) qd_connection_get_context(conn);

    if (qconn)
        return qdr_connection_process(qconn);
    return 0;
}


/**
 * Outgoing Link Writable Handler
 * DEPRECATE
 */
/*static*/ int router_writable_link_handler(void* context, qd_link_t *link)
{
    qd_router_t            *router = (qd_router_t*) context;
    qd_router_delivery_t   *delivery;
    qd_router_link_t       *rlink = (qd_router_link_t*) qd_link_get_context(link);
    pn_link_t              *pn_link = qd_link_pn(link);
    uint64_t                tag;
    int                     link_credit = pn_link_credit(pn_link);
    qd_routed_event_list_t  to_send;
    qd_routed_event_list_t  events;
    qd_routed_event_t      *re;
    size_t                  offer;
    int                     event_count = 0;

    if (!rlink)
        return 0;

    bool drain_mode;
    bool drain_changed = qd_link_drain_changed(link, &drain_mode);

    DEQ_INIT(to_send);
    DEQ_INIT(events);

    sys_mutex_lock(router->lock);

    //
    // Pull the non-delivery events into a local list so they can be processed without
    // the lock being held.
    //
    re = DEQ_HEAD(rlink->event_fifo);
    while (re) {
        DEQ_REMOVE_HEAD(rlink->event_fifo);
        DEQ_INSERT_TAIL(events, re);
        re = DEQ_HEAD(rlink->event_fifo);
    }

    //
    // Under lock, move available deliveries from the msg_fifo to the local to_send
    // list.  Don't move more than we have credit to send.
    //
    if (link_credit > 0) {
        tag = router->dtag;
        re = DEQ_HEAD(rlink->msg_fifo);
        while (re) {
            DEQ_REMOVE_HEAD(rlink->msg_fifo);
            DEQ_INSERT_TAIL(to_send, re);
            if (DEQ_SIZE(to_send) == link_credit)
                break;
            re = DEQ_HEAD(rlink->msg_fifo);
        }
        router->dtag += DEQ_SIZE(to_send);
    }

    offer = DEQ_SIZE(rlink->msg_fifo);
    sys_mutex_unlock(router->lock);

    //
    // Deliver all the to_send messages downrange
    //
    re = DEQ_HEAD(to_send);
    while (re) {
        DEQ_REMOVE_HEAD(to_send);

        //
        // Get a delivery for the send.  This will be the current delivery on the link.
        //
        tag++;
        delivery = qd_router_link_new_delivery(rlink, pn_dtag((char*) &tag, 8));

        //
        // Send the message
        //
        qd_message_send(re->message, link, rlink->strip_outbound_annotations);

        //
        // Check the delivery associated with the queued message.  If it is not
        // settled, link it to the outgoing delivery for disposition/settlement
        // tracking.  If it is (pre-)settled, put it on the incoming link's event
        // queue to be locally settled.  This is done to hold session credit during
        // the time the message is in the outgoing message fifo.
        //
        sys_mutex_lock(router->lock);
        if (re->delivery) {
            if (qd_router_delivery_fifo_exit_LH(re->delivery)) {
                if (qd_router_delivery_settled(re->delivery)) {
                    qd_router_link_t  *peer_rlink = qd_router_delivery_link(re->delivery);
                    qd_routed_event_t *return_re  = new_qd_routed_event_t();
                    DEQ_ITEM_INIT(return_re);
                    return_re->delivery    = re->delivery;
                    return_re->message     = 0;
                    return_re->settle      = true;
                    return_re->disposition = 0;
                    qd_router_delivery_fifo_enter_LH(re->delivery);
                    DEQ_INSERT_TAIL(peer_rlink->event_fifo, return_re);
                    qd_link_activate(peer_rlink->link);
                } else
                    qd_router_delivery_link_peers_LH(re->delivery, delivery);
            }
        } else
            qd_router_delivery_free_LH(delivery, 0);  // settle and free
        sys_mutex_unlock(router->lock);

        pn_link_advance(pn_link);
        event_count++;

        qd_message_free(re->message);
        free_qd_routed_event_t(re);
        re = DEQ_HEAD(to_send);
    }

    //
    // Process the non-delivery events.
    //
    re = DEQ_HEAD(events);
    while (re) {
        DEQ_REMOVE_HEAD(events);

        if (re->delivery) {
            if (re->disposition) {
                pn_delivery_update(qd_router_delivery_pn(re->delivery), re->disposition);
                event_count++;
            }

            sys_mutex_lock(router->lock);

            bool ok = qd_router_delivery_fifo_exit_LH(re->delivery);
            if (ok && re->settle) {
                qd_router_delivery_unlink_LH(re->delivery);
                qd_router_delivery_free_LH(re->delivery, re->disposition);
                event_count++;
            }

            sys_mutex_unlock(router->lock);
        }

        free_qd_routed_event_t(re);
        re = DEQ_HEAD(events);
    }

    //
    // Set the offer to the number of messages remaining to be sent.
    //
    if (offer > 0)
        pn_link_offered(pn_link, offer);
    else {
        pn_link_drained(pn_link);

        //
        // If this link is in drain mode and it wasn't last time we came through here, we need to
        // count this operation as a work event.  This will allow the container to process the
        // connector and send out the flow(drain=true) response to the receiver.
        //
        if (drain_changed && drain_mode)
            event_count++;
    }

    return event_count;
}

static qd_field_iterator_t *router_annotate_message(qd_router_t       *router,
                                                    qd_parsed_field_t *in_ma,
                                                    qd_message_t      *msg,
                                                    int               *drop,
                                                    const char        *to_override,
                                                    bool strip_inbound_annotations)
{
    qd_field_iterator_t *ingress_iter = 0;

    qd_parsed_field_t *trace   = 0;
    qd_parsed_field_t *ingress = 0;
    qd_parsed_field_t *to      = 0;

    if (in_ma && !strip_inbound_annotations) {
        uint32_t count = qd_parse_sub_count(in_ma);
        bool done = false;

        for (uint32_t idx = 0; idx < count && !done; idx++) {
            qd_parsed_field_t *sub  = qd_parse_sub_key(in_ma, idx);
            if (!sub) continue;
            qd_field_iterator_t *iter = qd_parse_raw(sub);
            if (!iter) continue;

            if (qd_field_iterator_equal(iter, (unsigned char *)QD_MA_TRACE)) {
                trace = qd_parse_sub_value(in_ma, idx);
            } else if (qd_field_iterator_equal(iter, (unsigned char *)QD_MA_INGRESS)) {
                ingress = qd_parse_sub_value(in_ma, idx);
            } else if (qd_field_iterator_equal(iter, (unsigned char *)QD_MA_TO)) {
                to = qd_parse_sub_value(in_ma, idx);
            }
            done = trace && ingress && to;
        }
    }

    //
    // QD_MA_TRACE:
    // If there is a trace field, append this router's ID to the trace.
    // If the router ID is already in the trace the msg has looped.
    //
    qd_composed_field_t *trace_field = qd_compose_subfield(0);
    qd_compose_start_list(trace_field);
    if (trace) {
        if (qd_parse_is_list(trace)) {
            uint32_t idx = 0;
            qd_parsed_field_t *trace_item = qd_parse_sub_value(trace, idx);
            while (trace_item) {
                qd_field_iterator_t *iter = qd_parse_raw(trace_item);
                if (qd_field_iterator_equal(iter, (unsigned char*) node_id)) {
                    *drop = 1;
                    return 0;  // no further processing necessary
                }
                qd_field_iterator_reset(iter);
                qd_compose_insert_string_iterator(trace_field, iter);
                idx++;
                trace_item = qd_parse_sub_value(trace, idx);
            }
        }
    }
    qd_compose_insert_string(trace_field, node_id);
    qd_compose_end_list(trace_field);
    qd_message_set_trace_annotation(msg, trace_field);

    //
    // QD_MA_TO:
    // The supplied to override takes precedence over any existing
    // value.
    //
    if (to_override) {  // takes precedence over existing value
        qd_composed_field_t *to_field = qd_compose_subfield(0);
        qd_compose_insert_string(to_field, to_override);
        qd_message_set_to_override_annotation(msg, to_field);
    } else if (to) {
        qd_composed_field_t *to_field = qd_compose_subfield(0);
        qd_compose_insert_string_iterator(to_field, qd_parse_raw(to));
        qd_message_set_to_override_annotation(msg, to_field);
    }

    //
    // QD_MA_INGRESS:
    // If there is no ingress field, annotate the ingress as
    // this router else keep the original field.
    //
    qd_composed_field_t *ingress_field = qd_compose_subfield(0);
    if (ingress && qd_parse_is_scalar(ingress)) {
        ingress_iter = qd_parse_raw(ingress);
        qd_compose_insert_string_iterator(ingress_field, ingress_iter);
    } else
        qd_compose_insert_string(ingress_field, node_id);
    qd_message_set_ingress_annotation(msg, ingress_field);

    //
    // Return the iterator to the ingress field _if_ it was present.
    // If we added the ingress, return NULL.
    //
    return ingress_iter;
}


/**
 * Inbound Delivery Handler
 */
static void router_rx_handler(void* context, qd_link_t *link, pn_delivery_t *pnd)
{
    qd_router_t      *router  = (qd_router_t*) context;
    pn_link_t        *pn_link = qd_link_pn(link);
    qd_router_link_t *rlink   = (qd_router_link_t*) qd_link_get_context(link);
    qd_message_t     *msg;
    int               valid_message = 0;

    //
    // Receive the message into a local representation.  If the returned message
    // pointer is NULL, we have not yet received a complete message.
    //
    // Note:  In the link-routing case, consider cutting the message through.  There's
    //        no reason to wait for the whole message to be received before starting to
    //        send it.
    //
    msg = qd_message_receive(pnd);
    if (!msg)
        return;

    //
    // Consume the delivery.
    //
    pn_link_advance(pn_link);

    //
    // If there's no router link, free the message and finish.  It's likely that the link
    // is closing.
    //
    if (!rlink) {
        qd_message_free(msg);
        return;
    }

    //
    // Handle the Link-Routing case.
    //
    sys_mutex_lock(router->lock);
    qd_router_link_t *clink = rlink->connected_link;
    if (clink) {
        //router_link_route_delivery_LH(clink, qd_router_delivery(rlink, pnd), msg);
        sys_mutex_unlock(router->lock);
        return;
    }

    //
    // Handle the Message-Routing case.  Start by issuing a replacement credit.
    //
    pn_link_flow(pn_link, 1);

    //
    // Validate the message through the Properties section so we can access the TO field.
    //
    qd_message_t           *in_process_copy    = 0;
    qd_router_message_cb_t  on_message         = 0;
    void                   *on_message_context = 0;

    valid_message = qd_message_check(msg, QD_DEPTH_PROPERTIES);

    if (valid_message) {
        qd_parsed_field_t   *in_ma     = 0;
        qd_field_iterator_t *iter      = 0;
        bool                 free_iter = true;
        char                *to_override  = 0;
        bool                 forwarded = false;
        qd_router_delivery_t *delivery = qd_router_delivery(rlink, pnd);

        //
        // Only respect the delivery annotations if the message came from another router.
        //
        if (rlink->link_type != QD_LINK_WAYPOINT)
            in_ma = qd_message_message_annotations(msg);

        //
        // If the message has delivery annotations, get the to-override field from the annotations.
        //
        if (in_ma) {
            qd_parsed_field_t *ma_to = qd_parse_value_by_key(in_ma, QD_MA_TO);
            if (ma_to) {
                iter      = qd_parse_raw(ma_to);
                free_iter = false;
            }
        }

        //
        // If this is a waypoint link, set the address (and to_override) to the phased
        // address for the link.
        //
        if (!iter && rlink->waypoint) {
            iter = qd_address_iterator_string(rlink->waypoint->address, ITER_VIEW_ADDRESS_HASH);
            qd_address_iterator_set_phase(iter, rlink->waypoint->out_phase);
        }

        //
        // Still no destination address?  Use the TO field from the message properties.
        //
        if (!iter)
            iter = qd_message_field_iterator(msg, QD_FIELD_TO);

        //
        // Handle the case where the TO field is absent and the incoming link has a target
        // address.  Use the target address in the lookup in lieu of a TO address.
        // Note also that the message must then be annotated with a TO-OVERRIDE field in
        // the delivery annotations.
        //
        // ref: https://issues.apache.org/jira/browse/DISPATCH-1
        //
        if (!iter && rlink->target) {
            iter = qd_address_iterator_string(rlink->target, ITER_VIEW_ALL);
            to_override = rlink->target;
        }

        if (iter) {
            //
            // Note: This function is going to need to be refactored so we can put an
            //       asynchronous address lookup here.  In the event there is a translation
            //       of the address (via namespace), it will have to be done here after
            //       obtaining the iterator and before doing the hash lookup.
            //
            //       Note that this lookup is only done for global/mobile class addresses.
            //
            bool is_local;
            bool is_direct;
            qd_address_t *addr = qd_router_address_lookup_LH(router, iter, &is_local, &is_direct);
            if (free_iter)
                qd_field_iterator_free(iter);

            if (addr) {
                //
                // If the incoming link is an endpoint link, count this as an ingress delivery.
                //
                if (rlink->link_type == QD_LINK_ENDPOINT)
                    addr->deliveries_ingress++;

                //
                // TO field is valid and contains a known destination.  Handle the various
                // cases for forwarding.
                //
                // Interpret and update the delivery annotations of the message.  As a convenience,
                // this function returns the iterator to the ingress field (if it exists).  It also
                // returns a 'drop' indication if it detects that the message will loop.
                //
                int drop = 0;
                qd_field_iterator_t *ingress_iter = router_annotate_message(router, in_ma, msg, &drop, to_override, rlink->strip_inbound_annotations);

                if (!drop) {
                    //
                    // Forward a copy of the message to the in-process endpoint for
                    // this address if there is one.  The actual invocation of the
                    // handler will occur later after we've released the lock.
                    //
                    if (addr->on_message) {
                        in_process_copy = qd_message_copy(msg);
                        on_message         = addr->on_message;
                        on_message_context = addr->on_message_context;
                        addr->deliveries_to_container++;
                    }

                    //
                    // If the address form is local (i.e. is prefixed by _local), don't forward
                    // outside of the router process.
                    //
                    if (!is_local && router->router_mode != QD_ROUTER_MODE_ENDPOINT) {
                        qd_router_forwarder_t *f = addr->forwarder;
                        forwarded = f->forward(f, router, msg, delivery, addr, ingress_iter, is_direct);
                    }
                }
            }
        }

        if (!forwarded) {
            if (on_message)
                // our local in-process handler will accept it:
                qd_router_delivery_free_LH(delivery, PN_ACCEPTED);
            else {
                // no one has accepted it, so inform sender
                qd_router_delivery_set_undeliverable_LH(delivery);
                qd_router_delivery_free_LH(delivery, PN_MODIFIED);
            }
        }
    } else {
        //
        // Message is invalid.  Reject the message.
        //
        pn_delivery_update(pnd, PN_REJECTED);
        pn_delivery_settle(pnd);
    }

    sys_mutex_unlock(router->lock);
    qd_message_free(msg);

    //
    // Invoke the in-process handler now that the lock is released.
    //
    if (on_message) {
        on_message(on_message_context, in_process_copy, rlink->mask_bit);
        qd_message_free(in_process_copy);
    }
}


/**
 * Delivery Disposition Handler
 */
static void router_disposition_handler(void* context, qd_link_t *link, pn_delivery_t *pnd)
{
    qd_router_t   *router  = (qd_router_t*) context;
    qd_router_delivery_t *delivery = (qd_router_delivery_t *)pn_delivery_get_context(pnd);
    if (!delivery) return;

    bool           changed = qd_router_delivery_disp_changed(delivery);
    uint64_t       disp    = qd_router_delivery_disp(delivery);
    bool           settled = qd_router_delivery_settled(delivery);

    sys_mutex_lock(router->lock);
    qd_router_delivery_t *peer = qd_router_delivery_peer(delivery);
    if (peer) {
        //
        // The case where this delivery has a peer.
        //
        if (changed || settled) {
            qd_router_link_t  *peer_link = qd_router_delivery_link(peer);
            qd_routed_event_t *re        = new_qd_routed_event_t();
            DEQ_ITEM_INIT(re);
            re->delivery    = peer;
            re->message     = 0;
            re->settle      = settled;
            re->disposition = changed ? disp : 0;

            qd_router_delivery_fifo_enter_LH(peer);
            DEQ_INSERT_TAIL(peer_link->event_fifo, re);
            if (settled) {
                qd_router_delivery_unlink_LH(delivery);
                qd_router_delivery_free_LH(delivery, 0);
            }

            qd_link_activate(peer_link->link);
        }
    } else if (settled)
        qd_router_delivery_free_LH(delivery, 0);

    sys_mutex_unlock(router->lock);
}


typedef struct link_attach_t {
    qd_router_t      *router;
    qd_router_link_t *peer_link;
    qd_link_t        *peer_qd_link;
    char             *link_name;
    qd_direction_t    dir;
    qd_connection_t  *conn;
    int               credit;
} link_attach_t;

ALLOC_DECLARE(link_attach_t);
ALLOC_DEFINE(link_attach_t);


#define COND_NAME_LEN        127
#define COND_DESCRIPTION_LEN 511

typedef struct link_detach_t {
    qd_router_t      *router;
    qd_router_link_t *rlink;
    char              condition_name[COND_NAME_LEN + 1];
    char              condition_description[COND_DESCRIPTION_LEN + 1];
    pn_data_t        *condition_info;
} link_detach_t;

ALLOC_DECLARE(link_detach_t);
ALLOC_DEFINE(link_detach_t);


typedef struct link_event_t {
    qd_router_t      *router;
    qd_router_link_t *rlink;
    int               credit;
    bool              drain;
} link_event_t;

ALLOC_DECLARE(link_event_t);
ALLOC_DEFINE(link_event_t);


typedef enum {
    LINK_ATTACH_FORWARDED = 1,  ///< The attach was forwarded
    LINK_ATTACH_NO_MATCH  = 2,  ///< No link-route address was found
    LINK_ATTACH_NO_PATH   = 3   ///< Link-route exists but there's no reachable destination
} link_attach_result_t;


static void qd_router_flow(void *context, bool discard)
{
    link_event_t *le = (link_event_t*) context;

    if (!discard) {
        qd_link_t *link    = le->rlink->link;
        pn_link_t *pn_link = qd_link_pn(link);
        int delta          = le->credit - pn_link_credit(pn_link);
        if (delta > 0) {
            pn_link_flow(pn_link, delta);
            qd_link_activate(link);
        }
    }

    free_link_event_t(le);
}


qd_router_link_t* qd_router_link(qd_link_t *link, qd_link_type_t link_type, qd_direction_t direction, qd_address_t *owning_addr, qd_waypoint_t *wp, int mask_bit)
{
    qd_router_link_t *rlink = new_qd_router_link_t();
    DEQ_ITEM_INIT(rlink);
    rlink->mask_bit       = mask_bit;
    rlink->link_type      = link_type;
    rlink->link_direction = direction;
    rlink->owning_addr    = owning_addr;
    rlink->waypoint       = wp;
    rlink->link           = link;
    rlink->connected_link = 0;
    rlink->ref            = 0;
    rlink->target         = 0;
    rlink->strip_inbound_annotations  = false;
    rlink->strip_outbound_annotations = false;
    DEQ_INIT(rlink->event_fifo);
    DEQ_INIT(rlink->msg_fifo);
    DEQ_INIT(rlink->deliveries);

    //Get the configuration via the connection's listener.
	qd_connection_t *connection = qd_link_connection(link);
    if (connection) {
        const qd_server_config_t *config = connection->listener ?
            connection->listener->config : connection->connector->config;

        if (config) {
            //strip_inbound_annotations and strip_outbound_annotations don't apply to inter router links.
            if (rlink->link_type != QD_LINK_ROUTER) {
                if (rlink->link_direction == QD_INCOMING) {
                    rlink->strip_inbound_annotations  = config->strip_inbound_annotations;
                } else {
                    rlink->strip_outbound_annotations = config->strip_outbound_annotations;
                }
            }
        }
    }

    return rlink;
}

/**
 * New Incoming Link Handler
 */
static int router_incoming_link_handler(void* context, qd_link_t *link)
{
    qd_connection_t  *conn     = qd_link_connection(link);
    qdr_connection_t *qdr_conn = (qdr_connection_t*) qd_connection_get_context(conn);
    qdr_link_t       *qdr_link = qdr_link_first_attach(qdr_conn, QD_INCOMING,
                                                       qdr_terminus(qd_link_remote_source(link)),
                                                       qdr_terminus(qd_link_remote_target(link)),
                                                       pn_link_name(qd_link_pn(link)));
    qdr_link_set_context(qdr_link, link);
    qd_link_set_context(link, qdr_link);

    return 0;
}


/**
 * New Outgoing Link Handler
 */
static int router_outgoing_link_handler(void* context, qd_link_t *link)
{
    qd_connection_t  *conn     = qd_link_connection(link);
    qdr_connection_t *qdr_conn = (qdr_connection_t*) qd_connection_get_context(conn);
    qdr_link_t       *qdr_link = qdr_link_first_attach(qdr_conn, QD_OUTGOING,
                                                       qdr_terminus(qd_link_remote_source(link)),
                                                       qdr_terminus(qd_link_remote_target(link)),
                                                       pn_link_name(qd_link_pn(link)));
    qdr_link_set_context(qdr_link, link);
    qd_link_set_context(link, qdr_link);

    return 0;
}


/**
 * Handler for remote opening of links that we initiated.
 */
static int router_link_attach_handler(void* context, qd_link_t *link)
{
    qdr_link_t *qlink = (qdr_link_t*) qd_link_get_context(link);
    qdr_link_second_attach(qlink, qdr_terminus(qd_link_remote_source(link)), qdr_terminus(qd_link_remote_target(link)));

    return 0;
}


/**
 * Handler for flow events on links
 */
static int router_link_flow_handler(void* context, qd_link_t *link)
{
    qd_router_t      *router     = (qd_router_t*) context;
    qd_router_link_t *rlink      = (qd_router_link_t*) qd_link_get_context(link);
    pn_link_t        *pn_link    = qd_link_pn(link);

    if (!rlink)
        return 0;

    sys_mutex_lock(router->lock);
    qd_router_link_t *peer_rlink = rlink->connected_link;

    if (peer_rlink) {
        qd_connection_t *out_conn = qd_link_connection(peer_rlink->link);
        if (out_conn) {
            if (rlink->link_direction == QD_OUTGOING) {
                //
                // Outgoing link handling
                //
                int credit = pn_link_remote_credit(pn_link) - DEQ_SIZE(rlink->msg_fifo);
                if (credit > 0) {
                    link_event_t *le = new_link_event_t();
                    memset(le, 0, sizeof(link_event_t));
                    le->router = router;
                    le->rlink  = peer_rlink;
                    le->credit = credit;
                    le->drain  = false;
                    qd_connection_invoke_deferred(out_conn, qd_router_flow, le);
                }
            } else {
                //
                // Incoming link handling
                //
            }
        }
    }

    sys_mutex_unlock(router->lock);
    return 0;
}


/**
 * Link Detached Handler
 */
static int router_link_detach_handler(void* context, qd_link_t *link, qd_detach_type_t dt)
{
    qdr_link_t     *rlink  = (qdr_link_t*) qd_link_get_context(link);
    pn_condition_t *cond   = qd_link_pn(link) ? pn_link_remote_condition(qd_link_pn(link)) : 0;

    if (rlink) {
        qdr_error_t *error = qdr_error_from_pn(cond);
        if (!error && dt == QD_LOST)
            error = qdr_error("qd:routed-link-lost", "Connectivity to the peer container was lost");
        qdr_link_detach(rlink, dt, error);
    }

    return 0;
}


static int router_inbound_opened_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qd_router_t           *router = (qd_router_t*) type_context;
    qdr_connection_role_t  role   = qd_router_connection_role(conn);
    qdr_connection_t      *qdrc   = qdr_connection_opened(router->router_core, true, role, 0); // TODO - get label

    qd_connection_set_context(conn, qdrc);
    qdr_connection_set_context(qdrc, conn);

    return 0;
}


static int router_outbound_opened_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qd_router_t           *router = (qd_router_t*) type_context;
    qdr_connection_role_t  role   = qd_router_connection_role(conn);
    qdr_connection_t      *qdrc   = qdr_connection_opened(router->router_core, false, role, 0); // TODO - get label

    qd_connection_set_context(conn, qdrc);
    qdr_connection_set_context(qdrc, conn);

    return 0;
}


static int router_closed_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qdr_connection_t *qdrc = (qdr_connection_t*) qd_connection_get_context(conn);
    qdr_connection_closed(qdrc);
    qd_connection_set_context(conn, 0);

    return 0;
}


static void qd_router_timer_handler(void *context)
{
    qd_router_t *router = (qd_router_t*) context;

    //
    // Periodic processing.
    //
    qd_pyrouter_tick(router);
    qd_timer_schedule(router->timer, 1000);
}


static qd_node_type_t router_node = {"router", 0, 0,
                                     router_rx_handler,
                                     router_disposition_handler,
                                     router_incoming_link_handler,
                                     router_outgoing_link_handler,
                                     router_writable_conn_handler,
                                     router_link_detach_handler,
                                     router_link_attach_handler,
                                     router_link_flow_handler,
                                     0,   // node_created_handler
                                     0,   // node_destroyed_handler
                                     router_inbound_opened_handler,
                                     router_outbound_opened_handler,
                                     router_closed_handler};
static int type_registered = 0;

qd_router_t *qd_router(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id)
{
    if (!type_registered) {
        type_registered = 1;
        qd_container_register_node_type(qd, &router_node);
    }

    size_t dplen = 9 + strlen(area) + strlen(id);
    direct_prefix = (char*) malloc(dplen);
    strcpy(direct_prefix, "_topo/");
    strcat(direct_prefix, area);
    strcat(direct_prefix, "/");
    strcat(direct_prefix, id);
    strcat(direct_prefix, "/");

    node_id = (char*) malloc(dplen);
    strcpy(node_id, area);
    strcat(node_id, "/");
    strcat(node_id, id);

    qd_router_t *router = NEW(qd_router_t);
    ZERO(router);

    router_node.type_context = router;

    qd->router = router;
    router->qd           = qd;
    router->router_core  = 0;
    router->log_source   = qd_log_source("ROUTER");
    router->router_mode  = mode;
    router->router_area  = area;
    router->router_id    = id;
    router->node         = qd_container_set_default_node_type(qd, &router_node, (void*) router, QD_DIST_BOTH);
    DEQ_INIT(router->addrs);
    router->addr_hash    = qd_hash(10, 32, 0);

    DEQ_INIT(router->links);
    DEQ_INIT(router->routers);
    DEQ_INIT(router->lrp_containers);

    router->out_links_by_mask_bit = NEW_PTR_ARRAY(qd_router_link_t, qd_bitmask_width());
    router->routers_by_mask_bit   = NEW_PTR_ARRAY(qd_router_node_t, qd_bitmask_width());
    for (int idx = 0; idx < qd_bitmask_width(); idx++) {
        router->out_links_by_mask_bit[idx] = 0;
        router->routers_by_mask_bit[idx]   = 0;
    }

    router->neighbor_free_mask = qd_bitmask(1);
    router->lock               = sys_mutex();
    router->timer              = qd_timer(qd, qd_router_timer_handler, (void*) router);
    router->dtag               = 1;
    DEQ_INIT(router->config_addrs);
    DEQ_INIT(router->waypoints);

    //
    // Inform the field iterator module of this router's id and area.  The field iterator
    // uses this to offload some of the address-processing load from the router.
    //
    qd_field_iterator_set_address(area, id);

    //
    // Seed the random number generator
    //
    unsigned int seed = (unsigned int) time(0);
    srandom(seed);

    switch (router->router_mode) {
    case QD_ROUTER_MODE_STANDALONE: qd_log(router->log_source, QD_LOG_INFO, "Router started in Standalone mode");  break;
    case QD_ROUTER_MODE_INTERIOR:   qd_log(router->log_source, QD_LOG_INFO, "Router started in Interior mode, area=%s id=%s", area, id);  break;
    case QD_ROUTER_MODE_EDGE:       qd_log(router->log_source, QD_LOG_INFO, "Router started in Edge mode");  break;
    case QD_ROUTER_MODE_ENDPOINT:   qd_log(router->log_source, QD_LOG_INFO, "Router started in Endpoint mode");  break;
    }

    return router;
}


static void qd_router_connection_activate(void *context, qdr_connection_t *conn)
{
    qd_server_activate((qd_connection_t*) qdr_connection_get_context(conn));
}


static void qd_router_link_first_attach(void             *context,
                                        qdr_connection_t *conn,
                                        qdr_link_t       *link, 
                                        qdr_terminus_t   *source,
                                        qdr_terminus_t   *target)
{
    qd_router_t     *router = (qd_router_t*) context;
    qd_connection_t *qconn  = (qd_connection_t*) qdr_connection_get_context(conn);

    //
    // Create a new link to be attached
    //
    qd_link_t *qlink = qd_link(router->node, qconn, qdr_link_direction(link), qdr_link_name(link));

    //
    // Copy the source and target termini to the link
    //
    qdr_terminus_copy(source, qd_link_source(qlink));
    qdr_terminus_copy(target, qd_link_target(qlink));

    //
    // Associate the qd_link and the qdr_link to each other
    //
    qdr_link_set_context(link, qlink);
    qd_link_set_context(qlink, link);

    //
    // Open (attach) the link
    //
    pn_link_open(qd_link_pn(qlink));
}


static void qd_router_link_second_attach(void *context, qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target)
{
    qd_link_t *qlink = (qd_link_t*) qdr_link_get_context(link);
    if (!qlink)
        return;

    qdr_terminus_copy(source, qd_link_source(qlink));
    qdr_terminus_copy(target, qd_link_target(qlink));

    //
    // Open (attach) the link
    //
    pn_link_open(qd_link_pn(qlink));
}


static void qd_router_link_detach(void *context, qdr_link_t *link, qdr_error_t *error)
{
}


void qd_router_setup_late(qd_dispatch_t *qd)
{
    qd->router->router_core = qdr_core(qd, qd->router->router_area, qd->router->router_id);

    qdr_connection_handlers(qd->router->router_core, (void*) qd->router,
                            qd_router_connection_activate,
                            qd_router_link_first_attach,
                            qd_router_link_second_attach,
                            qd_router_link_detach);

    qd_router_python_setup(qd->router);
    qd_timer_schedule(qd->router->timer, 1000);
}

void qd_router_free(qd_router_t *router)
{
    if (!router) return;

    qd_container_set_default_node_type(router->qd, 0, 0, QD_DIST_BOTH);

    for (qd_address_t *addr = DEQ_HEAD(router->addrs); addr; addr = DEQ_HEAD(router->addrs)) {
        for (qd_router_link_ref_t *rlink = DEQ_HEAD(addr->rlinks); rlink; rlink = DEQ_HEAD(addr->rlinks)) {
            DEQ_REMOVE_HEAD(addr->rlinks);
            free_qd_router_link_ref_t(rlink);
        }

        for (qd_router_ref_t *rnode = DEQ_HEAD(addr->rnodes); rnode; rnode = DEQ_HEAD(addr->rnodes)) {
            DEQ_REMOVE_HEAD(addr->rnodes);
            free_qd_router_ref_t(rnode);
        }

        qd_hash_handle_free(addr->hash_handle);
        DEQ_REMOVE_HEAD(router->addrs);
        qd_entity_cache_remove(QD_ROUTER_ADDRESS_TYPE, addr);
        free_qd_address_t(addr);
    }

    qdr_core_free(router->router_core);
    qd_timer_free(router->timer);
    sys_mutex_free(router->lock);
    qd_bitmask_free(router->neighbor_free_mask);
    free(router->out_links_by_mask_bit);
    free(router->routers_by_mask_bit);
    qd_hash_free(router->addr_hash);
    qd_router_configure_free(router);
    qd_router_python_free(router);

    free(router);
    free(node_id);
    free(direct_prefix);
}


const char *qd_router_id(const qd_dispatch_t *qd)
{
    return node_id;
}


qdr_core_t *qd_router_core(qd_dispatch_t *qd)
{
    return qd->router->router_core;
}


qd_address_t *qd_router_address_lookup_LH(qd_router_t *router,
                                          qd_field_iterator_t *addr_iter,
                                          bool *is_local, bool *is_direct)
{
    qd_address_t *addr = 0;
    qd_address_iterator_reset_view(addr_iter, ITER_VIEW_ADDRESS_HASH);
    qd_hash_retrieve(router->addr_hash, addr_iter, (void*) &addr);
    qd_address_iterator_reset_view(addr_iter, ITER_VIEW_NO_HOST);
    *is_local  = (bool) qd_field_iterator_prefix(addr_iter, local_prefix);
    *is_direct = (bool) qd_field_iterator_prefix(addr_iter, direct_prefix);
    return addr;
}


void qd_router_send(qd_dispatch_t       *qd,
                    qd_field_iterator_t *address,
                    qd_message_t        *msg)
{

    qd_router_t  *router = qd->router;
    qd_address_t *addr;

    qd_address_iterator_reset_view(address, ITER_VIEW_ADDRESS_HASH);
    sys_mutex_lock(router->lock);
    qd_hash_retrieve(router->addr_hash, address, (void*) &addr);

    if (addr) {
        //
        // Forward to all of the local links receiving this address.
        //
        addr->deliveries_from_container++;
        qd_router_link_ref_t *dest_link_ref = DEQ_HEAD(addr->rlinks);
        while (dest_link_ref) {
            qd_routed_event_t *re = new_qd_routed_event_t();
            DEQ_ITEM_INIT(re);
            re->delivery    = 0;
            re->message     = qd_message_copy(msg);
            re->settle      = 0;
            re->disposition = 0;
            DEQ_INSERT_TAIL(dest_link_ref->link->msg_fifo, re);

            qd_link_activate(dest_link_ref->link->link);
            addr->deliveries_egress++;

            dest_link_ref = DEQ_NEXT(dest_link_ref);
        }

        //
        // Forward to the next-hops for remote destinations.
        //
        qd_router_ref_t  *dest_node_ref = DEQ_HEAD(addr->rnodes);
        qd_router_link_t *dest_link;
        qd_bitmask_t     *link_set = qd_bitmask(0);

        while (dest_node_ref) {
            if (dest_node_ref->router->next_hop)
                dest_link = dest_node_ref->router->next_hop->peer_link;
            else
                dest_link = dest_node_ref->router->peer_link;
            if (dest_link)
                qd_bitmask_set_bit(link_set, dest_link->mask_bit);
            dest_node_ref = DEQ_NEXT(dest_node_ref);
        }

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
                qd_link_activate(dest_link->link);
                addr->deliveries_transit++;
            }
        }

        qd_bitmask_free(link_set);
    }
    sys_mutex_unlock(router->lock); // TOINVESTIGATE Move this higher?
}


void qd_router_send2(qd_dispatch_t *qd,
                     const char    *address,
                     qd_message_t  *msg)
{
    if (address && msg) {
        qd_field_iterator_t *iter = qd_address_iterator_string(address, ITER_VIEW_ADDRESS_HASH);
        qd_router_send(qd, iter, msg);
        qd_field_iterator_free(iter);
    }
}
