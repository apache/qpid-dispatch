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

const char *QD_ROUTER_NODE_TYPE = "router.node";
const char *QD_ROUTER_ADDRESS_TYPE = "router.address";
const char *QD_ROUTER_LINK_TYPE = "router.link";

static char *router_role    = "inter-router";
static char *on_demand_role = "on-demand";
static char *local_prefix   = "_local/";
static char *topo_prefix    = "_topo/";
static char *direct_prefix;
static char *node_id;

/*
 * Address Types and Processing:
 *
 *     Address                              Hash Key       onReceive
 *     ===================================================================
 *     _local/<local>                       L<local>               handler
 *     _topo/<area>/<router>/<local>        A<area>        forward
 *     _topo/<my-area>/<router>/<local>     R<router>      forward
 *     _topo/<my-area>/<my-router>/<local>  L<local>               handler
 *     _topo/<area>/all/<local>             A<area>        forward
 *     _topo/<my-area>/all/<local>          L<local>       forward handler
 *     _topo/all/all/<local>                L<local>       forward handler
 *     <mobile>                             M<mobile>      forward handler
 */

ALLOC_DEFINE(qd_routed_event_t);
ALLOC_DEFINE(qd_router_link_t);
ALLOC_DEFINE(qd_router_node_t);
ALLOC_DEFINE(qd_router_ref_t);
ALLOC_DEFINE(qd_router_link_ref_t);
ALLOC_DEFINE(qd_router_lrp_ref_t);
ALLOC_DEFINE(qd_address_t);
ALLOC_DEFINE(qd_router_conn_t);

qd_address_t* qd_address() {
    qd_address_t* addr = new_qd_address_t();
    memset(addr, 0, sizeof(qd_address_t));
    DEQ_ITEM_INIT(addr);
    DEQ_INIT(addr->lrps);
    DEQ_INIT(addr->rlinks);
    DEQ_INIT(addr->rnodes);
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
 * Check an address to see if it no longer has any associated destinations.
 * Depending on its policy, the address may be eligible for being closed out
 * (i.e. Logging its terminal statistics and freeing its resources).
 */
void qd_router_check_addr(qd_router_t *router, qd_address_t *addr, int was_local)
{
    if (addr == 0)
        return;

    unsigned char *key            = 0;
    int            to_delete      = 0;
    int            no_more_locals = 0;

    sys_mutex_lock(router->lock);

    //
    // If the address has no handlers or destinations, it should be deleted.
    //
    if (addr->handler == 0 &&
        DEQ_SIZE(addr->rlinks) == 0 && DEQ_SIZE(addr->rnodes) == 0 &&
        !addr->waypoint && !addr->block_deletion)
        to_delete = 1;

    //
    // If we have just removed a local linkage and it was the last local linkage,
    // we need to notify the router module that there is no longer a local
    // presence of this address.
    //
    if (was_local && DEQ_SIZE(addr->rlinks) == 0)
        no_more_locals = 1;

    if (to_delete) {
        //
        // Delete the address but grab the hash key so we can use it outside the
        // critical section.
        //
        qd_hash_remove_by_handle2(router->addr_hash, addr->hash_handle, &key);
        DEQ_REMOVE(router->addrs, addr);
        qd_entity_cache_remove(QD_ROUTER_ADDRESS_TYPE, addr);
        qd_hash_handle_free(addr->hash_handle);
        free_qd_address_t(addr);
    }

    //
    // If we're not deleting but there are no more locals, get a copy of the hash key.
    //
    if (!to_delete && no_more_locals) {
        const unsigned char *key_const = qd_hash_key_by_handle(addr->hash_handle);
        key = (unsigned char*) malloc(strlen((const char*) key_const) + 1);
        strcpy((char*) key, (const char*) key_const);
    }

    sys_mutex_unlock(router->lock);

    //
    // If the address is mobile-class and it was just removed from a local link,
    // tell the router module that it is no longer attached locally.
    //
    if (no_more_locals && key && key[0] == 'M')
        qd_router_mobile_removed(router, (const char*) key);

    //
    // Free the key that was not freed by the hash table.
    //
    if (key)
        free(key);
}


/**
 * Determine whether a connection is configured in the inter-router role.
 */
static int qd_router_connection_is_inter_router(const qd_connection_t *conn)
{
    if (!conn)
        return 0;

    const qd_server_config_t *cf = qd_connection_config(conn);
    if (cf && strcmp(cf->role, router_role) == 0)
        return 1;

    return 0;
}


/**
 * Determine whether a connection is configured in the on-demand role.
 */
static int qd_router_connection_is_on_demand(const qd_connection_t *conn)
{
    if (!conn)
        return 0;

    const qd_server_config_t *cf = qd_connection_config(conn);
    if (cf && strcmp(cf->role, on_demand_role) == 0)
        return 1;

    return 0;
}


/**
 * Determine whether a terminus has router capability
 */
static int qd_router_terminus_is_router(pn_terminus_t *term)
{
    pn_data_t *cap = pn_terminus_capabilities(term);

    pn_data_rewind(cap);
    pn_data_next(cap);
    if (cap && pn_data_type(cap) == PN_SYMBOL) {
        pn_bytes_t sym = pn_data_get_symbol(cap);
        if (sym.size == strlen(QD_CAPABILITY_ROUTER) &&
            strcmp(sym.start, QD_CAPABILITY_ROUTER) == 0)
            return 1;
    }

    return 0;
}


/**
 * If the terminus has a dynamic-node-property for a node address,
 * return an interator for the content of that property.
 */
static const char *qd_router_terminus_dnp_address(pn_terminus_t *term)
{
    pn_data_t *props = pn_terminus_properties(term);

    if (!props)
        return 0;

    pn_data_rewind(props);
    if (pn_data_next(props) && pn_data_enter(props) && pn_data_next(props)) {
        pn_bytes_t sym = pn_data_get_symbol(props);
        if (sym.start && strcmp(QD_DYNAMIC_NODE_PROPERTY_ADDRESS, sym.start) == 0) {
            if (pn_data_next(props)) {
                pn_bytes_t val = pn_data_get_string(props);
                if (val.start && *val.start != '\0')
                    return val.start;
            }
        }
    }

    return 0;
}


/**
 * Generate a temporary routable address for a destination connected to this
 * router node.
 */
static void qd_router_generate_temp_addr(qd_router_t *router, char *buffer, size_t length)
{
    static const char *table = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+_";
    char     discriminator[11];
    long int rnd1 = random();
    long int rnd2 = random();
    int      idx;
    int      cursor = 0;

    for (idx = 0; idx < 5; idx++) {
        discriminator[cursor++] = table[(rnd1 >> (idx * 6)) & 63];
        discriminator[cursor++] = table[(rnd2 >> (idx * 6)) & 63];
    }
    discriminator[cursor] = '\0';

    snprintf(buffer, length, "amqp:/%s%s/%s/temp.%s", topo_prefix, router->router_area, router->router_id, discriminator);
}


/**
 * Assign a link-mask-bit to a new link.  Do this in such a way that all links on the same
 * connection share the same mask-bit value.
 */
static int qd_router_find_mask_bit_LH(qd_router_t *router, qd_link_t *link)
{
    qd_router_conn_t *shared = (qd_router_conn_t*) qd_link_get_conn_context(link);
    if (shared) {
        shared->ref_count++;
        return shared->mask_bit;
    }

    int mask_bit;
    if (qd_bitmask_first_set(router->neighbor_free_mask, &mask_bit)) {
        qd_bitmask_clear_bit(router->neighbor_free_mask, mask_bit);
    } else {
        qd_log(router->log_source, QD_LOG_CRITICAL, "Exceeded maximum inter-router link count");
        return -1;
    }

    shared = new_qd_router_conn_t();
    shared->ref_count = 1;
    shared->mask_bit  = mask_bit;
    qd_link_set_conn_context(link, shared);
    return mask_bit;
}


/**
 *
 */
static qd_address_t *router_lookup_terminus_LH(qd_router_t *router, const char *taddr)
{
    //
    // For now: Find the first instance of a '.' in the address and search for the text
    // up to and including this instance.
    //
    if (taddr == 0 || *taddr == '\0')
        return 0;

    const char *cursor = taddr;
    while (*cursor && *cursor != '.')
        cursor++;
    if (*cursor == '.')
        cursor++;
    int len = (int) (cursor - taddr);

    qd_field_iterator_t *iter = qd_field_iterator_binary(taddr, len, ITER_VIEW_ADDRESS_HASH);
    qd_field_iterator_override_prefix(iter, 'C');

    qd_address_t *addr;
    qd_hash_retrieve(router->addr_hash, iter, (void*) &addr);
    qd_field_iterator_free(iter);

    return addr;
}


/**
 * Outgoing Link Writable Handler
 */
static int router_writable_link_handler(void* context, qd_link_t *link)
{
    qd_router_t            *router = (qd_router_t*) context;
    qd_delivery_t          *delivery;
    qd_router_link_t       *rlink = (qd_router_link_t*) qd_link_get_context(link);
    pn_link_t              *pn_link = qd_link_pn(link);
    uint64_t                tag;
    int                     link_credit = pn_link_credit(pn_link);
    qd_routed_event_list_t  to_send;
    qd_routed_event_list_t  events;
    qd_routed_event_t      *re;
    size_t                  offer;
    int                     event_count = 0;
    bool                    drain_mode;
    bool                    drain_changed = qd_link_drain_changed(link, &drain_mode);

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
        delivery = qd_delivery(link, pn_dtag((char*) &tag, 8));

        //
        // Send the message
        //
        qd_message_send(re->message, link);

        //
        // Check the delivery associated with the queued message.  If it is not
        // settled, link it to the outgoing delivery for disposition/settlement
        // tracking.  If it is (pre-)settled, put it on the incoming link's event
        // queue to be locally settled.  This is done to hold session credit during
        // the time the message is in the outgoing message fifo.
        //
        sys_mutex_lock(router->lock);
        if (re->delivery) {
            if (qd_delivery_fifo_exit_LH(re->delivery)) {
                if (qd_delivery_settled(re->delivery)) {
                    qd_link_t         *peer_link  = qd_delivery_link(re->delivery);
                    qd_router_link_t  *peer_rlink = (qd_router_link_t*) qd_link_get_context(peer_link);
                    qd_routed_event_t *return_re  = new_qd_routed_event_t();
                    DEQ_ITEM_INIT(return_re);
                    return_re->delivery    = re->delivery;
                    return_re->message     = 0;
                    return_re->settle      = true;
                    return_re->disposition = 0;
                    qd_delivery_fifo_enter_LH(re->delivery);
                    DEQ_INSERT_TAIL(peer_rlink->event_fifo, return_re);
                    qd_link_activate(peer_link);
                } else
                    qd_delivery_link_peers_LH(re->delivery, delivery);
            }
        } else
            qd_delivery_free_LH(delivery, 0);  // settle and free
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
                pn_delivery_update(qd_delivery_pn(re->delivery), re->disposition);
                event_count++;
            }

            sys_mutex_lock(router->lock);

            bool ok = qd_delivery_fifo_exit_LH(re->delivery);
            if (ok && re->settle) {
                qd_delivery_unlink_LH(re->delivery);
                qd_delivery_free_LH(re->delivery, re->disposition);
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
                                                    const char        *to_override)
{
    qd_composed_field_t *out_ma       = qd_compose(QD_PERFORMATIVE_MESSAGE_ANNOTATIONS, 0);
    qd_field_iterator_t *ingress_iter = 0;

    qd_parsed_field_t *trace   = 0;
    qd_parsed_field_t *ingress = 0;
    qd_parsed_field_t *to      = 0;

    if (in_ma) {
        trace   = qd_parse_value_by_key(in_ma, QD_MA_TRACE);
        ingress = qd_parse_value_by_key(in_ma, QD_MA_INGRESS);
        to      = qd_parse_value_by_key(in_ma, QD_MA_TO);
    }

    qd_compose_start_map(out_ma);

    //
    // If there is a to_override provided, insert a TO field.
    //
    if (to_override) {
        qd_compose_insert_symbol(out_ma, QD_MA_TO);
        qd_compose_insert_string(out_ma, to_override);
    } else if (to) {
        qd_compose_insert_symbol(out_ma, QD_MA_TO);
        qd_compose_insert_string_iterator(out_ma, qd_parse_raw(to));
    }

    //
    // If there is a trace field, append this router's ID to the trace.
    //
    qd_compose_insert_symbol(out_ma, QD_MA_TRACE);
    qd_compose_start_list(out_ma);
    if (trace) {
        if (qd_parse_is_list(trace)) {
            uint32_t idx = 0;
            qd_parsed_field_t *trace_item = qd_parse_sub_value(trace, idx);
            while (trace_item) {
                qd_field_iterator_t *iter = qd_parse_raw(trace_item);
                if (qd_field_iterator_equal(iter, (unsigned char*) node_id))
                    *drop = 1;
                qd_field_iterator_reset(iter);
                qd_compose_insert_string_iterator(out_ma, iter);
                idx++;
                trace_item = qd_parse_sub_value(trace, idx);
            }
        }
    }

    qd_compose_insert_string(out_ma, node_id);
    qd_compose_end_list(out_ma);

    //
    // If there is no ingress field, annotate the ingress as this router else
    // keep the original field.
    //
    qd_compose_insert_symbol(out_ma, QD_MA_INGRESS);
    if (ingress && qd_parse_is_scalar(ingress)) {
        ingress_iter = qd_parse_raw(ingress);
        qd_compose_insert_string_iterator(out_ma, ingress_iter);
    } else
        qd_compose_insert_string(out_ma, node_id);

    qd_compose_end_map(out_ma);

    qd_message_set_message_annotations(msg, out_ma);
    qd_compose_free(out_ma);

    //
    // Return the iterator to the ingress field _if_ it was present.
    // If we added the ingress, return NULL.
    //
    return ingress_iter;
}


/**
 * Handle the link-routing case, where links are pre-paired and there is no per-message
 * routing needed.
 *
 * Note that this function does not issue a replacement credit for the received message.
 * In link-routes, the flow commands must be propagated end-to-end.  In other words, the
 * ultimate receiving endpoint will issue the replacement credits as it sees fit.
 *
 * Note also that this function does not perform any message validation.  For link-routing,
 * there is no need to look into the transferred message.
 */
static void router_link_route_delivery_LH(qd_router_link_t *peer_link, qd_delivery_t *delivery, qd_message_t *msg)
{
    qd_routed_event_t *re = new_qd_routed_event_t();

    DEQ_ITEM_INIT(re);
    re->delivery    = 0;
    re->message     = msg;
    re->settle      = false;
    re->disposition = 0;
    DEQ_INSERT_TAIL(peer_link->msg_fifo, re);

    //
    // Link the incoming delivery into the event for deferred processing
    //
    re->delivery = delivery;
    qd_delivery_fifo_enter_LH(delivery);

    qd_link_activate(peer_link->link);
}


static void router_forward_to_direct_subscribers_LH(qd_address_t *addr, qd_delivery_t *delivery, qd_message_t *msg, int *fanout)
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
            qd_delivery_fifo_enter_LH(delivery);
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


static void router_forward_to_remote_subscribers_LH(qd_router_t *router, qd_address_t *addr, qd_delivery_t *delivery,
                                                    qd_message_t *msg, int *fanout, qd_field_iterator_t *ingress_iter)
{
    //
    // Get the mask bit associated with the ingress router for the message.
    // This will be compared against the "valid_origin" masks for each
    // candidate destination router.
    //
    int origin = -1;
    if (ingress_iter && !(addr->semantics & QD_BYPASS_VALID_ORIGINS)) {
        qd_field_iterator_reset_view(ingress_iter, ITER_VIEW_NODE_HASH);
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
                    qd_delivery_fifo_enter_LH(delivery);
                }

                addr->deliveries_transit++;
                qd_link_activate(dest_link->link);
            }
        }

        qd_bitmask_free(link_set);
    }
}


/**
 * Inbound Delivery Handler
 */
static void router_rx_handler(void* context, qd_link_t *link, qd_delivery_t *delivery)
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
    msg = qd_message_receive(delivery);
    if (!msg)
        return;

    //
    // Consume the delivery.
    //
    pn_link_advance(pn_link);

    //
    // Handle the Link-Routing case.
    //
    sys_mutex_lock(router->lock);
    qd_router_link_t *clink = rlink->connected_link;
    if (clink) {
        router_link_route_delivery_LH(clink, delivery, msg);
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
    qd_message_t           *in_process_copy = 0;
    qd_router_message_cb_t  handler         = 0;
    void                   *handler_context = 0;

    valid_message = qd_message_check(msg, QD_DEPTH_PROPERTIES);

    if (valid_message) {
        qd_parsed_field_t   *in_ma     = 0;
        qd_field_iterator_t *iter      = 0;
        bool                 free_iter = true;
        qd_address_t        *addr;
        int                  fanout       = 0;
        char                *to_override  = 0;

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
            iter = qd_field_iterator_string(rlink->waypoint->address, ITER_VIEW_ADDRESS_HASH);
            qd_field_iterator_set_phase(iter, rlink->waypoint->out_phase);
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
            iter = qd_field_iterator_string(rlink->target, ITER_VIEW_ALL);
            to_override = rlink->target;
        }

        if (iter) {
            qd_field_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);

            //
            // Note: This function is going to need to be refactored so we can put an
            //       asynchronous address lookup here.  In the event there is a translation
            //       of the address (via namespace), it will have to be done here after
            //       obtaining the iterator and before doing the hash lookup.
            //
            //       Note that this lookup is only done for global/mobile class addresses.
            //

            qd_hash_retrieve(router->addr_hash, iter, (void*) &addr);
            qd_field_iterator_reset_view(iter, ITER_VIEW_NO_HOST);
            int is_local  = qd_field_iterator_prefix(iter, local_prefix);
            int is_direct = qd_field_iterator_prefix(iter, direct_prefix);
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
                qd_field_iterator_t *ingress_iter = router_annotate_message(router, in_ma, msg, &drop, to_override);

                //
                // Forward to the in-process handler for this address if there is one.  The
                // actual invocation of the handler will occur later after we've released
                // the lock.
                //
                if (!drop && addr->handler) {
                    in_process_copy = qd_message_copy(msg);
                    handler         = addr->handler;
                    handler_context = addr->handler_context;
                    addr->deliveries_to_container++;
                }

                //
                // If the address form is local (i.e. is prefixed by _local), don't forward
                // outside of the router process.
                //
                if (!drop && !is_local && router->router_mode != QD_ROUTER_MODE_ENDPOINT) {
                    //
                    // Handle the various fanout and bias cases:
                    //
                    if (QD_FANOUT(addr->semantics) == QD_FANOUT_MULTIPLE) {
                        //
                        // Forward to all of the local links receiving this address.
                        //
                        router_forward_to_direct_subscribers_LH(addr, delivery, msg, &fanout);

                        //
                        // If the address form is direct to this router node, don't relay it on
                        // to any other part of the network.
                        //
                        if (!is_direct)
                            router_forward_to_remote_subscribers_LH(router, addr, delivery, msg, &fanout, ingress_iter);

                    } else if (QD_FANOUT(addr->semantics) == QD_FANOUT_SINGLE) {
                        if (QD_BIAS(addr->semantics) == QD_BIAS_CLOSEST) {
                            //
                            // Bias is "closest".  First, try to find a directly connected consumer for the address.
                            // If there is none, then look for the closest remote consumer.
                            //
                            router_forward_to_direct_subscribers_LH(addr, delivery, msg, &fanout);
                            if (fanout == 0 && !is_direct)
                                router_forward_to_remote_subscribers_LH(router, addr, delivery, msg, &fanout, ingress_iter);

                        } else if (QD_BIAS(addr->semantics) == QD_BIAS_SPREAD) {
                            //
                            // Bias is "spread".  Alternate between looking first for a local consumer and looking
                            // first for a remote consumer.
                            //
                            addr->toggle = !addr->toggle;
                            if (addr->toggle) {
                                router_forward_to_direct_subscribers_LH(addr, delivery, msg, &fanout);
                                if (fanout == 0 && !is_direct)
                                    router_forward_to_remote_subscribers_LH(router, addr, delivery, msg, &fanout, ingress_iter);
                            } else {
                                if (!is_direct)
                                    router_forward_to_remote_subscribers_LH(router, addr, delivery, msg, &fanout, ingress_iter);
                                if (fanout == 0)
                                    router_forward_to_direct_subscribers_LH(addr, delivery, msg, &fanout);
                            }
                        }
                    }
                }
            }

            //
            // In message-routing mode, the handling of the incoming delivery depends on the
            // number of copies of the received message that were forwarded.
            //
            if (handler) {
                qd_delivery_free_LH(delivery, PN_ACCEPTED);
            } else if (fanout == 0) {
                qd_delivery_free_LH(delivery, PN_RELEASED);
            } else if (qd_delivery_settled(delivery)) {
                qd_delivery_free_LH(delivery, 0);
            }
        }
    } else {
        //
        // Message is invalid.  Reject the message.
        //
        qd_delivery_free_LH(delivery, PN_REJECTED);
    }

    sys_mutex_unlock(router->lock);
    qd_message_free(msg);

    //
    // Invoke the in-process handler now that the lock is released.
    //
    if (handler) {
        handler(handler_context, in_process_copy, rlink->mask_bit);
        qd_message_free(in_process_copy);
    }
}


/**
 * Delivery Disposition Handler
 */
static void router_disposition_handler(void* context, qd_link_t *link, qd_delivery_t *delivery)
{
    qd_router_t   *router  = (qd_router_t*) context;
    bool           changed = qd_delivery_disp_changed(delivery);
    uint64_t       disp    = qd_delivery_disp(delivery);
    bool           settled = qd_delivery_settled(delivery);

    sys_mutex_lock(router->lock);
    qd_delivery_t *peer = qd_delivery_peer(delivery);
    if (peer) {
        //
        // The case where this delivery has a peer.
        //
        if (changed || settled) {
            qd_link_t         *peer_link = qd_delivery_link(peer);
            qd_router_link_t  *prl       = (qd_router_link_t*) qd_link_get_context(peer_link);
            qd_routed_event_t *re        = new_qd_routed_event_t();
            DEQ_ITEM_INIT(re);
            re->delivery    = peer;
            re->message     = 0;
            re->settle      = settled;
            re->disposition = changed ? disp : 0;

            qd_delivery_fifo_enter_LH(peer);
            DEQ_INSERT_TAIL(prl->event_fifo, re);
            if (settled) {
                qd_delivery_unlink_LH(delivery);
                qd_delivery_free_LH(delivery, 0);
            }

            qd_link_activate(peer_link);
        }
    } else if (settled)
        qd_delivery_free_LH(delivery, 0);

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


static void qd_router_attach_routed_link(void *context, bool discard)
{
    link_attach_t *la = (link_attach_t*) context;

    if (!discard) {
        qd_link_t        *link  = qd_link(la->router->node, la->conn, la->dir, la->link_name);
        qd_router_link_t *rlink = new_qd_router_link_t();
        DEQ_ITEM_INIT(rlink);
        rlink->link_type      = QD_LINK_ENDPOINT;
        rlink->link_direction = la->dir;
        rlink->owning_addr    = 0;
        rlink->waypoint       = 0;
        rlink->link           = link;
        rlink->ref            = 0;
        rlink->target         = 0;
        DEQ_INIT(rlink->event_fifo);
        DEQ_INIT(rlink->msg_fifo);
        qd_link_set_context(link, rlink);

        sys_mutex_lock(la->router->lock);
        rlink->connected_link = la->peer_link;
        la->peer_link->connected_link = rlink;
        qd_entity_cache_add(QD_ROUTER_LINK_TYPE, rlink);
        DEQ_INSERT_TAIL(la->router->links, rlink);
        sys_mutex_unlock(la->router->lock);

        pn_terminus_copy(qd_link_source(link), qd_link_remote_source(la->peer_qd_link));
        pn_terminus_copy(qd_link_target(link), qd_link_remote_target(la->peer_qd_link));

        pn_link_open(qd_link_pn(link));
        if (la->credit > 0)
            pn_link_flow(qd_link_pn(link), la->credit);
    }

    if (la->link_name)
        free(la->link_name);
    free_link_attach_t(la);
}


static void qd_router_detach_routed_link(void *context, bool discard)
{
    link_detach_t *ld = (link_detach_t*) context;

    if (!discard) {
        qd_link_t *link = ld->rlink->link;

        if (ld->condition_name[0]) {
            pn_condition_t *cond = pn_link_condition(qd_link_pn(link));
            pn_condition_set_name(cond, ld->condition_name);
            pn_condition_set_description(cond, ld->condition_description);
            if (ld->condition_info)
                pn_data_copy(pn_condition_info(cond), ld->condition_info);
        }

        qd_link_close(link);

        sys_mutex_lock(ld->router->lock);
        qd_entity_cache_remove(QD_ROUTER_LINK_TYPE, ld->rlink);
        DEQ_REMOVE(ld->router->links, ld->rlink);
        sys_mutex_unlock(ld->router->lock);
    }

    if (ld->condition_info)
        pn_data_free(ld->condition_info);
    free_link_detach_t(ld);
}


static void qd_router_open_routed_link(void *context, bool discard)
{
    link_event_t *le = (link_event_t*) context;

    if (!discard) {
        qd_link_t *link = le->rlink->link;

        if (le->rlink->connected_link) {
            qd_link_t *peer = le->rlink->connected_link->link;
            pn_terminus_copy(qd_link_source(link), qd_link_remote_source(peer));
            pn_terminus_copy(qd_link_target(link), qd_link_remote_target(peer));
            pn_link_open(qd_link_pn(link));
        }
    }

    free_link_event_t(le);
}


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


link_attach_result_t qd_router_link_route_LH(qd_router_t      *router,
                                             qd_router_link_t *rlink,
                                             const char       *term_addr,
                                             qd_direction_t    dir)
{
    //
    // Lookup the target address to see if we can link-route this attach.
    //
    qd_address_t *addr = router_lookup_terminus_LH(router, term_addr);
    if (addr) {
        //
        // This is a link-attach routable target.  Propagate the attach downrange.
        // Check first for a locally connected container.
        //
        qd_link_t           *link    = rlink->link;
        pn_link_t           *pn_link = qd_link_pn(link);
        qd_router_lrp_ref_t *lrpref  = DEQ_HEAD(addr->lrps);
        if (lrpref) {
            qd_connection_t *conn = lrpref->lrp->container->conn;
            if (conn) {
                link_attach_t *la = new_link_attach_t();
                la->router       = router;
                la->peer_link    = rlink;
                la->peer_qd_link = link;
                la->link_name    = strdup(pn_link_name(pn_link));
                la->dir          = dir;
                la->conn         = conn;
                la->credit       = pn_link_credit(pn_link);
                qd_connection_invoke_deferred(conn, qd_router_attach_routed_link, la);
            }
        } else if (DEQ_SIZE(addr->rnodes) > 0) {
            //
            // There are no locally connected containers for this link but there is at
            // least one on a remote router.  Forward the attach toward the remote destination.
            //
            qd_router_node_t *remote_router = DEQ_HEAD(addr->rnodes)->router;
            qd_router_link_t *out_link      = 0;
            if (remote_router)
                out_link = remote_router->peer_link;
            if (!out_link && remote_router && remote_router->next_hop)
                out_link = remote_router->next_hop->peer_link;
            if (out_link) {
                qd_connection_t *out_conn = qd_link_connection(out_link->link);
                if (out_conn) {
                    link_attach_t *la = new_link_attach_t();
                    la->router       = router;
                    la->peer_link    = rlink;
                    la->peer_qd_link = link;
                    la->link_name    = strdup(pn_link_name(pn_link));
                    la->dir          = dir;
                    la->conn         = out_conn;
                    la->credit       = pn_link_credit(pn_link);
                    qd_connection_invoke_deferred(out_conn, qd_router_attach_routed_link, la);
                } else
                    return LINK_ATTACH_NO_PATH;
            } else
                return LINK_ATTACH_NO_PATH;
        } else
            return LINK_ATTACH_NO_PATH;
    } else
        return LINK_ATTACH_NO_MATCH;
    return LINK_ATTACH_FORWARDED;
}


/**
 * New Incoming Link Handler
 */
static int router_incoming_link_handler(void* context, qd_link_t *link)
{
    qd_router_t *router    = (qd_router_t*) context;
    pn_link_t   *pn_link   = qd_link_pn(link);
    int          is_router = qd_router_terminus_is_router(qd_link_remote_source(link));
    const char  *r_tgt     = pn_terminus_get_address(qd_link_remote_target(link));

    if (is_router && !qd_router_connection_is_inter_router(qd_link_connection(link))) {
        qd_log(router->log_source, QD_LOG_WARNING,
               "Incoming link claims router capability but is not on an inter-router connection");
        pn_link_close(pn_link);
        return 0;
    }

    qd_router_link_t *rlink = new_qd_router_link_t();
    DEQ_ITEM_INIT(rlink);
    rlink->link_type      = is_router ? QD_LINK_ROUTER : QD_LINK_ENDPOINT;
    rlink->link_direction = QD_INCOMING;
    rlink->owning_addr    = 0;
    rlink->waypoint       = 0;
    rlink->link           = link;
    rlink->connected_link = 0;
    rlink->ref            = 0;
    rlink->target         = 0;
    DEQ_INIT(rlink->event_fifo);
    DEQ_INIT(rlink->msg_fifo);

    if (!is_router && r_tgt) {
        rlink->target = (char*) malloc(strlen(r_tgt) + 1);
        strcpy(rlink->target, r_tgt);
    }

    qd_link_set_context(link, rlink);

    sys_mutex_lock(router->lock);
    rlink->mask_bit = is_router ? qd_router_find_mask_bit_LH(router, link) : 0;
    qd_entity_cache_add(QD_ROUTER_LINK_TYPE, rlink);
    DEQ_INSERT_TAIL(router->links, rlink);

    //
    // Attempt to link-route this attach
    //
    link_attach_result_t la_result = LINK_ATTACH_NO_MATCH;
    if (!is_router)
        la_result = qd_router_link_route_LH(router, rlink, r_tgt, QD_OUTGOING);
    sys_mutex_unlock(router->lock);

    pn_terminus_copy(qd_link_source(link), qd_link_remote_source(link));
    pn_terminus_copy(qd_link_target(link), qd_link_remote_target(link));

    switch (la_result) {
    case LINK_ATTACH_NO_MATCH:
        //
        // We didn't link-route this attach.  It terminates here.
        // Open it in the reverse direction.
        //
        pn_link_flow(pn_link, 1000);
        pn_link_open(pn_link);
        break;

    case LINK_ATTACH_NO_PATH: {
        //
        // The link should be routable but there is no path to the
        // destination.  Close the link.
        //
        pn_condition_t *cond = pn_link_condition(pn_link);
        pn_condition_set_name(cond, "qd:no-route-to-dest");
        pn_condition_set_description(cond, "No route to the destination node");
        pn_link_close(pn_link);
        break;
    }

    case LINK_ATTACH_FORWARDED:
        //
        // We routed the attach outbound.  Don't open the link back until
        // the downstream link is opened.
        //
        break;
    }

    return 0;
}


/**
 * New Outgoing Link Handler
 */
static int router_outgoing_link_handler(void* context, qd_link_t *link)
{
    qd_router_t *router  = (qd_router_t*) context;
    pn_link_t   *pn_link = qd_link_pn(link);
    const char  *r_src   = pn_terminus_get_address(qd_link_remote_source(link));
    int is_dynamic       = pn_terminus_is_dynamic(qd_link_remote_source(link));
    int is_router        = qd_router_terminus_is_router(qd_link_remote_target(link));
    int propagate        = 0;
    qd_field_iterator_t    *iter  = 0;
    char                    phase = '0';
    qd_address_semantics_t  semantics;
    qd_address_t           *addr = 0;
    link_attach_result_t    la_result = LINK_ATTACH_NO_MATCH;

    if (is_router && !qd_router_connection_is_inter_router(qd_link_connection(link))) {
        qd_log(router->log_source, QD_LOG_WARNING,
               "Outgoing link claims router capability but is not on an inter-router connection");
        pn_link_close(pn_link);
        return 0;
    }

    //
    // If this link is not a router link and it has no source address, we can't
    // accept it.
    //
    if (r_src == 0 && !is_router && !is_dynamic) {
        pn_link_close(pn_link);
        return 0;
    }

    //
    // If this is an endpoint link with a source address, make sure the address is
    // appropriate for endpoint links.  If it is not mobile address, it cannot be
    // bound to an endpoint link.
    //
    if (r_src && !is_router && !is_dynamic) {
        iter = qd_field_iterator_string(r_src, ITER_VIEW_ADDRESS_HASH);
        unsigned char prefix = qd_field_iterator_octet(iter);
        qd_field_iterator_reset(iter);

        if (prefix != 'M') {
            qd_field_iterator_free(iter);
            pn_link_close(pn_link);
            qd_log(router->log_source, QD_LOG_WARNING,
                   "Rejected an outgoing endpoint link with a router address: %s", r_src);
            return 0;
        }
    }

    //
    // Create a router_link record for this link.  Some of the fields will be
    // modified in the different cases below.
    //
    qd_router_link_t *rlink = new_qd_router_link_t();
    DEQ_ITEM_INIT(rlink);
    rlink->link_type      = is_router ? QD_LINK_ROUTER : QD_LINK_ENDPOINT;
    rlink->link_direction = QD_OUTGOING;
    rlink->owning_addr    = 0;
    rlink->waypoint       = 0;
    rlink->link           = link;
    rlink->connected_link = 0;
    rlink->ref            = 0;
    rlink->target         = 0;
    DEQ_INIT(rlink->event_fifo);
    DEQ_INIT(rlink->msg_fifo);

    qd_link_set_context(link, rlink);
    pn_terminus_copy(qd_link_source(link), qd_link_remote_source(link));
    pn_terminus_copy(qd_link_target(link), qd_link_remote_target(link));

    //
    // Determine the semantics for the address prior to taking out the lock.
    //
    if (is_dynamic || !iter)
        semantics = QD_FANOUT_SINGLE | QD_BIAS_CLOSEST | QD_CONGESTION_BACKPRESSURE;
    else {
        semantics = router_semantics_for_addr(router, iter, '\0', &phase);
        qd_field_iterator_set_phase(iter, phase);
        qd_field_iterator_reset_view(iter, ITER_VIEW_ADDRESS_HASH);
    }

    sys_mutex_lock(router->lock);
    rlink->mask_bit = is_router ? qd_router_find_mask_bit_LH(router, link) : 0;

    if (is_router) {
        //
        // If this is a router link, put it in the hello_address link-list.
        //
        qd_router_add_link_ref_LH(&router->hello_addr->rlinks, rlink);
        rlink->owning_addr = router->hello_addr;
        router->out_links_by_mask_bit[rlink->mask_bit] = rlink;

    } else {
        //
        // If this is an endpoint link, check the source.  If it is dynamic, we will
        // assign it an ephemeral and routable address.  If it has a non-dynamic
        // address, that address needs to be set up in the address list.
        //
        char temp_addr[1000]; // TODO: Use pn_string or aprintf.
        const char *link_route_address = qd_router_terminus_dnp_address(qd_link_remote_source(link));

        if (link_route_address == 0)
            link_route_address = r_src;
        la_result = qd_router_link_route_LH(router, rlink, link_route_address, QD_INCOMING);

        if (la_result == LINK_ATTACH_NO_MATCH) {
            if (is_dynamic) {
                qd_router_generate_temp_addr(router, temp_addr, 1000);
                iter = qd_field_iterator_string(temp_addr, ITER_VIEW_ADDRESS_HASH);
                pn_terminus_set_address(qd_link_source(link), temp_addr);
                qd_log(router->log_source, QD_LOG_INFO, "Assigned temporary routable address=%s", temp_addr);
            } else
                qd_log(router->log_source, QD_LOG_INFO, "Registered local address=%s phase=%c", r_src, phase);

            qd_hash_retrieve(router->addr_hash, iter, (void**) &addr);
            if (!addr) {
                addr = qd_address();
                qd_hash_insert(router->addr_hash, iter, addr, &addr->hash_handle);
                DEQ_INSERT_TAIL(router->addrs, addr);
                addr->semantics = semantics;
                qd_entity_cache_add(QD_ROUTER_ADDRESS_TYPE, addr);
            }

            rlink->owning_addr = addr;
            qd_router_add_link_ref_LH(&addr->rlinks, rlink);

            //
            // If this is not a dynamic address and it is the first local subscription
            // to the address, supply the address to the router module for propagation
            // to other nodes.
            //
            propagate = (!is_dynamic) && (DEQ_SIZE(addr->rlinks) == 1);
        }
    }
    qd_entity_cache_add(QD_ROUTER_LINK_TYPE, rlink);
    DEQ_INSERT_TAIL(router->links, rlink);

    //
    // If an interesting change has occurred with this address and it has an associated waypoint,
    // notify the waypoint module so it can react appropriately.
    //
    if (propagate && addr->waypoint)
        qd_waypoint_address_updated_LH(router->qd, addr);

    sys_mutex_unlock(router->lock);

    if (propagate)
        qd_router_mobile_added(router, iter);

    if (iter)
        qd_field_iterator_free(iter);

    switch (la_result) {
    case LINK_ATTACH_NO_MATCH:
        //
        // We didn't link-route this attach.  It terminates here.
        // Open it in the reverse direction.
        //
        pn_link_open(pn_link);
        break;

    case LINK_ATTACH_NO_PATH: {
        //
        // The link should be routable but there is no path to the
        // destination.  Close the link.
        //
        pn_condition_t *cond = pn_link_condition(qd_link_pn(link));
        pn_condition_set_name(cond, "qd:no-route-to-dest");
        pn_condition_set_description(cond, "No route to the destination node");
        pn_link_close(pn_link);
        break;
    }

    case LINK_ATTACH_FORWARDED:
        //
        // We routed the attach outbound.  Don't open the link back until
        // the downstream link is opened.
        //
        break;
    }

    return 0;
}


/**
 * Handler for remote opening of links that we initiated.
 */
static int router_link_attach_handler(void* context, qd_link_t *link)
{
    qd_router_t      *router = (qd_router_t*) context;
    qd_router_link_t *rlink  = (qd_router_link_t*) qd_link_get_context(link);

    sys_mutex_lock(router->lock);
    qd_router_link_t *peer_rlink = rlink->connected_link;
    if (peer_rlink) {
        qd_connection_t *out_conn = qd_link_connection(peer_rlink->link);
        if (out_conn) {
            link_event_t *le = new_link_event_t();
            memset(le, 0, sizeof(link_event_t));
            le->router = router;
            le->rlink  = peer_rlink;
            qd_connection_invoke_deferred(out_conn, qd_router_open_routed_link, le);
        }
    }
    sys_mutex_unlock(router->lock);
    
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
static int router_link_detach_handler(void* context, qd_link_t *link, int closed)
{
    qd_router_t      *router = (qd_router_t*) context;
    qd_router_link_t *rlink  = (qd_router_link_t*) qd_link_get_context(link);
    qd_router_conn_t *shared = (qd_router_conn_t*) qd_link_get_conn_context(link);
    qd_address_t     *oaddr  = 0;
    int               lost_link_mask_bit = -1;

    if (!rlink)
        return 0;

    sys_mutex_lock(router->lock);

    if (rlink->connected_link) {
        qd_connection_t *out_conn = qd_link_connection(rlink->connected_link->link);
        if (out_conn) {
            link_detach_t *ld = new_link_detach_t();
            memset(ld, 0, sizeof(link_detach_t));
            ld->router = router;
            ld->rlink  = rlink->connected_link;
            pn_condition_t *cond = pn_link_remote_condition(qd_link_pn(link));
            if (pn_condition_is_set(cond)) {
                if (pn_condition_get_name(cond)) {
                    strncpy(ld->condition_name, pn_condition_get_name(cond), COND_NAME_LEN);
                    ld->condition_name[COND_NAME_LEN] = '\0';
                }
                if (pn_condition_get_description(cond)) {
                    strncpy(ld->condition_description, pn_condition_get_description(cond), COND_DESCRIPTION_LEN);
                    ld->condition_description[COND_DESCRIPTION_LEN] = '\0';
                }
                if (pn_condition_info(cond)) {
                    ld->condition_info = pn_data(0);
                    pn_data_copy(ld->condition_info, pn_condition_info(cond));
                }
            } else if (!closed) {
                strcpy(ld->condition_name, "qd:routed-link-lost");
                strcpy(ld->condition_description, "Connectivity to the peer container was lost");
            }
            rlink->connected_link->connected_link = 0;
            qd_connection_invoke_deferred(out_conn, qd_router_detach_routed_link, ld);
        }
    }

    //
    // If this link is part of an inter-router connection, drop the
    // reference count.  If this is the last link on the connection,
    // free the mask-bit and the shared connection record.
    //
    if (shared) {
        shared->ref_count--;
        if (shared->ref_count == 0) {
            lost_link_mask_bit = rlink->mask_bit;
            qd_bitmask_set_bit(router->neighbor_free_mask, rlink->mask_bit);
            qd_link_set_conn_context(link, 0);
            free_qd_router_conn_t(shared);
        }
    }

    //
    // If the link is outgoing, we must disassociate it from its address.
    //
    if (rlink->link_direction == QD_OUTGOING && rlink->owning_addr) {
        qd_router_del_link_ref_LH(&rlink->owning_addr->rlinks, rlink);
        oaddr = rlink->owning_addr;
    }

    //
    // If this is an outgoing inter-router link, we must remove the by-mask-bit
    // index reference to this link.
    //
    if (rlink->link_type == QD_LINK_ROUTER && rlink->link_direction == QD_OUTGOING) {
        if (router->out_links_by_mask_bit[rlink->mask_bit] == rlink)
            router->out_links_by_mask_bit[rlink->mask_bit] = 0;
        else
            qd_log(router->log_source, QD_LOG_CRITICAL,
                   "Outgoing router link closing but not in index: bit=%d", rlink->mask_bit);
    }

    //
    // Remove the link from the master list-of-links.
    //
    DEQ_REMOVE(router->links, rlink);
    qd_entity_cache_remove(QD_ROUTER_LINK_TYPE, rlink);
    sys_mutex_unlock(router->lock);

    //
    // Check to see if the owning address should be deleted
    //
    qd_router_check_addr(router, oaddr, 1);

    if (rlink->target)
        free(rlink->target);
    free_qd_router_link_t(rlink);

    //
    // If we lost the link to a neighbor router, notify the route engine so it doesn't
    // have to wait for the HELLO timeout to expire.
    //
    if (lost_link_mask_bit >= 0)
        qd_router_link_lost(router, lost_link_mask_bit);

    return 0;
}


static void router_inbound_open_handler(void *type_context, qd_connection_t *conn, void *context)
{
}


static void router_outbound_open_handler(void *type_context, qd_connection_t *conn, void *context)
{
    qd_router_t *router = (qd_router_t*) type_context;

    //
    // If the connection is on-demand, visit all waypoints that are waiting for their
    // connection to arrive.
    //
    if (qd_router_connection_is_on_demand(conn)) {
        qd_waypoint_connection_opened(router->qd, (qd_config_connector_t*) context, conn);
        return;
    }

    //
    // If the connection isn't inter-router, ignore it.
    //
    if (!qd_router_connection_is_inter_router(conn))
        return;

    qd_link_t        *sender;
    qd_link_t        *receiver;
    qd_router_link_t *rlink;
    int               mask_bit = 0;
    size_t            clen     = strlen(QD_CAPABILITY_ROUTER);

    //
    // Allocate a mask bit to designate the pair of links connected to the neighbor router
    //
    sys_mutex_lock(router->lock);
    if (qd_bitmask_first_set(router->neighbor_free_mask, &mask_bit)) {
        qd_bitmask_clear_bit(router->neighbor_free_mask, mask_bit);
    } else {
        sys_mutex_unlock(router->lock);
        qd_log(router->log_source, QD_LOG_CRITICAL, "Exceeded maximum inter-router link count");
        return;
    }

    //
    // Create an incoming link with router source capability
    //
    receiver = qd_link(router->node, conn, QD_INCOMING, QD_INTERNODE_LINK_NAME_1);
    // TODO - We don't want to have to cast away the constness of the literal string here!
    //        See PROTON-429
    pn_data_put_symbol(pn_terminus_capabilities(qd_link_target(receiver)),
                       pn_bytes(clen, (char*) QD_CAPABILITY_ROUTER));

    rlink = new_qd_router_link_t();
    DEQ_ITEM_INIT(rlink);
    rlink->mask_bit       = mask_bit;
    rlink->link_type      = QD_LINK_ROUTER;
    rlink->link_direction = QD_INCOMING;
    rlink->owning_addr    = 0;
    rlink->waypoint       = 0;
    rlink->link           = receiver;
    rlink->connected_link = 0;
    rlink->ref            = 0;
    rlink->target         = 0;
    DEQ_INIT(rlink->event_fifo);
    DEQ_INIT(rlink->msg_fifo);

    qd_link_set_context(receiver, rlink);
    qd_entity_cache_add(QD_ROUTER_LINK_TYPE, rlink);
    DEQ_INSERT_TAIL(router->links, rlink);

    //
    // Create an outgoing link with router target capability
    //
    sender = qd_link(router->node, conn, QD_OUTGOING, QD_INTERNODE_LINK_NAME_2);
    // TODO - We don't want to have to cast away the constness of the literal string here!
    //        See PROTON-429
    pn_data_put_symbol(pn_terminus_capabilities(qd_link_source(sender)),
                       pn_bytes(clen, (char *) QD_CAPABILITY_ROUTER));

    rlink = new_qd_router_link_t();
    DEQ_ITEM_INIT(rlink);
    rlink->mask_bit       = mask_bit;
    rlink->link_type      = QD_LINK_ROUTER;
    rlink->link_direction = QD_OUTGOING;
    rlink->owning_addr    = router->hello_addr;
    rlink->waypoint       = 0;
    rlink->link           = sender;
    rlink->connected_link = 0;
    rlink->ref            = 0;
    rlink->target         = 0;
    DEQ_INIT(rlink->event_fifo);
    DEQ_INIT(rlink->msg_fifo);

    //
    // Add the new outgoing link to the hello_address's list of links.
    //
    qd_router_add_link_ref_LH(&router->hello_addr->rlinks, rlink);

    //
    // Index this link from the by-maskbit index so we can later find it quickly
    // when provided with the mask bit.
    //
    router->out_links_by_mask_bit[mask_bit] = rlink;

    qd_link_set_context(sender, rlink);
    qd_entity_cache_add(QD_ROUTER_LINK_TYPE, rlink);
    DEQ_INSERT_TAIL(router->links, rlink);
    sys_mutex_unlock(router->lock);

    pn_link_open(qd_link_pn(receiver));
    pn_link_open(qd_link_pn(sender));
    pn_link_flow(qd_link_pn(receiver), 1000);
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
                                     router_writable_link_handler,
                                     router_link_detach_handler,
                                     router_link_attach_handler,
                                     router_link_flow_handler,
                                     0,   // node_created_handler
                                     0,   // node_destroyed_handler
                                     router_inbound_open_handler,
                                     router_outbound_open_handler };
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
    // Create addresses for all of the routers in the topology.  It will be registered
    // locally later in the initialization sequence.
    //
    if (router->router_mode == QD_ROUTER_MODE_INTERIOR) {
        router->router_addr   = qd_router_register_address(qd, "qdrouter", 0, QD_SEMANTICS_ROUTER_CONTROL, false, 0);
        router->routerma_addr = qd_router_register_address(qd, "qdrouter.ma", 0, QD_SEMANTICS_DEFAULT, false, 0);
        router->hello_addr    = qd_router_register_address(qd, "qdhello", 0, QD_SEMANTICS_ROUTER_CONTROL, false, 0);
    }

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

void qd_router_setup_late(qd_dispatch_t *qd)
{
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


qd_address_t *qd_router_register_address(qd_dispatch_t          *qd,
                                         const char             *address,
                                         qd_router_message_cb_t  handler,
                                         qd_address_semantics_t  semantics,
                                         bool                    global,
                                         void                   *context)
{
    char                 addr_string[1000];
    qd_router_t         *router = qd->router;
    qd_address_t        *addr = 0;
    qd_field_iterator_t *iter = 0;

    snprintf(addr_string, sizeof(addr_string), "%s%s", global ? "M0" : "L", address);
    iter = qd_field_iterator_string(addr_string, ITER_VIEW_NO_HOST);

    sys_mutex_lock(router->lock);
    qd_hash_retrieve(router->addr_hash, iter, (void**) &addr);
    if (!addr) {
        addr = qd_address();
        addr->semantics = semantics;
        qd_hash_insert(router->addr_hash, iter, addr, &addr->hash_handle);
        DEQ_ITEM_INIT(addr);
        DEQ_INSERT_TAIL(router->addrs, addr);
        qd_entity_cache_add(QD_ROUTER_ADDRESS_TYPE, addr);
    }
    qd_field_iterator_free(iter);

    addr->handler         = handler;
    addr->handler_context = context;

    sys_mutex_unlock(router->lock);

    if (handler)
        qd_log(router->log_source, QD_LOG_INFO, "In-Process Address Registered: %s", address);
    assert(addr);
    return addr;
}


void qd_router_unregister_address(qd_address_t *ad)
{
    //free_qd_address_t(ad);
}


void qd_address_set_redirect(qd_address_t *address, qd_address_t *redirect)
{
    address->redirect = redirect;
}


void qd_address_set_static_cc(qd_address_t *address, qd_address_t *cc)
{
    address->static_cc = cc;
}


void qd_address_set_dynamic_cc(qd_address_t *address, qd_address_t *cc)
{
    address->dynamic_cc = cc;
}


void qd_router_send(qd_dispatch_t       *qd,
                    qd_field_iterator_t *address,
                    qd_message_t        *msg)
{
    qd_router_t  *router = qd->router;
    qd_address_t *addr;

    qd_field_iterator_reset_view(address, ITER_VIEW_ADDRESS_HASH);
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
    qd_field_iterator_t *iter = qd_field_iterator_string(address, ITER_VIEW_ADDRESS_HASH);
    qd_router_send(qd, iter, msg);
    qd_field_iterator_free(iter);
}
