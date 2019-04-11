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

typedef struct qdr_subscription_t    qdr_subscription_t;
typedef struct qdr_connection_t      qdr_connection_t;
typedef struct qdr_link_t            qdr_link_t;
typedef struct qdr_delivery_t        qdr_delivery_t;
typedef struct qdr_terminus_t        qdr_terminus_t;
typedef struct qdr_error_t           qdr_error_t;
typedef struct qdr_connection_info_t qdr_connection_info_t;

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
void qdr_core_map_destination(qdr_core_t *core, int router_maskbit, const char *address_hash, int treatment_hint);
void qdr_core_unmap_destination(qdr_core_t *core, int router_maskbit, const char *address_hash);

typedef void (*qdr_mobile_added_t)   (void *context, const char *address_hash, qd_address_treatment_t treatment);
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
typedef void (*qdr_receive_t) (void *context, qd_message_t *msg, int link_maskbit, int inter_router_cost,
                               uint64_t conn_id);

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
void qdr_send_to1(qdr_core_t *core, qd_message_t *msg, qd_iterator_t *addr,
                  bool exclude_inprocess, bool control);
void qdr_send_to2(qdr_core_t *core, qd_message_t *msg, const char *addr,
                  bool exclude_inprocess, bool control);


/**
 ******************************************************************************
 * Connection functions
 ******************************************************************************
 */

typedef enum {
    QD_LINK_ENDPOINT,      ///< A link to a connected endpoint
    QD_LINK_CONTROL,       ///< A link to a peer router for control messages
    QD_LINK_ROUTER,        ///< A link to a peer router for routed messages
    QD_LINK_EDGE_DOWNLINK  ///< Default link from an interior router to an edge router
} qd_link_type_t;

typedef enum {
    QDR_ROLE_NORMAL,
    QDR_ROLE_INTER_ROUTER,
    QDR_ROLE_ROUTE_CONTAINER,
    QDR_ROLE_EDGE_CONNECTION
} qdr_connection_role_t;

typedef void (*qdr_connection_bind_context_t) (qdr_connection_t *context, void* token);

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
 * @param policy_allow_dynamic_link_routes True if this connection is allowed by policy to create link route destinations.
 * @param link_capacity The capacity, in deliveries, for links in this connection.
 * @param vhost If non-null, this is the vhost of the connection to be used for multi-tenancy.
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
                                        bool                   policy_allow_dynamic_link_routes,
                                        bool                   policy_allow_admin_status_update,
                                        int                    link_capacity,
                                        const char            *vhost,
                                        qdr_connection_info_t *connection_info,
                                        qdr_connection_bind_context_t context_binder,
                                        void* bind_token);

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

bool qdr_connection_route_container(qdr_connection_t *conn);

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
 * qdr_connection_get_tenant_space
 *
 * Retrieve the multi-tenant space for a connection.  Returns 0 if there is
 * no multi-tenancy on this connection.
 */
const char *qdr_connection_get_tenant_space(const qdr_connection_t *conn, int *len);

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
 * Activate a connection with pending work from the core to ensure it will be processed by
 * the proactor: the core has deliveries on links, disposition updates on deliveries, or
 * flow updates to be sent across the connection.
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
 * qdr_terminus_format
 *
 * Write a human-readable representation of the terminus content to the string
 * in 'output'.
 *
 * @param terminus The pointer returned by qdr_terminus()
 * @param output The string buffer where the result shall be written
 * @param size Input: the number of bytes availabie in output for writing.  Output: the
 *             number of bytes remaining after the operation.
 */
void qdr_terminus_format(qdr_terminus_t *terminus, char *output, size_t *size);

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
 * qdr_terminus_waypoint_capability
 *
 * If the terminus has a waypoint capability, return the ordinal of the
 * waypoint.  If not, return zero.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @return 1..9 if the terminus has waypoint capability, 0 otherwise
 */
int qdr_terminus_waypoint_capability(qdr_terminus_t *term);

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
 * qdr_terminus_is_coordinator
 *
 * Indicates if the terminus is a coordinator.
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @return true iff the terminus is a coordinator
 */
bool qdr_terminus_is_coordinator(qdr_terminus_t *term);

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
 * qdr_terminus_survives_disconnect
 *
 * Indicate whether this terminus will survive disconnection (i.e. if
 * state is expected to be kept).
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @return true iff the terminus has a timeout greater than 0 or an
 * expiry-policy of never
 */
bool qdr_terminus_survives_disconnect(qdr_terminus_t *term);

/**
 * qdr_terminus_set_address
 *
 * Set the terminus address
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @param addr An AMQP address (null-terminated string)
 */
void qdr_terminus_set_address(qdr_terminus_t *term, const char *addr);
void qdr_terminus_set_address_iterator(qdr_terminus_t *term, qd_iterator_t *addr);

/**
 * qdr_terminus_get_address
 *
 * Return the address of the terminus in the form of an iterator.
 * The iterator is borrowed, the caller must not free the iterator.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @return A pointer to an iterator or 0 if the terminus is anonymous.
 */
qd_iterator_t *qdr_terminus_get_address(qdr_terminus_t *term);

/**
 * qdr_terminus_insert_address_prefix
 *
 * Insert the given prefix into the terminus address
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @param prefix null-terminated string
 */
void qdr_terminus_insert_address_prefix(qdr_terminus_t *term, const char *prefix);

/**
 * qdr_terminus_strip_address_prefix
 *
 * Remove the given prefix from the terminus address
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @param prefix null-terminated string
 */
void qdr_terminus_strip_address_prefix(qdr_terminus_t *term, const char *prefix);

/**
 * qdr_terminus_dnp_address
 *
 * Return the address field in the dynamic-node-properties if it is there.
 * This iterator is given, the caller must free it when it is no longer needed.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @return A pointer to an iterator or 0 if there is no such field.
 */
qd_iterator_t *qdr_terminus_dnp_address(qdr_terminus_t *term);

/**
 * qdr_terminus_set_dnp_address_iterator
 *
 * Overwrite the dynamic-node-properties.address in the terminus
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 * @param iter An iterator whos view shall be placed in the dnp.address
 */
void qdr_terminus_set_dnp_address_iterator(qdr_terminus_t *term, qd_iterator_t *iter);


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
 * qdr_link_internal_address
 *
 * If this link is associated with an auto_link and the auto_link has different
 * internal and external addresses, return the internal (routing) address.
 *
 * @param link Link object
 * @return 0 or the auto_link's internal address.
 */
const char *qdr_link_internal_address(const qdr_link_t *link);

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
 * qdr_link_stalled_outbound
 *
 * Tell the link that it has been stalled outbound due to back-pressure from the
 * transport buffers.  Stalling is undone during link-flow processing.
 */
void qdr_link_stalled_outbound(qdr_link_t *link);

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
 * @param name - name of the link
 * @param terminus_addr - terminus address if any
 * @return A pointer to a new qdr_link_t object to track the link
 */
qdr_link_t *qdr_link_first_attach(qdr_connection_t *conn,
                                  qd_direction_t    dir,
                                  qdr_terminus_t   *source,
                                  qdr_terminus_t   *target,
                                  const char       *name,
                                  const char       *terminus_addr);

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
 * qdr_link_delete
 *
 * Request that the router-core delete this link and free all its associated resources.
 *
 * @param link The link pointer returned by qdr_link_first_attach or in a FIRST_ATTACH event.
 */
void qdr_link_delete(qdr_link_t *link);

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
 * @param ingress_index The bitmask index of the router that this delivery entered the network through.
 * @return Pointer to the qdr_delivery that will track the lifecycle of this delivery on this link.
 */
qdr_delivery_t *qdr_link_deliver(qdr_link_t *link, qd_message_t *msg, qd_iterator_t *ingress,
                                 bool settled, qd_bitmask_t *link_exclusion, int ingress_index);
qdr_delivery_t *qdr_link_deliver_to(qdr_link_t *link, qd_message_t *msg,
                                    qd_iterator_t *ingress, qd_iterator_t *addr,
                                    bool settled, qd_bitmask_t *link_exclusion, int ingress_index);
qdr_delivery_t *qdr_link_deliver_to_routed_link(qdr_link_t *link, qd_message_t *msg, bool settled,
                                                const uint8_t *tag, int tag_length,
                                                uint64_t disposition, pn_data_t* disposition_state);
qdr_delivery_t *qdr_deliver_continue(qdr_core_t *core, qdr_delivery_t *delivery);

int qdr_link_process_deliveries(qdr_core_t *core, qdr_link_t *link, int credit);

void qdr_link_flow(qdr_core_t *core, qdr_link_t *link, int credit, bool drain_mode);

typedef void (*qdr_link_first_attach_t)  (void *context, qdr_connection_t *conn, qdr_link_t *link, 
                                          qdr_terminus_t *source, qdr_terminus_t *target);
typedef void (*qdr_link_second_attach_t) (void *context, qdr_link_t *link,
                                          qdr_terminus_t *source, qdr_terminus_t *target);
typedef void (*qdr_link_detach_t)        (void *context, qdr_link_t *link, qdr_error_t *error, bool first, bool close);
typedef void (*qdr_link_flow_t)          (void *context, qdr_link_t *link, int credit);
typedef void (*qdr_link_offer_t)         (void *context, qdr_link_t *link, int delivery_count);
typedef void (*qdr_link_drained_t)       (void *context, qdr_link_t *link);
typedef void (*qdr_link_drain_t)         (void *context, qdr_link_t *link, bool mode);
typedef int  (*qdr_link_push_t)          (void *context, qdr_link_t *link, int limit);
typedef uint64_t (*qdr_link_deliver_t)   (void *context, qdr_link_t *link, qdr_delivery_t *delivery, bool settled);
typedef void (*qdr_delivery_update_t)    (void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled);
typedef void (*qdr_connection_close_t)   (void *context, qdr_connection_t *conn, qdr_error_t *error);

void qdr_connection_handlers(qdr_core_t             *core,
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
                             qdr_delivery_update_t      delivery_update,
                             qdr_connection_close_t     conn_close);

/**
 ******************************************************************************
 * Delivery functions
 ******************************************************************************
 */
void qdr_delivery_update_disposition(qdr_core_t *core, qdr_delivery_t *delivery, uint64_t disp,
                                     bool settled, qdr_error_t *error, pn_data_t *ext_state, bool ref_given);

void qdr_delivery_set_context(qdr_delivery_t *delivery, void *context);
void *qdr_delivery_get_context(qdr_delivery_t *delivery);
qdr_link_t *qdr_delivery_link(const qdr_delivery_t *delivery);
void qdr_delivery_incref(qdr_delivery_t *delivery, const char *label);
void qdr_delivery_decref(qdr_core_t *core, qdr_delivery_t *delivery, const char *label);
void qdr_delivery_tag(const qdr_delivery_t *delivery, const char **tag, int *length);
qd_message_t *qdr_delivery_message(const qdr_delivery_t *delivery);
qdr_error_t *qdr_delivery_error(const qdr_delivery_t *delivery);
bool qdr_delivery_presettled(const qdr_delivery_t *delivery);
void qdr_delivery_write_extension_state(qdr_delivery_t *dlv, pn_delivery_t* pdlv, bool update_disposition);
bool qdr_delivery_send_complete(const qdr_delivery_t *delivery);
bool qdr_delivery_tag_sent(const qdr_delivery_t *delivery);
void qdr_delivery_set_tag_sent(const qdr_delivery_t *delivery, bool tag_sent);
bool qdr_delivery_receive_complete(const qdr_delivery_t *delivery);
void qdr_delivery_set_disposition(qdr_delivery_t *delivery, uint64_t disposition);
uint64_t qdr_delivery_disposition(const qdr_delivery_t *delivery);
void qdr_delivery_set_aborted(const qdr_delivery_t *delivery, bool aborted);
bool qdr_delivery_is_aborted(const qdr_delivery_t *delivery);
void qdr_delivery_add_num_closed_receivers(qdr_delivery_t *delivery);

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

qdr_connection_info_t *qdr_connection_info(bool             is_encrypted,
                                           bool             is_authenticated,
                                           bool             opened,
                                           char            *sasl_mechanisms,
                                           qd_direction_t   dir,
                                           const char      *host,
                                           const char      *ssl_proto,
                                           const char      *ssl_cipher,
                                           const char      *user,
                                           const char      *container,
                                           pn_data_t       *connection_properties,
                                           int              ssl_ssf,
                                           bool             ssl);


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
}  qdr_global_stats_t;
ALLOC_DECLARE(qdr_global_stats_t);
typedef void (*qdr_global_stats_handler_t) (void *context);
void qdr_request_global_stats(qdr_core_t *core, qdr_global_stats_t *stats, qdr_global_stats_handler_t callback, void *context);

#endif
