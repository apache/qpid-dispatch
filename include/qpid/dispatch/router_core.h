#ifndef __router_core_h__
#define __router_core_h__ 1
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

#include "qpid/dispatch/amqp.h"
#include "qpid/dispatch/bitmask.h"
#include "qpid/dispatch/compose.h"
#include "qpid/dispatch/parse.h"
#include "qpid/dispatch/policy_spec.h"
#include "qpid/dispatch/router.h"

/**
 * All callbacks in this module shall be invoked on a connection thread from the server thread pool.
 * If the callback needs to perform work on a connection, it will be invoked on a thread that has
 * exclusive access to that connection.
 */

typedef struct qdr_subscription_t qdr_subscription_t;
typedef struct qdr_error_t        qdr_error_t;

typedef enum {
    QD_ROUTER_MODE_STANDALONE,  ///< Standalone router.  No routing protocol participation
    QD_ROUTER_MODE_INTERIOR,    ///< Interior router.  Full participation in routing protocol.
    QD_ROUTER_MODE_EDGE,        ///< Edge router.  No transit-router capability.
    QD_ROUTER_MODE_ENDPOINT     ///< No routing except for internal modules (agent, etc.).
} qd_router_mode_t;
ENUM_DECLARE(qd_router_mode);

/**
 * Allocate and start an instance of the router core module.
 */
qdr_core_t *qdr_core(qd_dispatch_t *qd, qd_router_mode_t mode, const char *area, const char *id);

/**
 * Stop and deallocate an instance of the router core.
 */
void qdr_core_free(qdr_core_t *core);

/**
 ******************************************************************************
 * Miscellaneous functions
 ******************************************************************************
 */

/**
 * Drive the core-internal timer every one second.
 *
 * @param core Pointer to the core object returned by qd_core()
 */
void qdr_process_tick(qdr_core_t *core);


/**
 ******************************************************************************
 * Route table maintenance functions (Router Control)
 ******************************************************************************
 */
void qdr_core_add_router(qdr_core_t *core, const char *address, int router_maskbit);
void qdr_core_del_router(qdr_core_t *core, int router_maskbit);
void qdr_core_set_link(qdr_core_t *core, int router_maskbit, int link_maskbit);
void qdr_core_remove_link(qdr_core_t *core, int router_maskbit);
void qdr_core_set_next_hop(qdr_core_t *core, int router_maskbit, int nh_router_maskbit);
void qdr_core_remove_next_hop(qdr_core_t *core, int router_maskbit);
void qdr_core_set_cost(qdr_core_t *core, int router_maskbit, int cost);
void qdr_core_set_valid_origins(qdr_core_t *core, int router_maskbit, qd_bitmask_t *routers);
void qdr_core_flush_destinations(qdr_core_t *core, int router_maskbit);
void qdr_core_mobile_seq_advanced(qdr_core_t *core, int router_maskbit);

typedef void (*qdr_set_mobile_seq_t)    (void *context, int router_maskbit, uint64_t mobile_seq);
typedef void (*qdr_set_my_mobile_seq_t) (void *context, uint64_t mobile_seq);
typedef void (*qdr_link_lost_t)         (void *context, int link_maskbit);

void qdr_core_route_table_handlers(qdr_core_t              *core, 
                                   void                    *context,
                                   qdr_set_mobile_seq_t     set_mobile_seq,
                                   qdr_set_my_mobile_seq_t  set_my_mobile_seq,
                                   qdr_link_lost_t          link_lost);

/**
 ******************************************************************************
 * In-process messaging functions
 ******************************************************************************
 */

/**
 * Subscription on_message callback
 *
 * @param context The opaque context supplied in the call to qdr_core_subscribe
 * @param msg The received message
 * @param link_maskbit The maskbit identifying the neighbor router from which the message was received
 * @param inter_router_cost The inter-router-cost of the connection over which the message was received
 * @param conn_id The identifier of the connection over which the message wad received
 * @param policy Pointer to the policy-spec in effect for the connection.  This may be NULL
 * @param error Output error to be used if the message delivery is rejected
 * @return The disposition to be used in settling the delivery (if the delivery was not pre-settled)
 */
typedef uint64_t (*qdr_receive_t) (void *context, qd_message_t *msg, int link_maskbit, int inter_router_cost,
                                   uint64_t conn_id, const qd_policy_spec_t *policy, qdr_error_t **error);

/**
 * qdr_core_subscribe
 *
 * Subscribe an in-process handler to receive messages to a particular address.
 *
 * @param core Pointer to the core module
 * @param address The address of messages to be received
 * @param aclass Address class character
 * @param phase Address phase character ('0' .. '9')
 * @param treatment Treatment for the address if it be being created as a side effect of this call
 * @param in_core True iff the handler is to be run in the context of the core thread
 * @param on_message The handler function
 * @param context The opaque context sent to the handler on all invocations
 * @return Pointer to the subscription object
 */
qdr_subscription_t *qdr_core_subscribe(qdr_core_t             *core,
                                       const char             *address,
                                       char                    aclass,
                                       char                    phase,
                                       qd_address_treatment_t  treatment,
                                       bool                    in_core,
                                       qdr_receive_t           on_message,
                                       void                   *context);

void qdr_core_unsubscribe(qdr_subscription_t *sub);

/**
 * qdr_send_to
 *
 * Send a message to a destination.  This function is used only by in-process components that
 * create messages to be sent.  For these messages, there is no inbound link or delivery.
 * Note also that deliveries sent through this function will be pre-settled.
 *
 * @param core Pointer to the core module
 * @param msg Pointer to the message to be sent.  The message will be copied during the call
 *            and must be freed by the caller if the caller doesn't need to hold it for later use.
 * @param addr Field iterator describing the address to which the message should be delivered.
 * @param exclude_inprocess If true, the message will not be sent to in-process subscribers.
 * @param control If true, this message is to be treated as control traffic and flow on a control link.
 */
void qdr_send_to1(qdr_core_t *core, qd_message_t *msg, qd_iterator_t *addr,
                  bool exclude_inprocess, bool control);
void qdr_send_to2(qdr_core_t *core, qd_message_t *msg, const char *addr,
                  bool exclude_inprocess, bool control);


/**
 ******************************************************************************
 * Error functions
 ******************************************************************************
 */

qdr_error_t *qdr_error_from_pn(pn_condition_t *pn);
qdr_error_t *qdr_error(const char *name, const char *description);
void qdr_error_free(qdr_error_t *error);
void qdr_error_copy(qdr_error_t *from, pn_condition_t *to);
char *qdr_error_description(const qdr_error_t *err);
char *qdr_error_name(const qdr_error_t *err);
pn_data_t *qdr_error_info(const qdr_error_t *err);

/**
 ******************************************************************************
 * Management functions
 ******************************************************************************
 */
typedef enum {
    QD_ROUTER_CONFIG_ADDRESS,
    QD_ROUTER_CONFIG_LINK_ROUTE,
    QD_ROUTER_CONFIG_AUTO_LINK,
    QD_ROUTER_CONNECTION,
    QD_ROUTER_ROUTER,
    QD_ROUTER_LINK,
    QD_ROUTER_ADDRESS,
    QD_ROUTER_EXCHANGE,
    QD_ROUTER_BINDING,
    QD_ROUTER_FORBIDDEN,
    QD_ROUTER_CONN_LINK_ROUTE
} qd_router_entity_type_t;

typedef struct qdr_query_t qdr_query_t;

/**
 * qdr_manage_create
 *
 * Request a managed entity to be created in the router core.
 *
 * @param core Pointer to the core object returned by qd_core()
 * @param context An opaque context that will be passed back in the invocation of the response callback
 * @param type The entity type for the create request
 * @param name The name supplied for the entity
 * @param in_body The body of the request message
 * @param out_body A composed field for the body of the response message
 * @param in_conn The identity of the connection over which the mgmt message arrived (0 if config file)
 */
void qdr_manage_create(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                       qd_iterator_t *name, qd_parsed_field_t *in_body, qd_composed_field_t *out_body,
                       qd_buffer_list_t body_buffers, uint64_t in_conn);

/**
 * qdr_manage_delete
 *
 * Request the deletion of a managed entity in the router core.
 *
 * @param core Pointer to the core object returned by qd_core()
 * @param context An opaque context that will be passed back in the invocation of the response callback
 * @param type The entity type for the create request
 * @param name The name supplied with the request (or 0 if the identity was supplied)
 * @param identity The identity supplied with the request (or 0 if the name was supplied)
 * @param in_conn The identity of the connection over which the mgmt message arrived (0 if config file)
 */
void qdr_manage_delete(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                       qd_iterator_t *name, qd_iterator_t *identity, uint64_t in_conn);

/**
 * qdr_manage_read
 *
 * Request a read of a managed entity in the router core.
 *
 * @param core Pointer to the core object returned by qd_core()
 * @param context An opaque context that will be passed back in the invocation of the response callback
 * @param type The entity type for the create request
 * @param name The name supplied with the request (or 0 if the identity was supplied)
 * @param identity The identity supplied with the request (or 0 if the name was supplied)
 * @param body A composed field for the body of the response message
 * @param in_conn The identity of the connection over which the mgmt message arrived (0 if config file)
 */
void qdr_manage_read(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                     qd_iterator_t *name, qd_iterator_t *identity, qd_composed_field_t *body,
                     uint64_t in_conn);


/**
 * qdr_manage_update
 *
 * Request the update of a managed entity in the router core.
 *
 * @param core Pointer to the core object returned by qd_core()
 * @param context An opaque context that will be passed back in the invocation of the response callback
 * @param type The entity type for the update request
 * @param name The name supplied with the request (or 0 if the identity was supplied)
 * @param identity The identity supplied with the request (or 0 if the name was supplied)
 * @param in_body The body of the request message
 * @param out_body A composed field for the body of the response message
 * @param in_conn The identity of the connection over which the mgmt message arrived (0 if config file)
 */
void qdr_manage_update(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                       qd_iterator_t *name, qd_iterator_t *identity,
                       qd_parsed_field_t *in_body, qd_composed_field_t *out_body,
                       uint64_t in_conn);

/**
 * Sequence for running a query:
 *
 * 1) Locate the attributeNames field in the body of the QUERY request
 * 2) Create a composed field for the body of the reply message
 * 3) Call qdr_manage_query with the attributeNames field and the response body
 * 4) Start the body map, add the "attributeNames" key
 * 5) Call qdr_query_add_attribute_names.  This will add the attribute names list
 * 6) Add the "results" key, start the outer list
 * 7) Call qdr_query_get_first.  This will asynchronously add the first inner list.
 * 8) When the qdr_manage_response_t callback is invoked:
 *    a) if more is true and count is not exceeded, call qdr_query_get_next
 *    b) if more is false or count is exceeded, call qdr_query_free, close the outer list, close the map
 */

qdr_query_t *qdr_manage_query(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                              qd_parsed_field_t *attribute_names, qd_composed_field_t *body,
                              uint64_t in_conn);
void qdr_query_add_attribute_names(qdr_query_t *query);
void qdr_query_get_first(qdr_query_t *query, int offset);
void qdr_query_get_next(qdr_query_t *query);
void qdr_query_free(qdr_query_t *query);

typedef void (*qdr_manage_response_t) (void *context, const qd_amqp_error_t *status, bool more);
void qdr_manage_handler(qdr_core_t *core, qdr_manage_response_t response_handler);

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t flags;
#define QDR_ROUTER_VERSION_SNAPSHOT 0x0100
#define QDR_ROUTER_VERSION_RC       0x0200  // lower byte == RC #
#define QDR_ROUTER_VERSION_RC_MASK  0x00FF
} qdr_router_version_t;

// version >= (Major, Minor, Patch)
#define QDR_ROUTER_VERSION_AT_LEAST(V, MAJOR, MINOR, PATCH)                       \
    ((V).major > (MAJOR) || ((V).major == (MAJOR)                                 \
                             && ((V).minor > (MINOR) || ((V).minor == (MINOR)     \
                                                         && (V).patch >= (PATCH)) \
                             )                                                    \
                        )                                                         \
    )

// version < (Major, Minor, Patch)
#define QDR_ROUTER_VERSION_LESS_THAN(V, MAJOR, MINOR, PATCH)    \
    (!QDR_ROUTER_VERSION_AT_LEAST(V, MAJOR, MINOR, PATCH))


typedef struct {
    size_t connections;
    size_t links;
    size_t addrs;
    size_t routers;
    size_t link_routes;
    size_t auto_links;
    size_t presettled_deliveries;
    size_t dropped_presettled_deliveries;
    size_t accepted_deliveries;
    size_t rejected_deliveries;
    size_t released_deliveries;
    size_t modified_deliveries;
    size_t deliveries_ingress;
    size_t deliveries_egress;
    size_t deliveries_transit;
    size_t deliveries_ingress_route_container;
    size_t deliveries_egress_route_container;
    size_t deliveries_delayed_1sec;
    size_t deliveries_delayed_10sec;
    size_t deliveries_stuck;
    size_t links_blocked;
    size_t deliveries_redirected_to_fallback;
}  qdr_global_stats_t;
ALLOC_DECLARE(qdr_global_stats_t);
typedef void (*qdr_global_stats_handler_t) (void *context);
void qdr_request_global_stats(qdr_core_t *core, qdr_global_stats_t *stats, qdr_global_stats_handler_t callback, void *context);


#endif
