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

#include "core_events.h"
#include "module.h"
#include "route_control.h"
#include "router_core_private.h"

#include "qpid/dispatch/compose.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/log.h"
#include "qpid/dispatch/message.h"
#include "qpid/dispatch/router.h"

#include <inttypes.h>
#include <stdio.h>

#define PROTOCOL_VERSION 1
static const char *OPCODE     = "opcode";
static const char *MAR        = "MAR";
static const char *MAU        = "MAU";
static const char *ID         = "id";
static const char *PV         = "pv";
static const char *AREA       = "area";
static const char *MOBILE_SEQ = "mobile_seq";
static const char *HINTS      = "hints";
static const char *ADD        = "add";
static const char *DEL        = "del";
static const char *EXIST      = "exist";
static const char *HAVE_SEQ   = "have_seq";

//
// qdr_address_t.sync_mask bit values
//
#define ADDR_SYNC_ADDRESS_IN_ADD_LIST     0x00000001
#define ADDR_SYNC_ADDRESS_IN_DEL_LIST     0x00000002
#define ADDR_SYNC_ADDRESS_TO_BE_DELETED   0x00000004
#define ADDR_SYNC_ADDRESS_MOBILE_TRACKING 0x00000008

//
// qdr_node_t.sync_mask bit values
//
#define ADDR_SYNC_ROUTER_MA_REQUESTED     0x00000001
#define ADDR_SYNC_ROUTER_VERSION_LOGGED   0x00000002

#define BIT_SET(M,B)   M |= B
#define BIT_CLEAR(M,B) M &= ~B
#define BIT_IS_SET(M,B) (M & B)

typedef struct {
    qdr_core_t                *core;
    qdrc_event_subscription_t *event_sub;
    qdr_core_timer_t          *timer;
    qdr_subscription_t        *message_sub1;
    qdr_subscription_t        *message_sub2;
    qd_log_source_t           *log;
    uint64_t                   mobile_seq;
    qdr_address_list_t         added_addrs;
    qdr_address_list_t         deleted_addrs;
} qdrm_mobile_sync_t;

/**
 * Gets the router id from the parsed field and prints the relevant error message.
 */
static void log_unknown_router(qdrm_mobile_sync_t *msync, qd_parsed_field_t *id_field, const char *opcode)
{
    char *r_id = 0;
    if (id_field) {
        qd_iterator_t *id_iter = qd_parse_raw(id_field);
        if (id_iter) {
            r_id = (char*) qd_iterator_copy(id_iter);
        }
    }
    //
    // There is a possibility here that router_id is null but that is fine. We want to print it out either way
    // which will help us in debugging.
    //
    qd_log(msync->log, QD_LOG_WARNING, "Received %s from an unknown router with router id %s", opcode, r_id);
    free(r_id);
}

static void qcm_mobile_sync_on_router_advanced_CT(qdrm_mobile_sync_t *msync, qdr_node_t *router);

//================================================================================
// Helper Functions
//================================================================================

static qd_address_treatment_t qcm_mobile_sync_default_treatment(qdr_core_t *core, int hint) {
    switch (hint) {
    case QD_TREATMENT_MULTICAST_FLOOD:
        return QD_TREATMENT_MULTICAST_FLOOD;
    case QD_TREATMENT_MULTICAST_ONCE:
        return QD_TREATMENT_MULTICAST_ONCE;
    case QD_TREATMENT_ANYCAST_CLOSEST:
        return QD_TREATMENT_ANYCAST_CLOSEST;
    case QD_TREATMENT_ANYCAST_BALANCED:
        return QD_TREATMENT_ANYCAST_BALANCED;
    case QD_TREATMENT_LINK_BALANCED:
        return QD_TREATMENT_LINK_BALANCED;
    case QD_TREATMENT_UNAVAILABLE:
        return QD_TREATMENT_UNAVAILABLE;
    default:
        return core->qd->default_treatment == QD_TREATMENT_UNAVAILABLE ? QD_TREATMENT_ANYCAST_BALANCED : core->qd->default_treatment;
    }
}


static bool qcm_mobile_sync_addr_is_mobile(qdr_address_t *addr)
{
    const char *hash_key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
    return !!strchr("MCDEFH", hash_key[0]);
}


qdr_node_t *qdc_mobile_sync_router_by_id(qdrm_mobile_sync_t *msync, qd_parsed_field_t *id_field)
{
    if (!id_field)
        return 0;

    qd_iterator_t *id_iter = qd_parse_raw(id_field);
    qdr_node_t *router = DEQ_HEAD(msync->core->routers);
    while (!!router) {
        if (qd_iterator_equal(id_iter, qd_hash_key_by_handle(router->owning_addr->hash_handle) + 1))
            return router;
        router = DEQ_NEXT(router);
    }

    return 0;
}


/**
 * Bump the ref_count on the address to ensure it is not deleted out from under our attention.
 */
static void qcm_mobile_sync_start_tracking(qdr_address_t *addr)
{
    BIT_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_MOBILE_TRACKING);
    addr->ref_count++;
}


/**
 * Decrement the address's ref_count.
 * Check the address to have it deleted if it is no longer referenced anywhere.
 */
static void qcm_mobile_sync_stop_tracking(qdr_core_t *core, qdr_address_t *addr)
{
    BIT_CLEAR(addr->sync_mask, ADDR_SYNC_ADDRESS_MOBILE_TRACKING);
    if (--addr->ref_count == 0)
        qdr_check_addr_CT(core, addr);
}


static qd_composed_field_t *qcm_mobile_sync_message_headers(const char *address, const char *opcode)
{
    qd_composed_field_t *field = qd_compose(QD_PERFORMATIVE_HEADER, 0);
    qd_compose_start_list(field);
    qd_compose_insert_bool(field, 0); // durable
    qd_compose_end_list(field);

    field = qd_compose(QD_PERFORMATIVE_PROPERTIES, field);
    qd_compose_start_list(field);
    qd_compose_insert_null(field);            // message-id
    qd_compose_insert_null(field);            // user-id
    qd_compose_insert_string(field, address); // to
    qd_compose_end_list(field);

    field = qd_compose(QD_PERFORMATIVE_APPLICATION_PROPERTIES, field);
    qd_compose_start_map(field);
    qd_compose_insert_symbol(field, OPCODE);
    qd_compose_insert_string(field, opcode);
    qd_compose_end_map(field);

    return field;
}


static void qcm_mobile_sync_compose_diff_addr_list(qdrm_mobile_sync_t *msync, qd_composed_field_t *field, bool is_added)
{
    qdr_address_list_t *list = is_added ? &msync->added_addrs : &msync->deleted_addrs;

    qd_compose_start_list(field);
    qdr_address_t *addr = DEQ_HEAD(*list);
    while (addr) {
        const char *hash_key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
        qd_compose_insert_string(field, hash_key);
        if (is_added) {
            DEQ_REMOVE_HEAD_N(SYNC_ADD, *list);
            BIT_CLEAR(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_ADD_LIST);
        } else {
            DEQ_REMOVE_HEAD_N(SYNC_DEL, *list);
            BIT_CLEAR(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_DEL_LIST);
            qcm_mobile_sync_stop_tracking(msync->core, addr);
        }
        addr = DEQ_HEAD(*list);
    }
    qd_compose_end_list(field);
}


static void qcm_mobile_sync_compose_diff_hint_list(qdrm_mobile_sync_t *msync, qd_composed_field_t *field)
{
    qd_compose_start_list(field);
    qdr_address_t *addr = DEQ_HEAD(msync->added_addrs);
    while (addr) {
        qd_compose_insert_int(field, addr->treatment);
        addr = DEQ_NEXT_N(SYNC_ADD, addr);
    }
    qd_compose_end_list(field);
}


static qd_message_t *qcm_mobile_sync_compose_differential_mau(qdrm_mobile_sync_t *msync, const char *address)
{
    qd_composed_field_t *headers = qcm_mobile_sync_message_headers(address, MAU);
    qd_composed_field_t *body    = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    //
    // Generate the message body
    //
    qd_compose_start_map(body);
    qd_compose_insert_symbol(body, ID);
    qd_compose_insert_string(body, msync->core->router_id);

    qd_compose_insert_symbol(body, PV);
    qd_compose_insert_long(body, PROTOCOL_VERSION);

    qd_compose_insert_symbol(body, AREA);
    qd_compose_insert_string(body, msync->core->router_area);

    qd_compose_insert_symbol(body, MOBILE_SEQ);
    qd_compose_insert_long(body, msync->mobile_seq);

    qd_compose_insert_symbol(body, HINTS);
    qcm_mobile_sync_compose_diff_hint_list(msync, body);

    qd_compose_insert_symbol(body, ADD);
    qcm_mobile_sync_compose_diff_addr_list(msync, body, true);

    qd_compose_insert_symbol(body, DEL);
    qcm_mobile_sync_compose_diff_addr_list(msync, body, false);

    qd_compose_end_map(body);

    qd_message_t *msg = qd_message_compose(headers, body, 0, true);

    return msg;
}


static qd_message_t *qcm_mobile_sync_compose_absolute_mau(qdrm_mobile_sync_t *msync, const char *address)
{
    qd_composed_field_t *headers = qcm_mobile_sync_message_headers(address, MAU);
    qd_composed_field_t *body    = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    qd_compose_start_map(body);
    qd_compose_insert_symbol(body, ID);
    qd_compose_insert_string(body, msync->core->router_id);

    qd_compose_insert_symbol(body, PV);
    qd_compose_insert_long(body, PROTOCOL_VERSION);

    qd_compose_insert_symbol(body, AREA);
    qd_compose_insert_string(body, msync->core->router_area);

    qd_compose_insert_symbol(body, MOBILE_SEQ);
    qd_compose_insert_long(body, msync->mobile_seq);

    qd_compose_insert_symbol(body, EXIST);
    qd_compose_start_list(body);
    qdr_address_t *addr = DEQ_HEAD(msync->core->addrs);
    while (!!addr) {
        //
        // For an address to be included in the list, it must:
        //   - be a mobile address
        //   - have at least one local consumer, link-route destination, or exchange
        //     _OR_ be in the delete list (because the peers haven't heard of its pending deletion)
        //   - not be in the add list (because the peers haven't heard of its pending addition)
        //
        // Note that in the two add/del list cases, we are reporting information that is not currently
        // accurate.  In these cases, a differentiao MAU will be sent very shortly that will put the
        // peer router in the correct state.
        //
        if (qcm_mobile_sync_addr_is_mobile(addr)
            && ((DEQ_SIZE(addr->rlinks) > 0 || DEQ_SIZE(addr->conns) > 0 || !!addr->exchange)
                || BIT_IS_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_DEL_LIST))
            && !BIT_IS_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_ADD_LIST)) {
            const char *hash_key = (const char*) qd_hash_key_by_handle(addr->hash_handle);
            qd_compose_insert_string(body, hash_key);
        }
        addr = DEQ_NEXT(addr);
    }
    qd_compose_end_list(body);

    qd_compose_insert_symbol(body, HINTS);
    qd_compose_start_list(body);
    addr = DEQ_HEAD(msync->core->addrs);
    while (!!addr) {
        //
        // This loop uses the same logic as above.
        //
        if (qcm_mobile_sync_addr_is_mobile(addr)
            && ((DEQ_SIZE(addr->rlinks) > 0 || DEQ_SIZE(addr->conns) > 0 || !!addr->exchange)
                || BIT_IS_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_DEL_LIST))
            && !BIT_IS_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_ADD_LIST))
            qd_compose_insert_int(body, addr->treatment);
        addr = DEQ_NEXT(addr);
    }
    qd_compose_end_list(body);
    qd_compose_end_map(body);

    return qd_message_compose(headers, body, 0, true);
}


static qd_message_t *qcm_mobile_sync_compose_mar(qdrm_mobile_sync_t *msync, qdr_node_t *router)
{
    qd_composed_field_t *headers = qcm_mobile_sync_message_headers(router->wire_address_ma, MAR);
    qd_composed_field_t *body    = qd_compose(QD_PERFORMATIVE_BODY_AMQP_VALUE, 0);

    qd_compose_start_map(body);
    qd_compose_insert_symbol(body, ID);
    qd_compose_insert_string(body, msync->core->router_id);

    qd_compose_insert_symbol(body, PV);
    qd_compose_insert_long(body, PROTOCOL_VERSION);

    qd_compose_insert_symbol(body, AREA);
    qd_compose_insert_string(body, msync->core->router_area);

    qd_compose_insert_symbol(body, HAVE_SEQ);
    qd_compose_insert_long(body, router->mobile_seq);

    qd_compose_end_map(body);

    return qd_message_compose(headers, body, 0, true);
}


//================================================================================
// Timer Handler
//================================================================================

static void qcm_mobile_sync_on_timer_CT(qdr_core_t *core, void *context)
{
    qdrm_mobile_sync_t *msync = (qdrm_mobile_sync_t*) context;

    //
    // Re-schedule the timer for the next go-around
    //
    qdr_core_timer_schedule_CT(core, msync->timer, 0);

    //
    // Check the add and delete lists.  If they are empty, nothing of note occured in the last
    // interval.  Exit the handler function.
    //
    size_t added_count   = DEQ_SIZE(msync->added_addrs);
    size_t deleted_count = DEQ_SIZE(msync->deleted_addrs);

    if (added_count == 0 && deleted_count == 0)
        return;

    //
    // Bump the mobile sequence number.
    //
    msync->mobile_seq++;

    //
    // Prepare a differential MAU for sending to all the other routers.
    //
    qd_message_t *mau = qcm_mobile_sync_compose_differential_mau(msync, "_topo/0/all/qdrouter.ma");

    //
    // Multicast the control message.  Set the exclude_inprocess and control flags.
    // Use the TOPOLOGICAL class address for sending.
    //
    int fanout = qdr_forward_message_CT(core, core->routerma_addr_T, mau, 0, true, true);
    qd_message_free(mau);

    //
    // Post the updated mobile sequence number to the Python router.  It is important that this be
    // done _after_ sending the differential MAU to prevent a storm of un-needed MAR requests from
    // the other routers.
    //
    qdr_post_set_my_mobile_seq_CT(core, msync->mobile_seq);

    //
    // Trace log the activity of this sequence update.
    //
    qd_log(msync->log, QD_LOG_DEBUG, "New mobile sequence: mobile_seq=%"PRIu64", addrs_added=%ld, addrs_deleted=%ld, fanout=%d",
           msync->mobile_seq, added_count, deleted_count, fanout);
}


//================================================================================
// Message Handler
//================================================================================

static void qcm_mobile_sync_on_mar_CT(qdrm_mobile_sync_t *msync, qd_parsed_field_t *body)
{
    if (!!body && qd_parse_is_map(body)) {
        qd_parsed_field_t *id_field       = qd_parse_value_by_key(body, ID);
        qd_parsed_field_t *have_seq_field = qd_parse_value_by_key(body, HAVE_SEQ);

        if (!id_field || !have_seq_field)
            return;

        uint64_t           have_seq       = qd_parse_as_ulong(have_seq_field);
        qd_parsed_field_t *version_field  = qd_parse_value_by_key(body, PV);
        uint32_t           version        = version_field ? qd_parse_as_uint(version_field) : 0;

        qdr_node_t *router = qdc_mobile_sync_router_by_id(msync, id_field);
        if (!!router) {
            if (version > PROTOCOL_VERSION) {
                if (!BIT_IS_SET(router->sync_mask, ADDR_SYNC_ROUTER_VERSION_LOGGED)) {
                    BIT_SET(router->sync_mask, ADDR_SYNC_ROUTER_VERSION_LOGGED);
                    qd_log(msync->log, QD_LOG_WARNING, "Received MAR at protocol version %"PRIu32" from %s.  Ignoring.",
                           version, (const char*) qd_hash_key_by_handle(router->owning_addr->hash_handle) + 1);
                }
                return;
            }

            qd_log(msync->log, QD_LOG_DEBUG, "Received MAR from %s, have_seq=%"PRIu64,
                   (const char*) qd_hash_key_by_handle(router->owning_addr->hash_handle) + 1, have_seq);

            if (have_seq < msync->mobile_seq) {
                //
                // The requestor's view of our mobile_seq is less than our actual mobile_sync.
                // Send them an absolute MAU to get them caught up to the present.
                //
                qd_message_t *mau = qcm_mobile_sync_compose_absolute_mau(msync, router->wire_address_ma);
                (void) qdr_forward_message_CT(msync->core, router->owning_addr, mau, 0, true, true);
                qd_message_free(mau);

                //
                // Trace log the activity of this sequence update.
                //
                qd_log(msync->log, QD_LOG_DEBUG, "Sent MAU to requestor: mobile_seq=%"PRIu64, msync->mobile_seq);
            }
        } else {
            log_unknown_router(msync, id_field, "MAR");
        }
    }
}


static void qcm_mobile_sync_on_mau_CT(qdrm_mobile_sync_t *msync, qd_parsed_field_t *body)
{
    if (!!body && qd_parse_is_map(body)) {
        qd_parsed_field_t *id_field         = qd_parse_value_by_key(body, ID);
        qd_parsed_field_t *mobile_seq_field = qd_parse_value_by_key(body, MOBILE_SEQ);

        if (!id_field || !mobile_seq_field)
            return;

        uint64_t           mobile_seq       = qd_parse_as_ulong(mobile_seq_field);
        qd_parsed_field_t *version_field    = qd_parse_value_by_key(body, PV);
        uint32_t           version          = version_field ? qd_parse_as_uint(version_field) : 0;

        qdr_node_t *router = qdc_mobile_sync_router_by_id(msync, id_field);
        if (!!router) {
            const char *router_id = (const char*) qd_hash_key_by_handle(router->owning_addr->hash_handle) + 1;

            if (version > PROTOCOL_VERSION) {
                if (!BIT_IS_SET(router->sync_mask, ADDR_SYNC_ROUTER_VERSION_LOGGED)) {
                    BIT_SET(router->sync_mask, ADDR_SYNC_ROUTER_VERSION_LOGGED);
                    qd_log(msync->log, QD_LOG_WARNING, "Received MAU at protocol version %"PRIu32" from %s.  Ignoring.",
                           version, router_id);
                }
                return;
            }

            qd_parsed_field_t *add_field   = qd_parse_value_by_key(body, ADD);
            qd_parsed_field_t *del_field   = qd_parse_value_by_key(body, DEL);
            qd_parsed_field_t *exist_field = qd_parse_value_by_key(body, EXIST);
            qd_parsed_field_t *hints_field = qd_parse_value_by_key(body, HINTS);
            uint32_t           hints_count = 0;
            qdr_address_t     *addr;

            //
            // Validate the fields and determine what kind of MAU we've received.
            //
            if (!!hints_field && qd_parse_is_list(hints_field))
                hints_count = qd_parse_sub_count(hints_field);

            //
            // Validate the exist, add, and del fields.  They must, if they exist, be lists.
            // If there is an exist field, there must not be an add or del field.
            // If there is no exist field, there must be both an add and a del field.
            //
            if ((!!exist_field && !qd_parse_is_list(exist_field))
                || (!!add_field && !qd_parse_is_list(add_field))
                || (!!del_field && !qd_parse_is_list(del_field))
                || (!!exist_field && (!!add_field || !!del_field))
                || (!exist_field && (!add_field || !del_field))) {
                qd_log(msync->log, QD_LOG_ERROR, "Received malformed MAU from %s", router_id);
                return;
            }

            //
            // If this is a differential MAU and it doesn't represent the next expected
            // update, treat this like a sequence-advance and send a MAR
            //
            if (!exist_field && router->mobile_seq != mobile_seq - 1) {
                if (!BIT_IS_SET(router->sync_mask, ADDR_SYNC_ROUTER_MA_REQUESTED)) {
                    qcm_mobile_sync_on_router_advanced_CT(msync, router);
                    BIT_SET(router->sync_mask, ADDR_SYNC_ROUTER_MA_REQUESTED);
                }
                return;
            }

            //
            // Record the new mobile sequence for the remote router.
            //
            BIT_CLEAR(router->sync_mask, ADDR_SYNC_ROUTER_MA_REQUESTED);
            router->mobile_seq = mobile_seq;

            //
            // Check the exist/add list size against the hints-list size.  If they are not
            // exactly equal, ignore the hints.
            //
            if (!!exist_field) {
                if (hints_count != qd_parse_sub_count(exist_field))
                    hints_count = 0;
            } else {
                if (hints_count != qd_parse_sub_count(add_field))
                    hints_count = 0;
            }

            qd_log(msync->log, QD_LOG_DEBUG, "Received MAU (%s) from %s, mobile_seq=%"PRIu64,
                   !!exist_field ? "absolute" : "differential", router_id, mobile_seq);

            //
            // If this is an absolute MAU, the existing set of addresses for this router must
            // be marked as needing deletion, in case they are not mentioned in the existing
            // address list.
            //
            if (!!exist_field) {
                addr = DEQ_HEAD(msync->core->addrs);
                while (!!addr) {
                    if (qcm_mobile_sync_addr_is_mobile(addr) && !!qd_bitmask_value(addr->rnodes, router->mask_bit))
                        BIT_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_TO_BE_DELETED);
                    addr = DEQ_NEXT(addr);
                }
            }

            //
            // Run through the add/exist list (depending on which we have) and lookup/add the
            // addresses, associating them with the sending router.  Clear the to-delete bits
            // on every address touched.  If hints are available, use them for addresses that
            // are newly created.
            //
            qd_parsed_field_t *field      = !!exist_field ? exist_field : add_field;
            qd_parsed_field_t *addr_field = qd_field_first_child(field);
            qd_parsed_field_t *hint_field = !!hints_count ? qd_field_first_child(hints_field) : 0;
            while (addr_field) {
                qd_iterator_t *iter = qd_parse_raw(addr_field);
                qdr_address_t *addr = 0;
                int treatment_hint = !!hint_field ? qd_parse_as_int(hint_field) : -1;

                do {
                    qd_hash_retrieve(msync->core->addr_hash, iter, (void**) &addr);
                    if (!addr) {
                        qdr_address_config_t   *addr_config;
                        qd_address_treatment_t  treatment =
                            qdr_treatment_for_address_hash_with_default_CT(msync->core,
                                                                           iter,
                                                                           qcm_mobile_sync_default_treatment(msync->core, treatment_hint),
                                                                           &addr_config);
                        addr = qdr_address_CT(msync->core, treatment, addr_config);
                        if (!addr) {
                            qd_log(msync->log, QD_LOG_CRITICAL, "map_destination: ignored");
                            assert(false);
                            break;
                        }
                        qd_hash_insert(msync->core->addr_hash, iter, addr, &addr->hash_handle);
                        DEQ_ITEM_INIT(addr);
                        DEQ_INSERT_TAIL(msync->core->addrs, addr);

                        //
                        // if the address is a link route, add the pattern to the wildcard
                        // address parse tree
                        //
                        const char *a_str = (const char*) qd_hash_key_by_handle(addr->hash_handle);
                        if (QDR_IS_LINK_ROUTE(a_str[0])) {
                            qdr_link_route_map_pattern_CT(msync->core, iter, addr);
                        }
                    }

                    BIT_CLEAR(addr->sync_mask, ADDR_SYNC_ADDRESS_TO_BE_DELETED);
                    if (!qd_bitmask_value(addr->rnodes, router->mask_bit)) {
                        qd_bitmask_set_bit(addr->rnodes, router->mask_bit);
                        router->ref_count++;
                        addr->cost_epoch--;
                        qdr_addr_start_inlinks_CT(msync->core, addr);
       
                        //
                        // Raise an address event if this is the first destination for the address
                        //
                        if (qd_bitmask_cardinality(addr->rnodes) + DEQ_SIZE(addr->rlinks) == 1)
                            qdrc_event_addr_raise(msync->core, QDRC_EVENT_ADDR_BECAME_DEST, addr);
                        else if (qd_bitmask_cardinality(addr->rnodes) == 1 && DEQ_SIZE(addr->rlinks) == 1)
                            qdrc_event_addr_raise(msync->core, QDRC_EVENT_ADDR_TWO_DEST, addr);
                    }
                } while (false);

                addr_field = qd_field_next_child(addr_field);
                hint_field = !!hint_field ? qd_field_next_child(hint_field) : 0;
            }

            //
            // Run through the delete list, if it exists, and disassociate each address from the
            // sending router.  Check the address to see if it needs to be deleted.
            //
            if (!!del_field) {
                addr_field = qd_field_first_child(del_field);
                while (!!addr_field) {
                    qd_iterator_t *iter = qd_parse_raw(addr_field);
                    qdr_address_t *addr = 0;

                    qd_hash_retrieve(msync->core->addr_hash, iter, (void**) &addr);
                    if (!!addr) {
                        if (qd_bitmask_value(addr->rnodes, router->mask_bit)) {
                            qd_bitmask_clear_bit(addr->rnodes, router->mask_bit);
                            router->ref_count--;
                            addr->cost_epoch--;

                            //
                            // Raise an address event if this was the last destination for the address
                            //
                            if (qd_bitmask_cardinality(addr->rnodes) + DEQ_SIZE(addr->rlinks) == 0)
                                qdrc_event_addr_raise(msync->core, QDRC_EVENT_ADDR_NO_LONGER_DEST, addr);
                            else if (qd_bitmask_cardinality(addr->rnodes) == 0 && DEQ_SIZE(addr->rlinks) == 1)
                                qdrc_event_addr_raise(msync->core, QDRC_EVENT_ADDR_ONE_LOCAL_DEST, addr);

                            qdr_check_addr_CT(msync->core, addr);
                        }
                    }
                    addr_field = qd_field_next_child(addr_field);
                }
            }

            //
            // If this was an absolute MAU, disassociate any addresses remaining with the
            // to-delete flag set.
            //
            if (!!exist_field) {
                addr = DEQ_HEAD(msync->core->addrs);
                while (!!addr) {
                    qdr_address_t *next_addr = DEQ_NEXT(addr);
                    if (qcm_mobile_sync_addr_is_mobile(addr)
                        && !!qd_bitmask_value(addr->rnodes, router->mask_bit)
                        && BIT_IS_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_TO_BE_DELETED)) {
                        qd_bitmask_clear_bit(addr->rnodes, router->mask_bit);
                        router->ref_count--;
                        addr->cost_epoch--;

                        //
                        // Raise an address event if this was the last destination for the address
                        //
                        if (qd_bitmask_cardinality(addr->rnodes) + DEQ_SIZE(addr->rlinks) == 0)
                            qdrc_event_addr_raise(msync->core, QDRC_EVENT_ADDR_NO_LONGER_DEST, addr);
                        else if (qd_bitmask_cardinality(addr->rnodes) == 0 && DEQ_SIZE(addr->rlinks) == 1)
                            qdrc_event_addr_raise(msync->core, QDRC_EVENT_ADDR_ONE_LOCAL_DEST, addr);

                        qdr_check_addr_CT(msync->core, addr);
                    }
                    addr = next_addr;
                }
            }

            //
            // Tell the python router about the new mobile sequence
            //
            qdr_post_set_mobile_seq_CT(msync->core, router->mask_bit, mobile_seq);
        } else {
            log_unknown_router(msync, id_field, "MAU");
        }
    }
}


static uint64_t qcm_mobile_sync_on_message_CT(void                    *context,
                                              qd_message_t            *msg,
                                              int                      unused_link_maskbit,
                                              int                      unused_inter_router_cost,
                                              uint64_t                 unused_conn_id,
                                              const qd_policy_spec_t  *unused_policy_spec,
                                              qdr_error_t            **error)
{
    qdrm_mobile_sync_t *msync      = (qdrm_mobile_sync_t*) context;
    qd_iterator_t      *ap_iter    = qd_message_field_iterator(msg, QD_FIELD_APPLICATION_PROPERTIES);
    qd_iterator_t      *body_iter  = qd_message_field_iterator(msg, QD_FIELD_BODY);
    qd_parsed_field_t  *ap_field   = qd_parse(ap_iter);
    qd_parsed_field_t  *body_field = qd_parse(body_iter);

    if (!!ap_field && qd_parse_is_map(ap_field)) {
        qd_parsed_field_t *opcode_field = qd_parse_value_by_key(ap_field, OPCODE);

        if (qd_iterator_equal(qd_parse_raw(opcode_field), (const unsigned char*) MAR))
            qcm_mobile_sync_on_mar_CT(msync, body_field);

        if (qd_iterator_equal(qd_parse_raw(opcode_field), (const unsigned char*) MAU))
            qcm_mobile_sync_on_mau_CT(msync, body_field);
    }

    qd_parse_free(ap_field);
    qd_iterator_free(ap_iter);
    qd_parse_free(body_field);
    qd_iterator_free(body_iter);

    return PN_ACCEPTED;
}


//================================================================================
// Event Handlers
//================================================================================

static void qcm_mobile_sync_on_became_local_dest_CT(qdrm_mobile_sync_t *msync, qdr_address_t *addr)
{
    if (!qcm_mobile_sync_addr_is_mobile(addr))
        return;

    qd_log(msync->log, QD_LOG_DEBUG, "Became Local Dest: %s", (const char*) qd_hash_key_by_handle(addr->hash_handle));

    if (BIT_IS_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_ADD_LIST))
        return;

    if (BIT_IS_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_DEL_LIST)) {
        //
        // If the address was deleted since the last update, simply forget that it was deleted.
        //
        DEQ_REMOVE_N(SYNC_DEL, msync->deleted_addrs, addr);
        BIT_CLEAR(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_DEL_LIST);
    } else {
        DEQ_INSERT_TAIL_N(SYNC_ADD, msync->added_addrs, addr);
        BIT_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_ADD_LIST);
        qcm_mobile_sync_start_tracking(addr);
    }
}


static void qcm_mobile_sync_on_no_longer_local_dest_CT(qdrm_mobile_sync_t *msync, qdr_address_t *addr)
{
    if (!qcm_mobile_sync_addr_is_mobile(addr))
        return;

    qd_log(msync->log, QD_LOG_DEBUG, "No Longer Local Dest: %s", (const char*) qd_hash_key_by_handle(addr->hash_handle));

    if (BIT_IS_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_DEL_LIST))
        return;

    if (BIT_IS_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_ADD_LIST)) {
        //
        // If the address was added since the last update, simply forget that it was added.
        //
        DEQ_REMOVE_N(SYNC_ADD, msync->added_addrs, addr);
        BIT_CLEAR(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_ADD_LIST);
        qcm_mobile_sync_stop_tracking(msync->core, addr);
    } else {
        DEQ_INSERT_TAIL_N(SYNC_DEL, msync->deleted_addrs, addr);
        BIT_SET(addr->sync_mask, ADDR_SYNC_ADDRESS_IN_DEL_LIST);
    }
}


static void qcm_mobile_sync_on_router_flush_CT(qdrm_mobile_sync_t *msync, qdr_node_t *router)
{
    router->mobile_seq = 0;
    qdr_address_t *addr = DEQ_HEAD(msync->core->addrs);
    while (!!addr) {
        qdr_address_t *next_addr = DEQ_NEXT(addr);
        if (qcm_mobile_sync_addr_is_mobile(addr)
            && !!qd_bitmask_value(addr->rnodes, router->mask_bit)) {
            //
            // This is an address mapped to the router.  Unmap the address and clean up.
            //
            qd_bitmask_clear_bit(addr->rnodes, router->mask_bit);
            router->ref_count--;
            addr->cost_epoch--;

            //
            // Raise an address event if this was the last destination for the address
            //
            if (qd_bitmask_cardinality(addr->rnodes) + DEQ_SIZE(addr->rlinks) == 0)
                qdrc_event_addr_raise(msync->core, QDRC_EVENT_ADDR_NO_LONGER_DEST, addr);
            else if (qd_bitmask_cardinality(addr->rnodes) == 0 && DEQ_SIZE(addr->rlinks) == 1)
                qdrc_event_addr_raise(msync->core, QDRC_EVENT_ADDR_ONE_LOCAL_DEST, addr);

            qdr_check_addr_CT(msync->core, addr);
        }
        addr = next_addr;
    }
}


static void qcm_mobile_sync_on_router_advanced_CT(qdrm_mobile_sync_t *msync, qdr_node_t *router)
{
    //
    // Prepare a MAR to be sent to the router
    //
    qd_message_t *mar = qcm_mobile_sync_compose_mar(msync, router);

    //
    // Send the control message.  Set the exclude_inprocess and control flags.
    //
    int fanout = qdr_forward_message_CT(msync->core, router->owning_addr, mar, 0, true, true);
    qd_message_free(mar);

    //
    // Trace log the activity of this sequence update.
    //
    qd_log(msync->log, QD_LOG_DEBUG, "Send MAR request to router %s, have_seq=%"PRIu64", fanout=%d",
           (const char*) qd_hash_key_by_handle(router->owning_addr->hash_handle) + 1, router->mobile_seq, fanout);
}


static void qcm_mobile_sync_on_addr_event_CT(void          *context,
                                             qdrc_event_t   event_type,
                                             qdr_address_t *addr)
{
    qdrm_mobile_sync_t *msync = (qdrm_mobile_sync_t*) context;

    switch (event_type) {
    case QDRC_EVENT_ADDR_BECAME_LOCAL_DEST:
        qcm_mobile_sync_on_became_local_dest_CT(msync, addr);
        break;
        
    case QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST:
        qcm_mobile_sync_on_no_longer_local_dest_CT(msync, addr);
        break;
        
    default:
        break;
    }
}


static void qcm_mobile_sync_on_router_event_CT(void          *context,
                                               qdrc_event_t   event_type,
                                               qdr_node_t    *router)
{
    qdrm_mobile_sync_t *msync = (qdrm_mobile_sync_t*) context;

    switch (event_type) {
    case QDRC_EVENT_ROUTER_MOBILE_FLUSH:
        qcm_mobile_sync_on_router_flush_CT(msync, router);
        break;

    case QDRC_EVENT_ROUTER_MOBILE_SEQ_ADVANCED:
        qcm_mobile_sync_on_router_advanced_CT(msync, router);
        break;

    default:
        break;
    }
}


//================================================================================
// Module Handlers
//================================================================================

static bool qcm_mobile_sync_enable_CT(qdr_core_t *core)
{
    return core->router_mode == QD_ROUTER_MODE_INTERIOR;
}


static void qcm_mobile_sync_init_CT(qdr_core_t *core, void **module_context)
{
    qdrm_mobile_sync_t *msync = NEW(qdrm_mobile_sync_t);
    ZERO(msync);
    msync->core      = core;

    //
    // Subscribe to core events:
    //
    //  - ADDR_BECAME_LOCAL_DEST     - Indicates a new address needs to tbe sync'ed with other routers
    //  - ADDR_NO_LONGER_LOCAL_DEST  - Indicates an address needs to be un-sync'd with other routers
    //  - ROUTER_MOBILE_FLUSH        - All addresses associated with the router must be unmapped
    //  - ROUTER_MOBILE_SEQ_ADVANCED - A router has an advanced mobile-seq and needs to be queried
    //
    msync->event_sub = qdrc_event_subscribe_CT(core,
                                               QDRC_EVENT_ADDR_BECAME_LOCAL_DEST
                                               | QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST
                                               | QDRC_EVENT_ROUTER_MOBILE_FLUSH
                                               | QDRC_EVENT_ROUTER_MOBILE_SEQ_ADVANCED,
                                               0,
                                               0,
                                               qcm_mobile_sync_on_addr_event_CT,
                                               qcm_mobile_sync_on_router_event_CT,
                                               msync);

    //
    // Create and schedule a one-second recurring timer to drive the sync protocol
    //
    msync->timer = qdr_core_timer_CT(core, qcm_mobile_sync_on_timer_CT, msync);
    qdr_core_timer_schedule_CT(core, msync->timer, 0);

    //
    // Subscribe to receive messages sent to the 'qdrouter.ma' addresses
    //
    msync->message_sub1 = qdr_core_subscribe(core, "qdrouter.ma", 'L', '0',
                                             QD_TREATMENT_MULTICAST_ONCE, true, qcm_mobile_sync_on_message_CT, msync);
    msync->message_sub2 = qdr_core_subscribe(core, "qdrouter.ma", 'T', '0',
                                             QD_TREATMENT_MULTICAST_ONCE, true, qcm_mobile_sync_on_message_CT, msync);

    //
    // Create a log source for mobile address sync
    //
    msync->log = qd_log_source("ROUTER_MA");

    *module_context = msync;
}


static void qcm_mobile_sync_final_CT(void *module_context)
{
    qdrm_mobile_sync_t *msync = (qdrm_mobile_sync_t*) module_context;

    qdrc_event_unsubscribe_CT(msync->core, msync->event_sub);
    qdr_core_timer_free_CT(msync->core, msync->timer);

    qdr_core_unsubscribe(msync->message_sub1);
    qdr_core_unsubscribe(msync->message_sub2);

    free(msync);
}


QDR_CORE_MODULE_DECLARE("mobile_sync", qcm_mobile_sync_enable_CT, qcm_mobile_sync_init_CT, qcm_mobile_sync_final_CT)
