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

#include <qpid/dispatch/amqp.h>
#include <qpid/dispatch/bitmask.h>
#include <qpid/dispatch/compose.h>
#include <qpid/dispatch/parse.h>
#include <qpid/dispatch/router.h>


/**
 * All callbacks in this module shall be invoked on a connection thread from the server thread pool.
 * If the callback needs to perform work on a connection, it will be invoked on a thread that has
 * exclusive access to that connection.
 */

typedef struct qdr_core_t         qdr_core_t;
typedef struct qdr_subscription_t qdr_subscription_t;
typedef struct qdr_connection_t   qdr_connection_t;
typedef struct qdr_link_t         qdr_link_t;
typedef struct qdr_delivery_t     qdr_delivery_t;
typedef struct qdr_terminus_t     qdr_terminus_t;
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
void qdr_core_map_destination(qdr_core_t *core, int router_maskbit, const char *address_hash);
void qdr_core_unmap_destination(qdr_core_t *core, int router_maskbit, const char *address_hash);

typedef void (*qdr_mobile_added_t)   (void *context, const char *address_hash);
typedef void (*qdr_mobile_removed_t) (void *context, const char *address_hash);
typedef void (*qdr_link_lost_t)      (void *context, int link_maskbit);

void qdr_core_route_table_handlers(qdr_core_t           *core, 
                                   void                 *context,
                                   qdr_mobile_added_t    mobile_added,
                                   qdr_mobile_removed_t  mobile_removed,
                                   qdr_link_lost_t       link_lost);

/**
 ******************************************************************************
 * In-process messaging functions
 ******************************************************************************
 */
typedef void (*qdr_receive_t) (void *context, qd_message_t *msg, int link_maskbit, int inter_router_cost);

qdr_subscription_t *qdr_core_subscribe(qdr_core_t             *core,
                                       const char             *address,
                                       char                    aclass,
                                       char                    phase,
                                       qd_address_treatment_t  treatment,
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
void qdr_send_to1(qdr_core_t *core, qd_message_t *msg, qd_field_iterator_t *addr,
                  bool exclude_inprocess, bool control);
void qdr_send_to2(qdr_core_t *core, qd_message_t *msg, const char *addr,
                  bool exclude_inprocess, bool control);


/**
 ******************************************************************************
 * Connection functions
 ******************************************************************************
 */

typedef enum {
    QD_LINK_ENDPOINT,   ///< A link to a connected endpoint
    QD_LINK_CONTROL,    ///< A link to a peer router for control messages
    QD_LINK_ROUTER      ///< A link to a peer router for routed messages
} qd_link_type_t;

typedef enum {
    QDR_ROLE_NORMAL,
    QDR_ROLE_INTER_ROUTER,
    QDR_ROLE_ROUTE_CONTAINER,
    QDR_ROLE_ON_DEMAND
} qdr_connection_role_t;


/**
 * qdr_connection_opened
 *
 * This function must be called once for every connection that is opened in the router.
 * Once a new connection has been both remotely and locally opened, the core must be notified.
 *
 * @param core Pointer to the core object
 * @param incoming True iff this connection is associated with a listener, False if a connector
 * @param role The configured role of this connection
 * @param cost If the role is inter_router, this is the configured cost for the connection.
 * @param management_id - A unique identifier that is used in management and logging operations.
 * @param label Optional label provided in the connection's configuration.  This is used to 
 *        correlate the connection with waypoints and link-route destinations that use the connection.
 * @param strip_annotations_in True if configured to remove annotations on inbound messages.
 * @param strip_annotations_out True if configured to remove annotations on outbound messages.
 * @param link_capacity The capacity, in deliveries, for links in this connection.
 * @return Pointer to a connection object that can be used to refer to this connection over its lifetime.
 */
qdr_connection_t *qdr_connection_opened(qdr_core_t            *core,
                                        bool                   incoming,
                                        qdr_connection_role_t  role,
                                        int                    cost,
                                        uint64_t               management_id,
                                        const char            *label,
                                        const char            *remote_container_id,
                                        bool                   strip_annotations_in,
                                        bool                   strip_annotations_out,
                                        int                    link_capacity);

/**
 * qdr_connection_closed
 *
 * This function must be called when a connection is closed, either cleanly by protocol
 * or uncleanly by lost connectivity.  Once this functino is called, the caller must never
 * again refer to or use the connection pointer.
 *
 * @param conn The pointer returned by qdr_connection_opened
 */
void qdr_connection_closed(qdr_connection_t *conn);

/**
 * qdr_connection_set_context
 *
 * Store an arbitrary void pointer in the connection object.
 */
void qdr_connection_set_context(qdr_connection_t *conn, void *context);

/**
 * qdr_connection_get_context
 *
 * Retrieve the stored void pointer from the connection object.
 */
void *qdr_connection_get_context(const qdr_connection_t *conn);

/**
 * qdr_connection_process
 *
 * Allow the core to process work associated with this connection.
 * This function MUST be called on a thread that exclusively owns
 * this connection.
 *
 * @param conn The pointer returned by qdr_connection_opened
 * @return The number of actions processed.
 */
int qdr_connection_process(qdr_connection_t *conn);

/**
 * qdr_connection_activate_t callback
 *
 * Activate a connection for transmission (socket write).  This is called whenever
 * the core has deliveries on links, disposition updates on deliveries, or flow updates
 * to be sent across the connection.
 *
 * IMPORTANT: This function will be invoked on the core thread.  It must never block,
 * delay, or do any lenghty computation.
 *
 * @param context The context supplied when the callback was registered
 * @param conn The connection object to be activated
 */
typedef void (*qdr_connection_activate_t) (void *context, qdr_connection_t *conn);

/**
 ******************************************************************************
 * Terminus functions
 ******************************************************************************
 */

/**
 * qdr_terminus
 *
 * Create a qdr_terminus_t that contains all the content of the
 * pn_terminus_t.  Note that the pointer to the pn_terminus_t
 * _will not_ be held or referenced further after this function
 * returns.
 *
 * @param pn Pointer to a proton terminus object that will be copied into
 *           the qdr_terminus object
 * @return Pointer to a newly allocated qdr_terminus object
 */
qdr_terminus_t *qdr_terminus(pn_terminus_t *pn);

/**
 * qdr_terminus_free
 *
 * Free a qdr_terminus object once it is no longer needed.
 *
 * @param terminus The pointer returned by qdr_terminus()
 */
void qdr_terminus_free(qdr_terminus_t *terminus);

/**
 * qdr_terminus_copy
 *
 * Copy the contents of the qdr_terminus into a proton terminus
 *
 * @param from A qdr_terminus pointer returned by qdr_terminus()
 * @param to A proton terminus to  be overwritten with the contents
 *           of 'from'
 */
void qdr_terminus_copy(qdr_terminus_t *from, pn_terminus_t *to);

/**
 * qdr_terminus_add_capability
 *
 * Add a capability symbol to the terminus.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @param capability A string to be added as a symbol to the capability list
 */
void qdr_terminus_add_capability(qdr_terminus_t *term, const char *capability);

/**
 * qdr_terminus_has_capability
 *
 * Check to see if a terminus has a particular capability.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @param capability A string describing a capability to be checked
 * @return true iff the capability is advertised for this terminus
 */
bool qdr_terminus_has_capability(qdr_terminus_t *term, const char *capability);

/**
 * qdr_terminus_is_anonymous
 *
 * Indicate whether this terminus represents an anonymous endpoint.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @return true iff the terminus is anonymous
 */
bool qdr_terminus_is_anonymous(qdr_terminus_t *term);

/**
 * qdr_terminus_is_dynamic
 *
 * Indicate whether this terminus represents a dynamic endpoint.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @return true iff the terminus is dynamic
 */
bool qdr_terminus_is_dynamic(qdr_terminus_t *term);

/**
 * qdr_terminus_set_address
 *
 * Set the terminus address
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @param addr An AMQP address (null-terminated string)
 */
void qdr_terminus_set_address(qdr_terminus_t *term, const char *addr);

/**
 * qdr_terminus_get_address
 *
 * Return the address of the terminus in the form of an iterator.
 * The iterator is borrowed, the caller must not free the iterator.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @return A pointer to an iterator or 0 if the terminus is anonymous.
 */
qd_field_iterator_t *qdr_terminus_get_address(qdr_terminus_t *term);

/**
 * qdr_terminus_dnp_address
 *
 * Return the address field in the dynamic-node-properties if it is there.
 * This iterator is given, the caller must free it when it is no longer needed.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @return A pointer to an iterator or 0 if there is no such field.
 */
qd_field_iterator_t *qdr_terminus_dnp_address(qdr_terminus_t *term);


/**
 ******************************************************************************
 * Error functions
 ******************************************************************************
 */

qdr_error_t *qdr_error_from_pn(pn_condition_t *pn);
qdr_error_t *qdr_error(const char *name, const char *description);
void qdr_error_free(qdr_error_t *error);
void qdr_error_copy(qdr_error_t *from, pn_condition_t *to);
char *qdr_error_description(qdr_error_t *err);

/**
 ******************************************************************************
 * Link functions
 ******************************************************************************
 */

/**
 * qdr_link_set_context
 *
 * Store an arbitrary void pointer in the link object.
 */
void qdr_link_set_context(qdr_link_t *link, void *context);

/**
 * qdr_link_get_context
 *
 * Retrieve the stored void pointer from the link object.
 */
void *qdr_link_get_context(const qdr_link_t *link);

/**
 * qdr_link_type
 *
 * Retrieve the link-type from the link object.
 *
 * @param link Link object
 * @return Link-type
 */
qd_link_type_t qdr_link_type(const qdr_link_t *link);

/**
 * qdr_link_direction
 *
 * Retrieve the link-direction from the link object.
 *
 * @param link Link object
 * @return Link-direction
 */
qd_direction_t qdr_link_direction(const qdr_link_t *link);

/**
 * qdr_link_phase
 *
 * If this link is associated with an auto_link, return the address phase.  Otherwise
 * return zero.
 *
 * @param link Link object
 * @return 0 or the phase of the link's auto_link.
 */
int qdr_link_phase(const qdr_link_t *link);

/**
 * qdr_link_is_anonymous
 *
 * Indicate whether the link is anonymous.  Note that this is determined inside
 * the core thread.  In the time between first creating the link and when the
 * core thread determines its status, a link will indicate "true" for being anonymous.
 * The reason for this is to be conservative.  The anonymous check is an optimization
 * used by the caller to skip parsing the "to" field for messages on non-anonymous links.
 *
 * @param link Link object
 * @return True if the link is anonymous or the link hasn't been processed yet.
 */
bool qdr_link_is_anonymous(const qdr_link_t *link);

/**
 * qdr_link_is_routed
 *
 * Indicate whether the link is link-routed.
 *
 * @param link Link object
 * @return True if the link is link-routed.
 */
bool qdr_link_is_routed(const qdr_link_t *link);

/**
 * qdr_link_strip_annotations_in
 *
 * Indicate whether the link's connection is configured to strip message annotations on inbound messages.
 */
bool qdr_link_strip_annotations_in(const qdr_link_t *link);

/**
 * qdr_link_strip_annotations_oout
 *
 * Indicate whether the link's connection is configured to strip message annotations on outbound messages.
 */
bool qdr_link_strip_annotations_out(const qdr_link_t *link);

/**
 * qdr_link_name
 *
 * Retrieve the name of the link.
 *
 * @param link Link object
 * @return The link's name
 */
const char *qdr_link_name(const qdr_link_t *link);

/**
 * qdr_link_first_attach
 *
 * This function is invoked when a first-attach (not a response to an earlier attach)
 * arrives for a connection.
 *
 * @param conn Connection pointer returned by qdr_connection_opened
 * @param dir Direction of the new link, incoming or outgoing
 * @param source Source terminus of the attach
 * @param target Target terminus of the attach
 * @return A pointer to a new qdr_link_t object to track the link
 */
qdr_link_t *qdr_link_first_attach(qdr_connection_t *conn,
                                  qd_direction_t    dir,
                                  qdr_terminus_t   *source,
                                  qdr_terminus_t   *target,
                                  const char       *name);

/**
 * qdr_link_second_attach
 *
 * This function is invoked when a second-attach (a response to an attach we sent)
 * arrives for a connection.
 *
 * @param link The link pointer returned by qdr_link_first_attach or in a FIRST_ATTACH event.
 * @param source Source terminus of the attach
 * @param target Target terminus of the attach
 */
void qdr_link_second_attach(qdr_link_t *link, qdr_terminus_t *source, qdr_terminus_t *target);

/**
 * qdr_link_detach
 *
 * This function is invoked when a link detach arrives.
 *
 * @param link The link pointer returned by qdr_link_first_attach or in a FIRST_ATTACH event.
 * @param dt The type of detach that occurred.
 * @param error The link error from the detach frame or 0 if none.
 */
void qdr_link_detach(qdr_link_t *link, qd_detach_type_t dt, qdr_error_t *error);

/**
 * qdr_link_deliver
 *
 * Deliver a message to the router core for forwarding.  This function is used in cases where
 * the link contains all the information needed for proper message routing (i.e. non-anonymous
 * inbound links).
 *
 * @param link Pointer to the link over which the message arrived.
 * @param msg Pointer to the delivered message.  The sender is giving this reference to the router
 *            core.  The sender _must not_ free or otherwise use the message after invoking this function.
 * @param ingress Field iterator referencing the value of the ingress-router header.  NOTE: This
 *                iterator is assumed to reference content in the message that will stay valid
 *                through the lifetime of the message.
 * @param settled True iff the delivery is pre-settled.
 * @param link_exclusion If present, this is a bitmask of inter-router links that should not be used
 *                       to send this message.  This bitmask is created by the trace_mask module and
 *                       it built on the trace header from a received message.
 * @return Pointer to the qdr_delivery that will track the lifecycle of this delivery on this link.
 */
qdr_delivery_t *qdr_link_deliver(qdr_link_t *link, qd_message_t *msg, qd_field_iterator_t *ingress,
                                 bool settled, qd_bitmask_t *link_exclusion);
qdr_delivery_t *qdr_link_deliver_to(qdr_link_t *link, qd_message_t *msg,
                                    qd_field_iterator_t *ingress, qd_field_iterator_t *addr,
                                    bool settled, qd_bitmask_t *link_exclusion);
qdr_delivery_t *qdr_link_deliver_to_routed_link(qdr_link_t *link, qd_message_t *msg, bool settled,
                                                const uint8_t *tag, int tag_length);

void qdr_link_process_deliveries(qdr_core_t *core, qdr_link_t *link, int credit);

void qdr_link_flow(qdr_core_t *core, qdr_link_t *link, int credit, bool drain_mode);

typedef void (*qdr_link_first_attach_t)  (void *context, qdr_connection_t *conn, qdr_link_t *link, 
                                          qdr_terminus_t *source, qdr_terminus_t *target);
typedef void (*qdr_link_second_attach_t) (void *context, qdr_link_t *link,
                                          qdr_terminus_t *source, qdr_terminus_t *target);
typedef void (*qdr_link_detach_t)        (void *context, qdr_link_t *link, qdr_error_t *error, bool first);
typedef void (*qdr_link_flow_t)          (void *context, qdr_link_t *link, int credit);
typedef void (*qdr_link_offer_t)         (void *context, qdr_link_t *link, int delivery_count);
typedef void (*qdr_link_drained_t)       (void *context, qdr_link_t *link);
typedef void (*qdr_link_drain_t)         (void *context, qdr_link_t *link, bool mode);
typedef void (*qdr_link_push_t)          (void *context, qdr_link_t *link);
typedef void (*qdr_link_deliver_t)       (void *context, qdr_link_t *link, qdr_delivery_t *delivery, bool settled);
typedef void (*qdr_delivery_update_t)    (void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled);

void qdr_connection_handlers(qdr_core_t                *core,
                             void                      *context,
                             qdr_connection_activate_t  activate,
                             qdr_link_first_attach_t    first_attach,
                             qdr_link_second_attach_t   second_attach,
                             qdr_link_detach_t          detach,
                             qdr_link_flow_t            flow,
                             qdr_link_offer_t           offer,
                             qdr_link_drained_t         drained,
                             qdr_link_drain_t           drain,
                             qdr_link_push_t            push,
                             qdr_link_deliver_t         deliver,
                             qdr_delivery_update_t      delivery_update);

/**
 ******************************************************************************
 * Delivery functions
 ******************************************************************************
 */
void qdr_delivery_update_disposition(qdr_core_t *core, qdr_delivery_t *delivery, uint64_t disp,
                                     bool settled, bool ref_given);

void qdr_delivery_set_context(qdr_delivery_t *delivery, void *context);
void *qdr_delivery_get_context(qdr_delivery_t *delivery);
void qdr_delivery_incref(qdr_delivery_t *delivery);
void qdr_delivery_decref(qdr_delivery_t *delivery);
void qdr_delivery_tag(const qdr_delivery_t *delivery, const char **tag, int *length);
qd_message_t *qdr_delivery_message(const qdr_delivery_t *delivery);

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
    QD_ROUTER_LINK,
    QD_ROUTER_ADDRESS,
    QD_ROUTER_EXCHANGE,
    QD_ROUTER_BINDING,
    QD_ROUTER_FORBIDDEN
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
 */
void qdr_manage_create(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                       qd_field_iterator_t *name, qd_parsed_field_t *in_body, qd_composed_field_t *out_body);

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
 */
void qdr_manage_delete(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                       qd_field_iterator_t *name, qd_field_iterator_t *identity);

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
 */
void qdr_manage_read(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                     qd_field_iterator_t *name, qd_field_iterator_t *identity, qd_composed_field_t *body);


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
 */
void qdr_manage_update(qdr_core_t *core, void *context, qd_router_entity_type_t type,
                       qd_field_iterator_t *name, qd_field_iterator_t *identity,
                       qd_parsed_field_t *in_body, qd_composed_field_t *out_body);

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
                              qd_parsed_field_t *attribute_names, qd_composed_field_t *body);
void qdr_query_add_attribute_names(qdr_query_t *query);
void qdr_query_get_first(qdr_query_t *query, int offset);
void qdr_query_get_next(qdr_query_t *query);
void qdr_query_free(qdr_query_t *query);

typedef void (*qdr_manage_response_t) (void *context, const qd_amqp_error_t *status, bool more);
void qdr_manage_handler(qdr_core_t *core, qdr_manage_response_t response_handler);

#endif
