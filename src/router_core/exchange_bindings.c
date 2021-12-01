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
#include "exchange_bindings.h"

#include "delivery.h"
#include "forwarder.h"
#include "router_core_private.h"

#include "qpid/dispatch/ctools.h"

#include <inttypes.h>
#include <stdio.h>

// next_hop_t
// Describes the destination of a forwarded message
// May be shared by different bindings
//
typedef struct next_hop_t next_hop_t;
struct next_hop_t
{
    // per-exchange list of all next hops
    DEQ_LINKS_N(exchange_list, next_hop_t);
    // when hooked to the transmit list
    DEQ_LINKS_N(transmit_list, next_hop_t);

    int                  ref_count;  // binding references
    int                  phase;
    bool                 on_xmit_list;
    qdr_exchange_t      *exchange;
    unsigned char       *next_hop;
    qdr_address_t       *qdr_addr;
};

ALLOC_DECLARE(next_hop_t);
ALLOC_DEFINE(next_hop_t);
DEQ_DECLARE(next_hop_t, next_hop_list_t);

// qdr_binding_t
// Represents a subject key --> next hop mapping
// A binding is uniquely identified by the tuple (pattern, nextHop, phase).  No
// two bindings with the same tuple value can exist on an exchange.
// The binding is implemented using two classes: qdr_binding_t and
// next_hop_t. The qdr_binding_t holds the pattern and points to the
// next_hop_t.  This allows different patterns to share the same nextHop.
// Since there is only one next_hop_t instance for each (nextHop, phase) value,
// we guarantee only 1 copy of a message is forwarded to a given nextHop+phase
// even if multiple distinct patterns are matched. Ex: a message with a
// value of "a.b" will match two distict binding keys "+.b" and "a.+".  If
// both these patterns share the same next_hop_t only 1 copy of the message
// will be forwarded.
typedef struct qdr_binding qdr_binding_t;
struct qdr_binding
{
    // per-exchange list of all bindings
    DEQ_LINKS_N(exchange_list, qdr_binding_t);
    // parse tree node's list of bindings sharing the same pattern
    DEQ_LINKS_N(tree_list, qdr_binding_t);

    unsigned char       *name;
    uint64_t             identity;
    qdr_exchange_t      *exchange;

    unsigned char       *key;
    next_hop_t          *next_hop;

    uint64_t             msgs_matched;
};

ALLOC_DECLARE(qdr_binding_t);
ALLOC_DEFINE(qdr_binding_t);
DEQ_DECLARE(qdr_binding_t, qdr_binding_list_t);


struct qdr_exchange {
    DEQ_LINKS(qdr_exchange_t);          // for core->exchanges
    qdr_core_t         *core;
    uint64_t            identity;
    unsigned char      *name;
    unsigned char      *address;
    int                 phase;
    qd_parse_tree_t    *parse_tree;
    qdr_address_t      *qdr_addr;
    next_hop_t         *alternate;
    qdr_binding_list_t  bindings;
    next_hop_list_t     next_hops;
    qdr_forwarder_t    *old_forwarder;

    uint64_t msgs_received;
    uint64_t msgs_dropped;
    uint64_t msgs_routed;
    uint64_t msgs_alternate;
};

ALLOC_DECLARE(qdr_exchange_t);
ALLOC_DEFINE(qdr_exchange_t);

static void qdr_exchange_free(qdr_exchange_t *ex);
static qdr_exchange_t *qdr_exchange(qdr_core_t    *core,
                                    qd_iterator_t *name,
                                    qd_iterator_t *address,
                                    int            phase,
                                    qd_iterator_t *alternate,
                                    int            alt_phase,
                                    qd_parse_tree_type_t method);
static void write_config_exchange_map(qdr_exchange_t      *ex,
                                      qd_composed_field_t *body);
static qdr_exchange_t *find_exchange(qdr_core_t    *core,
                                     qd_iterator_t *identity,
                                     qd_iterator_t *name);
static qdr_binding_t *find_binding(qdr_core_t *core,
                                   qd_iterator_t  *identity,
                                   qd_iterator_t  *name);
static void write_config_exchange_list(qdr_exchange_t *ex,
                                       qdr_query_t    *query);
static qdr_binding_t *qdr_binding(qdr_exchange_t *ex,
                                  qd_iterator_t  *name,
                                  qd_iterator_t  *key,
                                  qd_iterator_t  *next_hop,
                                  int             phase);
static void write_config_binding_map(qdr_binding_t       *binding,
                                     qd_composed_field_t *body);
static qdr_binding_t *find_binding(qdr_core_t    *core,
                                   qd_iterator_t *identity,
                                   qd_iterator_t *name);
static void qdr_binding_free(qdr_binding_t *b);
static void write_config_binding_list(qdr_binding_t *binding,
                                      qdr_query_t   *query);
static qdr_binding_t *get_binding_at_index(qdr_core_t *core,
                                           int         index);
static next_hop_t *next_hop(qdr_exchange_t *ex,
                            qd_iterator_t  *address,
                            int             phase);
static void next_hop_release(next_hop_t *next_hop);
static next_hop_t *find_next_hop(qdr_exchange_t *ex,
                                 qd_iterator_t  *address,
                                 int             phase);
static bool gather_next_hops(void *handle,
                             const char *pattern,
                             void *payload);
static int send_message(qdr_core_t     *core,
                        next_hop_t     *next_hop,
                        qd_message_t   *msg,
                        qdr_delivery_t *in_delivery,
                        bool            exclude_inprocess,
                        bool            control);


//
// The Exchange Forwarder
//
int qdr_forward_exchange_CT(qdr_core_t     *core,
                            qdr_address_t  *addr,
                            qd_message_t   *msg,
                            qdr_delivery_t *in_delivery,
                            bool            exclude_inprocess,
                            bool            control)
{
    int forwarded = 0;
    const bool presettled = !!in_delivery ? in_delivery->settled : true;
    qdr_exchange_t *ex = addr->exchange;
    assert(ex);

    ex->msgs_received += 1;

    // honor the disposition for the exchange address (this may not be right??)
    if (ex->old_forwarder)
        forwarded = ex->old_forwarder->forward_message(core, addr, msg, in_delivery, exclude_inprocess, control);

    // @TODO(kgiusti): de-duplicate this code (cut & paste from multicast
    // forwarder)
    //
    // If the delivery is not presettled, set the settled flag for forwarding so all
    // outgoing deliveries will be presettled.
    //
    // NOTE:  This is the only multicast mode currently supported.  Others will likely be
    //        implemented in the future.
    //
    if (!presettled)
        in_delivery->settled = true;

    qd_iterator_t *subject = qd_message_check_depth(msg, QD_DEPTH_PROPERTIES) == QD_MESSAGE_DEPTH_OK
        ? qd_message_field_iterator(msg, QD_FIELD_SUBJECT)
        : NULL;
    next_hop_list_t transmit_list;
    DEQ_INIT(transmit_list);

    if (subject) {
        // find all matching bindings and build a list of their next hops
        qd_parse_tree_search(ex->parse_tree, subject, gather_next_hops, &transmit_list);
        qd_iterator_free(subject);
    }

    // if there are valid next hops then we're routing this message based on an
    // entirely new destination address.  We need to reset the origin and the
    // excluded link flags in the delivery.  We also need to reset the trace
    // annotations and ingress field in the message. This is done because it is
    // possible that the next hop is reached via the same link/router this
    // message arrived from.
    // @TODO(kgiusti) - loop detection
    if (DEQ_SIZE(transmit_list) > 0 || ex->alternate) {
        if (in_delivery) {
            in_delivery->origin = 0;
            qd_bitmask_free(in_delivery->link_exclusion);
            in_delivery->link_exclusion = 0;
        }

        qd_message_reset_trace_annotation(msg);
        qd_message_reset_ingress_router_annotation(msg);
    }

    next_hop_t *next_hop = DEQ_HEAD(transmit_list);
    while (next_hop) {
        DEQ_REMOVE_N(transmit_list, transmit_list, next_hop);
        next_hop->on_xmit_list = false;
        assert(next_hop->qdr_addr);
        // @TODO(kgiusti) - non-recursive handling of next hop if it is an exchange
        forwarded += send_message(ex->core, next_hop, msg, in_delivery, exclude_inprocess, control);
        next_hop = DEQ_HEAD(transmit_list);
    }

    if (forwarded == 0 && ex->alternate) {
        forwarded = send_message(ex->core, ex->alternate, msg, in_delivery, exclude_inprocess, control);
        if (forwarded) {
            ex->msgs_alternate += 1;
        }
    }

    // @TODO(kgiusti): de-duplicate the settlement code (cut & paste from
    // multicast forwarder)
    if (forwarded == 0) {
        ex->msgs_dropped += 1;
        if (!presettled) {
            //
            // The delivery was not originally presettled and it was not
            // forwarded to any destinations, return it to its original
            // unsettled state.
            //
            in_delivery->settled = false;
        }
    } else {
        ex->msgs_routed += 1;
        if (in_delivery && !presettled) {
            //
            // The delivery was not presettled and it was forwarded to at least
            // one destination.  Accept and settle the delivery only if the
            // entire delivery has been received.
            //
            const bool receive_complete = qd_message_receive_complete(qdr_delivery_message(in_delivery));
            if (receive_complete) {
                in_delivery->disposition = PN_ACCEPTED;
                qdr_delivery_push_CT(core, in_delivery);
            }
        }
    }

    return forwarded;
}


// callback from parse tree search:
// handle = transmit_list containing all matching next_hops
// pattern = pattern that matches the search (ignored)
// payload = list of bindings configured for the pattern
static bool gather_next_hops(void *handle, const char *pattern, void *payload)
{
    next_hop_list_t *transmit_list = (next_hop_list_t *)handle;
    qdr_binding_list_t *bindings = (qdr_binding_list_t *)payload;

    qdr_binding_t *binding = DEQ_HEAD(*bindings);
    while (binding) {
        binding->msgs_matched += 1;
        // note - since multiple bindings may reference the next hop, it is
        // possible a next hop has already been added to the transmit list.
        // do not re-add.  This is not thread safe but that is fine since all
        // forwarding is done on the core thread.
        if (!binding->next_hop->on_xmit_list) {
            DEQ_INSERT_TAIL_N(transmit_list, *transmit_list, binding->next_hop);
            binding->next_hop->on_xmit_list = true;
        }
        binding = DEQ_NEXT_N(tree_list, binding);
    }
    return true;  // keep searching
}


// Forward a copy of the message to the to_addr address
static int send_message(qdr_core_t     *core,
                        next_hop_t     *next_hop,
                        qd_message_t   *msg,
                        qdr_delivery_t *in_delivery,
                        bool            exclude_inprocess,
                        bool            control)
{
    int count = 0;
    qd_message_t *copy = qd_message_copy(msg);

    qd_log(core->log, QD_LOG_TRACE, "Exchange '%s' forwarding message to '%s'",
           next_hop->exchange->name, next_hop->next_hop);

    // set "to override" and "phase" message annotations based on the next hop
    qd_message_set_to_override_annotation(copy, (const char*) next_hop->next_hop);
    qd_message_set_phase_annotation(copy, next_hop->phase);

    count = qdr_forward_message_CT(core, next_hop->qdr_addr, copy, in_delivery, exclude_inprocess, control);
    qd_message_free(copy);

    return count;
}


long qdr_exchange_binding_count(const qdr_exchange_t *ex)
{
    return (long) DEQ_SIZE(ex->bindings);
}


qdr_address_t *qdr_exchange_alternate_addr(const qdr_exchange_t *ex)
{
    return (ex->alternate) ? ex->alternate->qdr_addr : NULL;
}


/////////////////////////////
// Exchange Management API //
/////////////////////////////

#define QDR_CONFIG_EXCHANGE_NAME          0
#define QDR_CONFIG_EXCHANGE_IDENTITY      1
#define QDR_CONFIG_EXCHANGE_ADDRESS       2
#define QDR_CONFIG_EXCHANGE_PHASE         3
#define QDR_CONFIG_EXCHANGE_ALTERNATE     4
#define QDR_CONFIG_EXCHANGE_ALT_PHASE     5
#define QDR_CONFIG_EXCHANGE_MATCH_METHOD  6
#define QDR_CONFIG_EXCHANGE_BINDING_COUNT 7
#define QDR_CONFIG_EXCHANGE_RECEIVED      8
#define QDR_CONFIG_EXCHANGE_DROPPED       9
#define QDR_CONFIG_EXCHANGE_FORWARDED     10
#define QDR_CONFIG_EXCHANGE_DIVERTED      11

const char *qdr_config_exchange_columns[QDR_CONFIG_EXCHANGE_COLUMN_COUNT + 1] =
    {"name",
     "identity",
     "address",
     "phase",
     "alternateAddress",
     "alternatePhase",
     "matchMethod",
     "bindingCount",
     "receivedCount",
     "droppedCount",
     "forwardedCount",
     "divertedCount",
     0};

// from management_agent.c
extern const unsigned char *config_exchange_entity_type;

#define QDR_CONFIG_BINDING_NAME         0
#define QDR_CONFIG_BINDING_IDENTITY     1
#define QDR_CONFIG_BINDING_EXCHANGE     2
#define QDR_CONFIG_BINDING_KEY          3
#define QDR_CONFIG_BINDING_NEXTHOP      4
#define QDR_CONFIG_BINDING_NHOP_PHASE   5
#define QDR_CONFIG_BINDING_MATCHED      6

const char *qdr_config_binding_columns[QDR_CONFIG_BINDING_COLUMN_COUNT + 1] =
    {"name",
     "identity",
     "exchangeName",
     "bindingKey",
     "nextHopAddress",
     "nextHopPhase",
     "matchedCount",
     0};

// from management_agent.c
extern const unsigned char *config_binding_entity_type;


// called on core shutdown to release all exchanges
//
void qdr_exchange_free_all(qdr_core_t *core)
{
    qdr_exchange_t *ex = DEQ_HEAD(core->exchanges);
    while (ex) {
        qdr_exchange_t *next = DEQ_NEXT(ex);
        qdr_exchange_free(ex);
        ex = next;
    }
}


// Exchange CREATE
//
//
void qdra_config_exchange_create_CT(qdr_core_t         *core,
                                    qd_iterator_t      *name,
                                    qdr_query_t        *query,
                                    qd_parsed_field_t  *in_body)
{
    qdr_exchange_t *ex = NULL;

    query->status = QD_AMQP_BAD_REQUEST;

    if (!qd_parse_is_map(in_body)) {
        query->status.description = "Body of request must be a map";
        goto exit;
    }

    if (!name) {
        query->status.description = "exchange requires a unique name";
        goto exit;
    }

    qd_parsed_field_t *address_field = qd_parse_value_by_key(in_body,
                                                             qdr_config_exchange_columns[QDR_CONFIG_EXCHANGE_ADDRESS]);
    if (!address_field) {
        query->status.description = "exchange address is mandatory";
        goto exit;
    }
    qd_iterator_t *address = qd_parse_raw(address_field);

    // check for duplicates
    {
        qdr_exchange_t *eptr = 0;
        for (eptr = DEQ_HEAD(core->exchanges); eptr; eptr = DEQ_NEXT(eptr)) {
            if (qd_iterator_equal(address, eptr->address)) {
                query->status.description = "duplicate exchange address";
                goto exit;
            } else if (qd_iterator_equal(name, eptr->name)) {
                query->status.description = "duplicate exchange name";
                goto exit;
            }
        }
    }

    qd_parsed_field_t *method_field = qd_parse_value_by_key(in_body,
                                                            qdr_config_exchange_columns[QDR_CONFIG_EXCHANGE_MATCH_METHOD]);
    qd_parse_tree_type_t method = QD_PARSE_TREE_AMQP_0_10;
    if (method_field) {
        if (qd_iterator_equal(qd_parse_raw(method_field), (const unsigned char *)"mqtt")) {
            method = QD_PARSE_TREE_MQTT;
        } else if (!qd_iterator_equal(qd_parse_raw(method_field), (const unsigned char *)"amqp")) {
            query->status.description = "Exchange matchMethod must be either 'amqp' or 'mqtt'";
            goto exit;
        }
    }

    long phase = 0;
    qd_parsed_field_t *phase_field = qd_parse_value_by_key(in_body,
                                                           qdr_config_exchange_columns[QDR_CONFIG_EXCHANGE_PHASE]);
    if (phase_field) {
        phase = qd_parse_as_long(phase_field);
        if (phase < 0 || phase > 9) {
            query->status.description = "phase must be in the range 0-9";
            goto exit;
        }
    }

    qd_iterator_t *alternate = NULL;
    long alt_phase = 0;
    qd_parsed_field_t *alternate_field = qd_parse_value_by_key(in_body,
                                                               qdr_config_exchange_columns[QDR_CONFIG_EXCHANGE_ALTERNATE]);
    if (alternate_field) {
        alternate = qd_parse_raw(alternate_field);
        qd_parsed_field_t *alt_phase_field = qd_parse_value_by_key(in_body,
                                                                   qdr_config_exchange_columns[QDR_CONFIG_EXCHANGE_ALT_PHASE]);
        if (alt_phase_field) {
            alt_phase = qd_parse_as_long(alt_phase_field);
            if (alt_phase < 0 || alt_phase > 9) {
                query->status.description = "phase must be in the range 0-9";
                goto exit;
            }
        }
    }

    ex = qdr_exchange(core, name, address, phase, alternate, alt_phase, method);
    if (ex) {
        // @TODO(kgiusti) - for now, until the behavior is nailed down:
        static int warn_user;
        if (!warn_user) {
            warn_user = 1;
            qd_log(core->agent_log, QD_LOG_WARNING,
                   "The Exchange/Binding feature is currently EXPERIMENTAL."
                   " Its functionality may change in future releases"
                   " of the Qpid Dispatch Router. Backward compatibility is"
                   " not guaranteed.");
        }
        query->status = QD_AMQP_CREATED;
        if (query->body) {
            write_config_exchange_map(ex, query->body);
        }
    } else {
        query->status.description = "failed to allocate exchange";
    }

 exit:

    if (query->status.status == QD_AMQP_CREATED.status) {
        qd_log(core->agent_log, QD_LOG_DEBUG,
               "Exchange %s CREATED (id=%"PRIu64")", ex->name, ex->identity);

    } else {
        qd_log(core->agent_log, QD_LOG_ERROR,
               "Error performing CREATE of %s: %s", config_exchange_entity_type, query->status.description);
        // return a NULL body:
        if (query->body) qd_compose_insert_null(query->body);
    }

    if (query->body) {
        qdr_agent_enqueue_response_CT(core, query);
    } else {
        // no body == create from internal config parser
        qdr_query_free(query);
    }
}


// Exchange DELETE:
//
void qdra_config_exchange_delete_CT(qdr_core_t    *core,
                                    qdr_query_t   *query,
                                    qd_iterator_t *name,
                                    qd_iterator_t *identity)
{
    qdr_exchange_t *ex = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing DELETE of %s: %s",
               config_exchange_entity_type, query->status.description);
    } else {
        ex = find_exchange(core, identity, name);
        if (ex) {
            qd_log(core->agent_log, QD_LOG_DEBUG,
                   "Exchange %s DELETED (id=%"PRIu64")", ex->name, ex->identity);
            qdr_exchange_free(ex);
            query->status = QD_AMQP_NO_CONTENT;
        } else
            query->status = QD_AMQP_NOT_FOUND;
    }

    qdr_agent_enqueue_response_CT(core, query);
}

// Exchange GET
//
void qdra_config_exchange_get_CT(qdr_core_t    *core,
                                 qd_iterator_t *name,
                                 qd_iterator_t *identity,
                                 qdr_query_t   *query,
                                 const char    *columns[])
{
    qdr_exchange_t *ex = 0;

    if (!name && !identity) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s",
               config_exchange_entity_type, query->status.description);
    }
    else {
        ex = find_exchange(core, identity, name);
        if (!ex) {
            query->status = QD_AMQP_NOT_FOUND;
        }
        else {
            if (query->body) write_config_exchange_map(ex, query->body);
            query->status = QD_AMQP_OK;
        }
    }

    qdr_agent_enqueue_response_CT(core, query);
}


// Exchange GET first:
//
void qdra_config_exchange_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    //
    // Queries that get this far will always succeed.
    //
    query->status = QD_AMQP_OK;

    //
    // If the offset goes beyond the set of objects, end the query now.
    //
    if (offset >= DEQ_SIZE(core->exchanges)) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    //
    // Run to the object at the offset.
    //
    qdr_exchange_t *ex = DEQ_HEAD(core->exchanges);
    for (int i = 0; i < offset; i++)
        ex = DEQ_NEXT(ex);
    assert(ex);

    //
    // Write the columns of the object into the response body.
    //
    if (query->body) write_config_exchange_list(ex, query);

    //
    // Advance to the next address
    //
    query->next_offset = offset + 1;
    query->more = !!DEQ_NEXT(ex);

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}

// Exchange GET-NEXT
//
void qdra_config_exchange_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_exchange_t *ex = 0;

    if (query->next_offset < DEQ_SIZE(core->exchanges)) {
        ex = DEQ_HEAD(core->exchanges);
        for (int i = 0; i < query->next_offset && ex; i++)
            ex = DEQ_NEXT(ex);
    }

    if (ex) {
        //
        // Write the columns of the addr entity into the response body.
        //
        if (query->body) write_config_exchange_list(ex, query);

        //
        // Advance to the next object
        //
        query->next_offset++;
        query->more = !!DEQ_NEXT(ex);
    } else
        query->more = false;

    //
    // Enqueue the response.
    //
    qdr_agent_enqueue_response_CT(core, query);
}


// Binding CREATE
void qdra_config_binding_create_CT(qdr_core_t         *core,
                                   qd_iterator_t      *name,
                                   qdr_query_t        *query,
                                   qd_parsed_field_t  *in_body)
{
    qdr_binding_t *binding = NULL;
    qdr_exchange_t *ex = NULL;
    qd_iterator_t *key = NULL;

    query->status = QD_AMQP_BAD_REQUEST;

    if (!qd_parse_is_map(in_body)) {
        query->status.description = "Body of request must be a map";
        goto exit;
    }

    qd_parsed_field_t *exchange_field = qd_parse_value_by_key(in_body,
                                                              qdr_config_binding_columns[QDR_CONFIG_BINDING_EXCHANGE]);
    if (!exchange_field) {
        query->status.description = "Binding configuration requires an exchange";
        goto exit;
    }

    // lookup the exchange by its name:
    ex = find_exchange(core, NULL, qd_parse_raw(exchange_field));
    if (!ex) {
        query->status.description = "Named exchange does not exist";
        goto exit;
    }

    qd_parsed_field_t *next_hop_field = qd_parse_value_by_key(in_body,
                                                              qdr_config_binding_columns[QDR_CONFIG_BINDING_NEXTHOP]);
    if (!next_hop_field) {
        query->status.description = "No next hop specified";
        goto exit;
    }
    qd_iterator_t *nhop = qd_parse_raw(next_hop_field);

    qd_parsed_field_t *key_field = qd_parse_value_by_key(in_body,
                                                         qdr_config_binding_columns[QDR_CONFIG_BINDING_KEY]);
    // if no pattern given, assume match all "#":
    key = key_field ? qd_iterator_dup(qd_parse_raw(key_field)) : qd_iterator_string("#", ITER_VIEW_ALL);

    if (!qd_parse_tree_validate_pattern(ex->parse_tree, key)) {
        query->status.description = "The binding key pattern is invalid";
        goto exit;
    }

    qd_parsed_field_t *phase_field = qd_parse_value_by_key(in_body,
                                                         qdr_config_binding_columns[QDR_CONFIG_BINDING_NHOP_PHASE]);
    long phase = (phase_field ? qd_parse_as_long(phase_field) : 0);
    if (phase < 0 || phase > 9) {
        query->status.description = "phase must be in the range 0-9";
        goto exit;
    }

    // check for duplicates: the name and the tuple (key, next hop, phase) must
    // be unique per exchange

    for (qdr_binding_t *b = DEQ_HEAD(ex->bindings); b; b = DEQ_NEXT_N(exchange_list, b)) {
        if (name && b->name && qd_iterator_equal(name, b->name)) {
            query->status.description = "Duplicate next hop name";
            goto exit;
        } else if (qd_iterator_equal(key, b->key) &&
                   qd_iterator_equal(nhop, b->next_hop->next_hop) &&
                   phase == b->next_hop->phase) {
            query->status.description = "Next hop for key already exists";
            goto exit;
        }
    }

    binding = qdr_binding(ex, name, key, nhop, phase);
    if (binding) {
        query->status = QD_AMQP_CREATED;
        if (query->body) {
            write_config_binding_map(binding, query->body);
        }
    } else {
        query->status.description = "Failed to allocate next hop";
    }


 exit:

    if (query->status.status == QD_AMQP_CREATED.status) {
        qd_log(core->agent_log, QD_LOG_DEBUG,
               "Exchange %s Binding %s -> %s CREATED (id=%"PRIu64")", ex->name,
               binding->key, binding->next_hop->next_hop, binding->identity);
    } else {
        qd_log(core->agent_log, QD_LOG_ERROR,
               "Error performing CREATE of %s: %s",
               config_binding_entity_type,
               query->status.description);
        // return a NULL body:
        if (query->body) qd_compose_insert_null(query->body);
    }

    if (query->body) {
        qdr_agent_enqueue_response_CT(core, query);
    } else {
        // no body == create from internal config parser
        qdr_query_free(query);
    }

    if (key) qd_iterator_free(key);
}


// Binding DELETE
//
void qdra_config_binding_delete_CT(qdr_core_t    *core,
                                   qdr_query_t   *query,
                                   qd_iterator_t *name,
                                   qd_iterator_t *identity)
{
    if (!identity && !name) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No binding name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing DELETE of %s: %s",
               config_binding_entity_type, query->status.description);
    } else {
        qdr_binding_t *binding = find_binding(core, identity, name);
        if (!binding) {
            query->status = QD_AMQP_NOT_FOUND;
        } else {
            qd_log(core->agent_log, QD_LOG_DEBUG,
                   "Binding %s -> %s on exchange %s DELETED (id=%"PRIu64")",
                   binding->key,
                   binding->next_hop->next_hop,
                   binding->exchange->name,
                   binding->identity);
            qdr_binding_free(binding);
            query->status = QD_AMQP_NO_CONTENT;
        }
    }

    qdr_agent_enqueue_response_CT(core, query);
}


// Binding GET
//
void qdra_config_binding_get_CT(qdr_core_t    *core,
                                qd_iterator_t *name,
                                qd_iterator_t *identity,
                                qdr_query_t   *query,
                                const char    *columns[])
{
    if (!identity && !name) {
        query->status = QD_AMQP_BAD_REQUEST;
        query->status.description = "No binding name or identity provided";
        qd_log(core->agent_log, QD_LOG_ERROR, "Error performing READ of %s: %s",
               config_binding_entity_type, query->status.description);
    } else {
        qdr_binding_t *binding = find_binding(core, identity, name);
        if (binding == 0) {
            query->status = QD_AMQP_NOT_FOUND;
        }
        else {
            if (query->body) write_config_binding_map(binding, query->body);
            query->status = QD_AMQP_OK;
        }
    }

    qdr_agent_enqueue_response_CT(core, query);
}


// Binding GET first
//
void qdra_config_binding_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset)
{
    query->status = QD_AMQP_OK;

    qdr_binding_t *binding = get_binding_at_index(core, offset);
    if (!binding) {
        query->more = false;
        qdr_agent_enqueue_response_CT(core, query);
        return;
    }

    if (query->body) write_config_binding_list(binding, query);
    query->next_offset = offset + 1;
    query->more = !!(DEQ_NEXT_N(exchange_list, binding) || DEQ_NEXT(binding->exchange));
    qdr_agent_enqueue_response_CT(core, query);
}


// Binding GET-NEXT
//
void qdra_config_binding_get_next_CT(qdr_core_t *core, qdr_query_t *query)
{
    qdr_binding_t *binding = get_binding_at_index(core, query->next_offset);
    if (binding) {
        if (query->body) write_config_binding_list(binding, query);
        query->next_offset++;
        query->more = !!(DEQ_NEXT_N(exchange_list, binding) || DEQ_NEXT(binding->exchange));
    } else
        query->more = false;
    qdr_agent_enqueue_response_CT(core, query);
}


// Exchange constructor/destructor
static qdr_exchange_t *qdr_exchange(qdr_core_t *core,
                                    qd_iterator_t *name,
                                    qd_iterator_t *address,
                                    int            phase,
                                    qd_iterator_t *alternate,
                                    int            alt_phase,
                                    qd_parse_tree_type_t method)
{
    assert(address);
    qdr_exchange_t *ex = new_qdr_exchange_t();
    if (ex) {
        ZERO(ex);
        DEQ_ITEM_INIT(ex);
        ex->core = core;
        ex->identity = qdr_identifier(core);
        ex->name = qd_iterator_copy(name);
        ex->address = qd_iterator_copy(address);
        ex->phase = phase;
        ex->parse_tree = qd_parse_tree_new(method);
        DEQ_INIT(ex->bindings);
        DEQ_INIT(ex->next_hops);

        qd_iterator_reset_view(address, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_phase(address, (char) phase + '0');
        qd_hash_retrieve(core->addr_hash, address, (void **)&ex->qdr_addr);
        if (!ex->qdr_addr) {
            qdr_address_config_t   *addr_config;
            qd_address_treatment_t  treatment = qdr_treatment_for_address_hash_CT(core, address, &addr_config);
            ex->qdr_addr = qdr_address_CT(core, treatment, addr_config);
            qd_hash_insert(core->addr_hash, address, ex->qdr_addr, &ex->qdr_addr->hash_handle);
            DEQ_INSERT_TAIL(core->addrs, ex->qdr_addr);
        }

        // we're going to override the forwarder
        qdr_forwarder_t *old = ex->qdr_addr->forwarder;
        qdr_forwarder_t *new = qdr_new_forwarder(qdr_forward_exchange_CT,
                                                 old ? old->forward_attach : 0,
                                                 old ? old->bypass_valid_origins: false);
        ex->old_forwarder = old;
        ex->qdr_addr->forwarder = new;
        ex->qdr_addr->ref_count += 1;
        ex->qdr_addr->exchange = ex;
        DEQ_INSERT_TAIL(core->exchanges, ex);

        if (alternate) {
            ex->alternate = next_hop(ex, alternate, alt_phase);
        }

        //
        // TODO - handle case where there was already a local dest.
        //
        qdr_addr_start_inlinks_CT(ex->core, ex->qdr_addr);
        qdrc_event_addr_raise(ex->core, QDRC_EVENT_ADDR_BECAME_LOCAL_DEST, ex->qdr_addr);
    }

    return ex;
}

static void qdr_exchange_free(qdr_exchange_t *ex)
{
    if (ex->core->running && DEQ_SIZE(ex->qdr_addr->rlinks) == 0) {
        qdrc_event_addr_raise(ex->core, QDRC_EVENT_ADDR_NO_LONGER_LOCAL_DEST, ex->qdr_addr);
    }

    DEQ_REMOVE(ex->core->exchanges, ex);
    while (DEQ_SIZE(ex->bindings) > 0) {
        // freeing the binding removes it from the binding list
        // and the parse tree
        qdr_binding_free(DEQ_HEAD(ex->bindings));
    }
    if (ex->alternate) {
        next_hop_release(ex->alternate);
    }
    assert(DEQ_IS_EMPTY(ex->next_hops));

    free(ex->qdr_addr->forwarder);
    ex->qdr_addr->forwarder = ex->old_forwarder;
    assert(ex->qdr_addr->ref_count > 0);
    ex->qdr_addr->ref_count -= 1;
    qdr_check_addr_CT(ex->core, ex->qdr_addr);
    free(ex->name);
    free(ex->address);

    qd_parse_tree_free(ex->parse_tree);
    free_qdr_exchange_t(ex);
}

// Binding constructor/destructor

static qdr_binding_t *qdr_binding(qdr_exchange_t *ex,
                                  qd_iterator_t *name,
                                  qd_iterator_t *key,
                                  qd_iterator_t *nhop,
                                  int            phase)
{
    qdr_binding_t *b = new_qdr_binding_t();
    if (b) {
        ZERO(b);
        DEQ_ITEM_INIT_N(exchange_list, b);
        DEQ_ITEM_INIT_N(tree_list, b);

        b->name = qd_iterator_copy(name);
        b->identity = qdr_identifier(ex->core);
        b->exchange = ex;
        b->key = qd_iterator_copy(key);
        b->next_hop = next_hop(ex, nhop, phase);

        qdr_binding_list_t  *bindings = NULL;
        if (!qd_parse_tree_get_pattern(ex->parse_tree, key, (void **)&bindings)) {
            // new pattern
            bindings = malloc(sizeof(*bindings));
            DEQ_INIT(*bindings);
            qd_parse_tree_add_pattern(ex->parse_tree, key, bindings);
        }
        assert(bindings);
        DEQ_INSERT_TAIL_N(tree_list, *bindings, b);
        DEQ_INSERT_TAIL_N(exchange_list, ex->bindings, b);
    }
    return b;
}


static void qdr_binding_free(qdr_binding_t *b)
{
    qdr_binding_list_t *bindings = NULL;
    qdr_exchange_t *ex = b->exchange;

    qd_iterator_t *k_iter = qd_iterator_string((char *)b->key,
                                               ITER_VIEW_ALL);
    if (qd_parse_tree_get_pattern(ex->parse_tree, k_iter, (void **)&bindings)) {
        assert(bindings && !DEQ_IS_EMPTY(*bindings));
        DEQ_REMOVE_N(tree_list, *bindings, b);
        if (DEQ_IS_EMPTY(*bindings)) {
            qd_parse_tree_remove_pattern(ex->parse_tree, k_iter);
            free(bindings);
        }
    }
    qd_iterator_free(k_iter);

    DEQ_REMOVE_N(exchange_list, b->exchange->bindings, b);
    free(b->name);
    free(b->key);
    next_hop_release(b->next_hop);
    free_qdr_binding_t(b);
}


// Next Hop constructor/destructor

static next_hop_t *next_hop(qdr_exchange_t *ex,
                            qd_iterator_t  *address,
                            int             phase)
{
    next_hop_t *nh = find_next_hop(ex, address, phase);
    if (!nh) {
        nh = new_next_hop_t();
        if (!nh) return NULL;
        ZERO(nh);
        DEQ_ITEM_INIT_N(exchange_list, nh);
        DEQ_ITEM_INIT_N(transmit_list, nh);
        nh->exchange = ex;
        nh->next_hop = qd_iterator_copy(address);
        nh->phase = phase;

        qd_iterator_reset_view(address, ITER_VIEW_ADDRESS_HASH);
        qd_iterator_annotate_phase(address, (char) phase + '0');
        qd_hash_retrieve(ex->core->addr_hash, address, (void **)&nh->qdr_addr);
        if (!nh->qdr_addr) {
            qdr_core_t *core = ex->core;
            qdr_address_config_t   *addr_config;
            qd_address_treatment_t  treatment = qdr_treatment_for_address_hash_CT(core, address, &addr_config);
            nh->qdr_addr = qdr_address_CT(core, treatment, addr_config);
            qd_hash_insert(core->addr_hash, address, nh->qdr_addr, &nh->qdr_addr->hash_handle);
            DEQ_INSERT_TAIL(core->addrs, nh->qdr_addr);
        }
        nh->qdr_addr->ref_count += 1;
        DEQ_INSERT_TAIL_N(exchange_list, ex->next_hops, nh);
    }
    nh->ref_count += 1;         // for caller's reference
    return nh;
}


static void next_hop_release(next_hop_t *nh)
{
    assert(nh->ref_count > 0);
    if (--nh->ref_count == 0) {
        assert(nh->qdr_addr->ref_count > 0);
        if (--nh->qdr_addr->ref_count == 0) {
            qdr_check_addr_CT(nh->exchange->core, nh->qdr_addr);
        }
        DEQ_REMOVE_N(exchange_list, nh->exchange->next_hops, nh);
        assert(!nh->on_xmit_list);
        free(nh->next_hop);
        free_next_hop_t(nh);
    }
}


// lookup

static qdr_exchange_t *find_exchange(qdr_core_t *core, qd_iterator_t *identity, qd_iterator_t *name)
{
    qdr_exchange_t *ex = 0;
    for (ex = DEQ_HEAD(core->exchanges); ex; ex = DEQ_NEXT(ex)) {
        if (identity) {  // ignore name - identity takes precedence
            // Convert the identity for comparison
            char id[100];
            snprintf(id, 100, "%"PRId64, ex->identity);
            if (qd_iterator_equal(identity, (const unsigned char*) id))
                break;
        } else if (name && qd_iterator_equal(name, ex->name))
            break;
    }
    return ex;
}


static qdr_binding_t *find_binding(qdr_core_t *core, qd_iterator_t *identity, qd_iterator_t *name)
{
    for (qdr_exchange_t *ex = DEQ_HEAD(core->exchanges); ex; ex = DEQ_NEXT(ex)) {
        for (qdr_binding_t *binding = DEQ_HEAD(ex->bindings); binding; binding = DEQ_NEXT_N(exchange_list, binding)) {
            if (identity) {  // ignore name - identity takes precedence
                // Convert the identity for comparison
                char id[100];
                snprintf(id, 100, "%"PRId64, binding->identity);
                if (qd_iterator_equal(identity, (const unsigned char*) id))
                    return binding;
            } else if (name && qd_iterator_equal(name, binding->name))
                return binding;
        }
    }
    return NULL;
}


static next_hop_t *find_next_hop(qdr_exchange_t *ex,
                                 qd_iterator_t  *address,
                                 int             phase)
{
    next_hop_t *nh = DEQ_HEAD(ex->next_hops);
    DEQ_FIND_N(exchange_list, nh, (phase == nh->phase) && qd_iterator_equal(address, nh->next_hop));
    return nh;
}


// Management helper routines

static void exchange_insert_column(qdr_exchange_t *ex, int col, qd_composed_field_t *body)
{
    switch(col) {
    case QDR_CONFIG_EXCHANGE_NAME:
        qd_compose_insert_string(body, (const char *)ex->name);
        break;

    case QDR_CONFIG_EXCHANGE_IDENTITY: {
        char id_str[100];
        snprintf(id_str, 100, "%"PRId64, ex->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_CONFIG_EXCHANGE_ADDRESS:
        qd_compose_insert_string(body, (const char *)ex->address);
        break;

    case QDR_CONFIG_EXCHANGE_PHASE:
        qd_compose_insert_int(body, ex->phase);
        break;

    case QDR_CONFIG_EXCHANGE_ALTERNATE:
        if (ex->alternate && ex->alternate->next_hop)
            qd_compose_insert_string(body, (const char *)ex->alternate->next_hop);
        else
            qd_compose_insert_null(body);
        break;


    case QDR_CONFIG_EXCHANGE_ALT_PHASE:
        if (ex->alternate)
            qd_compose_insert_int(body, ex->alternate->phase);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_EXCHANGE_MATCH_METHOD:
        switch (qd_parse_tree_type(ex->parse_tree)) {
        case QD_PARSE_TREE_AMQP_0_10:
            qd_compose_insert_string(body, "amqp");
            break;
        case QD_PARSE_TREE_MQTT:
            qd_compose_insert_string(body, "mqtt");
            break;
        default:
            break;
        }
        break;

    case QDR_CONFIG_EXCHANGE_BINDING_COUNT:
        qd_compose_insert_uint(body, DEQ_SIZE(ex->bindings));
        break;

    case QDR_CONFIG_EXCHANGE_RECEIVED:
        qd_compose_insert_ulong(body, ex->msgs_received);
        break;

    case QDR_CONFIG_EXCHANGE_DROPPED:
        qd_compose_insert_ulong(body, ex->msgs_dropped);
        break;

    case QDR_CONFIG_EXCHANGE_FORWARDED:
        qd_compose_insert_ulong(body, ex->msgs_routed);
        break;

    case QDR_CONFIG_EXCHANGE_DIVERTED:
        qd_compose_insert_ulong(body, ex->msgs_alternate);
        break;
    }
}


static void binding_insert_column(qdr_binding_t *b, int col, qd_composed_field_t *body)
{
    switch(col) {
    case QDR_CONFIG_BINDING_NAME:
        if (b->name)
            qd_compose_insert_string(body, (char *)b->name);
        else
            qd_compose_insert_null(body);
        break;

    case QDR_CONFIG_BINDING_IDENTITY: {
        char id_str[100];
        snprintf(id_str, 100, "%"PRIu64, b->identity);
        qd_compose_insert_string(body, id_str);
        break;
    }

    case QDR_CONFIG_BINDING_EXCHANGE:
        qd_compose_insert_string(body, (char *)b->exchange->name);
        break;

    case QDR_CONFIG_BINDING_KEY:
        qd_compose_insert_string(body, (char *)b->key);
        break;

    case QDR_CONFIG_BINDING_NEXTHOP:
        assert(b->next_hop && b->next_hop->next_hop);
        qd_compose_insert_string(body, (char *)b->next_hop->next_hop);
        break;

    case QDR_CONFIG_BINDING_NHOP_PHASE:
        assert(b->next_hop);
        qd_compose_insert_int(body, b->next_hop->phase);
        break;

    case QDR_CONFIG_BINDING_MATCHED:
        qd_compose_insert_ulong(body, b->msgs_matched);
        break;
    }
}


static void write_config_exchange_map(qdr_exchange_t      *ex,
                                      qd_composed_field_t *body)
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_CONFIG_EXCHANGE_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_config_exchange_columns[i]);
        exchange_insert_column(ex, i, body);
    }

    qd_compose_end_map(body);
}


static void write_config_exchange_list(qdr_exchange_t *ex,
                                       qdr_query_t    *query)
{
    qd_compose_start_list(query->body);

    int i = 0;
    while (query->columns[i] >= 0) {
        exchange_insert_column(ex, query->columns[i], query->body);
        i++;
    }

    qd_compose_end_list(query->body);
}


static void write_config_binding_map(qdr_binding_t       *binding,
                                     qd_composed_field_t *body)
{
    qd_compose_start_map(body);

    for(int i = 0; i < QDR_CONFIG_BINDING_COLUMN_COUNT; i++) {
        qd_compose_insert_string(body, qdr_config_binding_columns[i]);
        binding_insert_column(binding, i, body);
    }

    qd_compose_end_map(body);
}

static void write_config_binding_list(qdr_binding_t *binding,
                                      qdr_query_t   *query)
{
    qd_compose_start_list(query->body);

    int i = 0;
    while (query->columns[i] >= 0) {
        binding_insert_column(binding, query->columns[i], query->body);
        i++;
    }

    qd_compose_end_list(query->body);
}


static qdr_binding_t *get_binding_at_index(qdr_core_t *core, int index)
{
    qdr_binding_t *binding = 0;

    // skip to the proper exchange:
    qdr_exchange_t *ex = DEQ_HEAD(core->exchanges);
    while (ex && index >= DEQ_SIZE(ex->bindings)) {
        index -= DEQ_SIZE(ex->bindings);
        ex = DEQ_NEXT(ex);
    }

    if (ex) {
        // then to the target binding
        assert(index < DEQ_SIZE(ex->bindings));
        binding = DEQ_HEAD(ex->bindings);
        while (index--) {
            binding = DEQ_NEXT_N(exchange_list, binding);
        }
    }
    return binding;
}

