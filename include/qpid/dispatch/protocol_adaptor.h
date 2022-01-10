#ifndef __protocol_adaptor_h__
#define __protocol_adaptor_h__ 1
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

#include "qpid/dispatch/delivery_state.h"
#include "qpid/dispatch/policy_spec.h"
#include "qpid/dispatch/router_core.h"

typedef struct qdr_protocol_adaptor_t  qdr_protocol_adaptor_t;
typedef struct qdr_connection_t        qdr_connection_t;
typedef struct qdr_link_t              qdr_link_t;
typedef struct qdr_delivery_t          qdr_delivery_t;
typedef struct qdr_terminus_t          qdr_terminus_t;
typedef struct qdr_connection_info_t   qdr_connection_info_t;

/**
 ******************************************************************************
 * Protocol adaptor declaration macro
 ******************************************************************************
 */
/**
 * Callback to initialize a protocol adaptor at core thread startup
 *
 * @param core Pointer to the core object
 * @param adaptor_context [out] Returned adaptor context
 */
typedef void (*qdr_adaptor_init_t) (qdr_core_t *core, void **adaptor_context);


/**
 * Callback to finalize a protocol adaptor at core thread shutdown
 *
 * @param adaptor_context The context returned by the adaptor during the on_init call
 */
typedef void (*qdr_adaptor_final_t) (void *adaptor_context);


/**
 * Declaration of a protocol adaptor
 *
 * A protocol adaptor may declare itself by invoking the QDR_CORE_ADAPTOR_DECLARE macro in its body.
 *
 * @param name A null-terminated literal string naming the module
 * @param on_init Pointer to a function for adaptor initialization, called at core thread startup
 * @param on_final Pointer to a function for adaptor finalization, called at core thread shutdown
 */
#define QDR_CORE_ADAPTOR_DECLARE(name,on_init,on_final)      \
    static void adaptorstart() __attribute__((constructor)); \
    void adaptorstart() { qdr_register_adaptor(name, on_init, on_final); }
void qdr_register_adaptor(const char         *name,
                          qdr_adaptor_init_t  on_init,
                          qdr_adaptor_final_t on_final);


/**
 ******************************************************************************
 * Callback function definitions
 ******************************************************************************
 */

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
 * qdr_link_first_attach_t callback
 *
 * This function is invoked when the core requires that a new link be attached over a
 * connection.  Such a link is either being initiated from the core or is the propagation
 * of a link route from an originator somewhere in the network.
 *
 * @param context The context supplied when the callback was registered
 * @param conn The connection over which the first attach is to be sent
 * @param link The link object for the new link
 * @param source The source terminus for the attach
 * @param target The target terminus for the attach
 * @param ssn_class The session class to be used to allocate this link to a session
 */
typedef void (*qdr_link_first_attach_t) (void               *context,
                                         qdr_connection_t   *conn,
                                         qdr_link_t         *link,
                                         qdr_terminus_t     *source,
                                         qdr_terminus_t     *target,
                                         qd_session_class_t  ssn_class);

/**
 * qdr_link_second_attach_t callback
 *
 * This function is invoked when the core is responding to an incoming attach from an
 * external container.  The function must send the responding (second) attach to the
 * remote container to complete the attachment of the link.
 *
 * @param context The context supplied when the callback was registered
 * @param link The link being attached
 * @param source The source terminus for the attach
 * @param target The target terminus for the attach
 */
typedef void (*qdr_link_second_attach_t) (void           *context,
                                          qdr_link_t     *link,
                                          qdr_terminus_t *source,
                                          qdr_terminus_t *target);

/**
 * qdr_link_detach_t callback
 *
 * A DETACH performative must be sent for a link that is being closed or detached.
 *
 * @param context The context supplied when the callback was registered
 * @param link The link being detached
 * @param error Error record if the detach is the result of an error condition, null otherwise
 * @param first True if this is the first detach (i.e. initiated outbound), False if this is the
 *              the response to a remotely initiated detach
 * @param close True if this is a link close, False if this is a link detach
 */
typedef void (*qdr_link_detach_t) (void *context, qdr_link_t *link, qdr_error_t *error, bool first, bool close);

/**
 * qdr_link_flow_t callback
 *
 * Credit is being issued for an incoming link.  Credit is issued incrementally, being added
 * to credit may have been issued in the past.
 *
 * @param context The context supplied when the callback was registered
 * @param link The link for which credit is being issued
 * @param credit The number of new credits being issued to the link
 */
typedef void (*qdr_link_flow_t) (void *context, qdr_link_t *link, int credit);

/**
 * qdr_link_offer_t callback
 *
 * This function is invoked when the core wishes to inform the remote terminus of an outoing link
 * that it is willing and ready to transfer a certain number of deliveries over that link.
 *
 * @param context The context supplied when the callback was registered
 * @param link The link being affected
 * @param delivery_count The number of deliveries available to be sent over this link
 */
typedef void (*qdr_link_offer_t) (void *context, qdr_link_t *link, int delivery_count);

/**
 * qdr_link_drained_t callback
 *
 * This function is invoked when the core wishes to inform the remote terminus of an outgoing link
 * that it has drained its outgoing deliveries and removed any residual credit.
 *
 * @param context The context supplied when the callback was registered
 * @param link The link being affected
 */
typedef void (*qdr_link_drained_t) (void *context, qdr_link_t *link);

/**
 * qdr_link_drain_t callback
 *
 * This functino is invoked when the core wishes to inform the remote terminus of a link
 * that the drain mode of the link has changed.
 *
 * @param context The context supplied when the callback was registered
 * @param link The link being affected
 * @param mode True for enabling drain mode, False for disabling drain mode
 */
typedef void (*qdr_link_drain_t) (void *context, qdr_link_t *link, bool mode);

/**
 * qdr_link_push_t callback
 *
 * The core invokes this function when it wishes to transfer deliveries on an outgoing link.
 * This function, in turn, calls qdr_link_process_deliveries with the desired number of
 * deliveries (up to limit) that should be transferred from the core.  Typically, this
 * function will call qdr_link_process_deliveries with MIN(limit, available-credit).
 *
 * @param context The context supplied when the callback was registered
 * @param link The link over which deliveries should be transfered
 * @param limit The maximum number of deliveries that should be transferred
 * @return The number of deliveries transferred
 */
typedef int (*qdr_link_push_t) (void *context, qdr_link_t *link, int limit);

extern const uint64_t QD_DELIVERY_MOVED_TO_NEW_LINK;
/**
 * qdr_link_deliver_t callback
 *
 * This function is invoked by the core during the execution of qdr_link_process_deliveries.  There
 * is one invocation for each delivery to be transferred.  If this function returns a non-zero
 * disposition, the core will settle the delivery with that disposition back to the sender.
 *
 * @param context The context supplied when the callback was registered
 * @param link The link over which deliveries should be transfered
 * @param delivery The delivery (which contains the message) to be transferred
 * @param settled True iff the delivery is already settled
 * @return The disposition of the delivery to be sent back to the sender, or 0 if no disposition
 */
typedef uint64_t (*qdr_link_deliver_t) (void *context, qdr_link_t *link, qdr_delivery_t *delivery, bool settled);

/**
 * qdr_link_get_credit_t callback
 *
 * Query a link for the current amount of available credit.
 *
 * @param context The context supplied when the callback was registered
 * @param link The link being queried
 * @return The number of credits available on this link
 */
typedef int (*qdr_link_get_credit_t) (void *context, qdr_link_t *link);

/**
 * qdr_delivery_update_t callback
 *
 * This function is invoked by the core when a delivery's disposition and settlement are being
 * changed.  This fuction must send the updated delivery state to the remote terminus.
 *
 * @param context The context supplied when the callback was registered
 * @param dlv The delivery being updated
 * @param disp The new disposition for the delivery
 * @param settled True iff the delivery is being settled
 */
typedef void (*qdr_delivery_update_t) (void *context, qdr_delivery_t *dlv, uint64_t disp, bool settled);

/**
 * qdr_connection_close_t callback
 *
 * The core invokes this function when a connection to a remote container is to be closed.
 *
 * @param context The context supplied when the callback was registered
 * @param conn The connection being closed
 * @param error If the close is a result of an error, this is the error record to be used, else it's null
 */
typedef void (*qdr_connection_close_t) (void *context, qdr_connection_t *conn, qdr_error_t *error);

/**
 * qdr_connection_trace_t callback
 *
 * This callback is invoked when per-connection tracing is being turned on of off.  The new tracing
 * state must be propagated down into the tracing capabilities of the lower layers of connection processing.
 *
 * @param context The context supplied when the callback was registered
 * @param conn The connection being affected
 * @param trace True to enable tracing for this connection, False to disable tracing for this connection
 */
typedef void (*qdr_connection_trace_t) (void *context, qdr_connection_t *conn, bool trace);


/**
 ******************************************************************************
 * Protocol adaptor plugin functions
 ******************************************************************************
 */

/**
 * qdr_protocol_adaptor
 *
 * Register a new protocol adaptor with the router core.
 *
 * @param core Pointer to the core object
 * @param name The name of this adaptor's protocol
 * @param context The context to be used in all of the callbacks
 * @param callbacks Pointers to all of the callback functions used in the adaptor
 * @return Pointer to a protocol adaptor object
 */
qdr_protocol_adaptor_t *qdr_protocol_adaptor(qdr_core_t                *core,
                                             const char                *name,
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
                                             qdr_link_get_credit_t      get_credit,
                                             qdr_delivery_update_t      delivery_update,
                                             qdr_connection_close_t     conn_close,
                                             qdr_connection_trace_t     conn_trace);


/**
 * qdr_protocol_adaptor_free
 *
 * Free the resources used for a protocol adaptor.  This should be called during adaptor
 * finalization.
 * 
 * @param core Pointer to the core object
 * @param adaptor Pointer to a protocol adaptor object returned by qdr_protocol_adaptor
 */
void qdr_protocol_adaptor_free(qdr_core_t *core, qdr_protocol_adaptor_t *adaptor);


/**
 ******************************************************************************
 * Connection functions
 ******************************************************************************
 */

typedef enum {
    QD_CONN_OPER_UP,
    QD_CONN_OPER_DOWN,
} qd_conn_oper_status_t;


typedef enum {
    QD_CONN_ADMIN_ENABLED,
    QD_CONN_ADMIN_DELETED
} qd_conn_admin_status_t;


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

typedef void (*qdr_connection_bind_context_t) (qdr_connection_t *context, void *token);

/**
 * qdr_connection_opened
 *
 * This function must be called once for every connection that is opened in the router.
 * Once a new connection has been both remotely and locally opened, the core must be notified.
 *
 * @param core Pointer to the core object
 * @param protocol_adaptor Pointer to the protocol adaptor handling the connection
 * @param incoming True iff this connection is associated with a listener, False if a connector
 * @param role The configured role of this connection
 * @param cost If the role is inter_router, this is the configured cost for the connection.
 * @param management_id - A unique identifier that is used in management and logging operations.
 * @param label Optional label provided in the connection's configuration.  This is used to 
 *        correlate the connection with waypoints and link-route destinations that use the connection.
 * @param strip_annotations_in True if configured to remove annotations on inbound messages.
 * @param strip_annotations_out True if configured to remove annotations on outbound messages.
 * @param policy_allow_dynamic_link_routes True if this connection is allowed by policy to create link route destinations.
 * @param policy_allow_admin_status_update True if this connection is allowed to modify admin_status on other connections.
 * @param link_capacity The capacity, in deliveries, for links in this connection.
 * @param vhost If non-null, this is the vhost of the connection to be used for multi-tenancy.
 * @return Pointer to a connection object that can be used to refer to this connection over its lifetime.
 */
qdr_connection_t *qdr_connection_opened(qdr_core_t                    *core,
                                        qdr_protocol_adaptor_t        *protocol_adaptor,
                                        bool                           incoming,
                                        qdr_connection_role_t          role,
                                        int                            cost,
                                        uint64_t                       management_id,
                                        const char                    *label,
                                        const char                    *remote_container_id,
                                        bool                           strip_annotations_in,
                                        bool                           strip_annotations_out,
                                        int                            link_capacity,
                                        const char                    *vhost,
                                        const qd_policy_spec_t        *policy_spec,
                                        qdr_connection_info_t         *connection_info,
                                        qdr_connection_bind_context_t  context_binder,
                                        void                          *bind_token);

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
 * qdr_core_close_connection
 *
 * This function is called when a connection is closed, usually by a management request.
 * Initiates a core thread action that quite simply sets the closed flag on the passed in connection object
 * and activates the connection. The qdr_connection_process() further processes this connection and calls
 * back the appropriate protocol adaptor's conn_close_handler, where the io thread can further perform
 * any appropriate cleanup.
 *
 * @param qdr_connection_t *conn - the connection that needs to be closed. The pointer returned by qdr_connection_opened
 */
void qdr_core_close_connection(qdr_connection_t *conn);

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
 * qdr_connection_role
 *
 * Retrieve the role of the connection object.
 */
qdr_connection_role_t qdr_connection_role(const qdr_connection_t *conn);


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
 * qdr_terminus_set_dynamic
 *
 * Set this terminus to be dynamic.
 *
 * @param term A qdr_terminus pointer returned by qdr_terminus()
 */
void qdr_terminus_set_dynamic(qdr_terminus_t *term);

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
 * @param no_route If true, new deliveries are not to be routed to this link
 * @param initial_delivery (optional) Move this delivery from its existing link to the head of this link's buffer
 * @param link_id - set to the management id of the new link
 * @return A pointer to a new qdr_link_t object to track the link
 */
qdr_link_t *qdr_link_first_attach(qdr_connection_t *conn,
                                  qd_direction_t    dir,
                                  qdr_terminus_t   *source,
                                  qdr_terminus_t   *target,
                                  const char       *name,
                                  const char       *terminus_addr,
                                  bool              no_route,
                                  qdr_delivery_t   *initial_delivery,
                                  uint64_t         *link_id);

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
 * @param remote_disposition as set by sender on the transfer
 * @param remote_disposition_state as set by sender on the transfer
 * @return Pointer to the qdr_delivery that will track the lifecycle of this delivery on this link.
 */
qdr_delivery_t *qdr_link_deliver(qdr_link_t *link, qd_message_t *msg, qd_iterator_t *ingress,
                                 bool settled, qd_bitmask_t *link_exclusion, int ingress_index,
                                 uint64_t remote_disposition,
                                 qd_delivery_state_t *remote_state);
qdr_delivery_t *qdr_link_deliver_to(qdr_link_t *link, qd_message_t *msg,
                                    qd_iterator_t *ingress, qd_iterator_t *addr,
                                    bool settled, qd_bitmask_t *link_exclusion, int ingress_index,
                                    uint64_t remote_disposition,
                                    qd_delivery_state_t *remote_state);
qdr_delivery_t *qdr_link_deliver_to_routed_link(qdr_link_t *link, qd_message_t *msg, bool settled,
                                                const uint8_t *tag, int tag_length,
                                                uint64_t remote_disposition,
                                                qd_delivery_state_t *remote_state);

/**
 * qdr_link_process_deliveries
 *
 * This function is called by the protocol adaptor in the context of the link_push
 * callback.  It provides the core module access to the IO thread so the core can
 * deliver outgoing messages to the adaptor.
 *
 * @param core Pointer to the router core object
 * @param link Pointer to the link being processed
 * @param credit The maximum number of deliveries to be processed on this link
 * @return The number of deliveries that were completed during the processing
 */
int qdr_link_process_deliveries(qdr_core_t *core, qdr_link_t *link, int credit);


/**
 * qdr_link_complete_sent_message
 *
 * If an outgoing message is completed outside of the context of the link_deliver callback,
 * this function must be called to inform the router core that the delivery on the head of
 * the link's undelivered list can be moved out of that list.  Ensure that the send-complete
 * status of the message has been set before calling this function.  This function will check
 * the send-complete status of the head delivery on the link's undelivered list.  If it is
 * true, that delivery will be removed from the undelivered list.
 *
 * DO NOT call this function from within the link_deliver callback.  Use it only if you must
 * asynchronously complete the sending of the current message.
 *
 * This will typically occur when a message delivered to the protcol adaptor cannot be sent
 * on the wire due to back-pressure.  In this case, the removal of the back pressure is the
 * stimulus for completing the send of the message.
 *
 * @param core Pointer to the router core object
 * @param link Pointer to the link on which the head delivery has been completed
 */
void qdr_link_complete_sent_message(qdr_core_t *core, qdr_link_t *link);


void qdr_link_flow(qdr_core_t *core, qdr_link_t *link, int credit, bool drain_mode);

/**
 * Sets the link's drain flag to false and sets credit to core to zero.
 * The passed in link has been drained and hence no longer in drain mode.
 * Call this right after calling pn_link_drained
 *
 * @param core - router core
 * @param link - the link that has been drained
 */
void qdr_link_set_drained(qdr_core_t *core, qdr_link_t *link);

/**
 * Extract the disposition and delivery state data that is to be sent to the
 * remote endpoint via the delivery. Caller takes ownership of the returned
 * delivery_state and must free it when done.
 */
qd_delivery_state_t *qdr_delivery_take_local_delivery_state(qdr_delivery_t *dlv, uint64_t *dispo);


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
                                           bool             ssl,
                                           const char      *version,
                                           bool             streaming_links);

#endif
