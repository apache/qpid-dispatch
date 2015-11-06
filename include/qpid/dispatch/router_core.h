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

typedef struct qdr_core_t qdr_core_t;
typedef struct qdr_connection_t qdr_connection_t;
typedef struct qdr_link_t qdr_link_t;
typedef struct qdr_delivery_t qdr_delivery_t;

/**
 * Allocate and start an instance of the router core module.
 */
qdr_core_t *qdr_core(qd_dispatch_t *qd);

/**
 * Stop and deallocate an instance of the router core.
 */
void qdr_core_free(qdr_core_t *core);

/**
 ******************************************************************************
 * Route table maintenance functions
 ******************************************************************************
 */
void qdr_core_add_router(qdr_core_t *core, const char *address, int router_maskbit);
void qdr_core_del_router(qdr_core_t *core, int router_maskbit);
void qdr_core_set_link(qdr_core_t *core, int router_maskbit, int link_maskbit);
void qdr_core_remove_link(qdr_core_t *core, int router_maskbit);
void qdr_core_set_next_hop(qdr_core_t *core, int router_maskbit, int nh_router_maskbit);
void qdr_core_remove_next_hop(qdr_core_t *core, int router_maskbit);
void qdr_core_set_valid_origins(qdr_core_t *core, int router_maskbit, qd_bitmask_t *routers);
void qdr_core_map_destination(qdr_core_t *core, int router_maskbit, const char *address, char aclass, char phase, qd_address_semantics_t sem);
void qdr_core_unmap_destination(qdr_core_t *core, int router_maskbit, const char *address, char aclass, char phase);

typedef void (*qdr_mobile_added_t)   (void *context, const char *address);
typedef void (*qdr_mobile_removed_t) (void *context, const char *address);
typedef void (*qdr_link_lost_t)      (void *context, int link_maskbit);

void qdr_core_route_table_handlers(qdr_core_t           *core, 
                                   void                 *context,
                                   qdr_mobile_added_t    mobile_added,
                                   qdr_mobile_removed_t  mobile_removed,
                                   qdr_link_lost_t       link_lost);

/**
 ******************************************************************************
 * In-process message-receiver functions
 ******************************************************************************
 */
typedef void (*qdr_receive_t) (void *context, qd_message_t *msg, int link_maskbit);

void qdr_core_subscribe(qdr_core_t *core, const char *address, char aclass, char phase,
                        qd_address_semantics_t sem, qdr_receive_t on_message, void *context);


/**
 ******************************************************************************
 * Connection functions
 ******************************************************************************
 */

/**
 * qdr_connection_opened
 *
 * This function must be called once for every connection that is opened in the router.
 * Once a new connection has been both remotely and locally opened, the core must be notified.
 *
 * @param core Pointer to the core object
 * @param label Optional label provided in the connection's configuration.  This is used to 
 *        correlate the connection with waypoints and link-route destinations that use the connection.
 * @return Pointer to a connection object that can be used to refer to this connection over its lifetime.
 */
qdr_connection_t *qdr_connection_opened(qdr_core_t *core, const char *label);

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
void *qdr_connection_get_context(qdr_connection_t *conn);

/**
 * qdr_connection_activate_t callback
 *
 * Activate a connection for transmission (socket write).  This is called whenever
 * the core has deliveries on links, disposition updates on deliveries, or flow updates
 * to be sent across the connection.
 *
 * @param context The context supplied when the callback was registered
 * @param conn The connection object to be activated
 */
typedef void (*qdr_connection_activate_t) (void *context, qdr_connection_t *conn);

/**
 ******************************************************************************
 * Link functions
 ******************************************************************************
 */

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
qdr_link_t *qdr_link_first_attach(qdr_connection_t *conn, qd_direction_t dir, pn_terminus_t *source, pn_terminus_t *target);

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
void qdr_link_second_attach(qdr_link_t *link, pn_terminus_t *source, pn_terminus_t *target);

/**
 * qdr_link_detach
 *
 * This function is invoked when a link detach arrives.
 *
 * @param link The link pointer returned by qdr_link_first_attach or in a FIRST_ATTACH event.
 * @param condition The link condition from the detach frame.
 */
void qdr_link_detach(qdr_link_t *link, pn_condition_t *condition);

qdr_delivery_t *qdr_link_deliver(qdr_link_t *link, pn_delivery_t *delivery, qd_message_t *msg);
qdr_delivery_t *qdr_link_deliver_to(qdr_link_t *link, pn_delivery_t *delivery, qd_message_t *msg, qd_field_iterator_t *addr);

typedef void (*qdr_link_first_attach_t)  (void *context, qdr_connection_t *conn, qd_direction_t dir, pn_terminus_t *source, pn_terminus_t *target);
typedef void (*qdr_link_second_attach_t) (void *context, qdr_link_t *link, pn_terminus_t *source, pn_terminus_t *target);
typedef void (*qdr_link_detach_t)        (void *context, qdr_link_t *link, pn_condition_t *condition);

/**
 ******************************************************************************
 * Delivery functions
 ******************************************************************************
 */
void qdr_delivery_update_disposition(qdr_delivery_t *delivery);
void qdr_delivery_update_flow(qdr_delivery_t *delivery);
void qdr_delivery_process(qdr_delivery_t *delivery);

/**
 ******************************************************************************
 * Management functions
 ******************************************************************************
 */
typedef enum {
    QD_ROUTER_CONNECTION,
    QD_ROUTER_LINK,
    QD_ROUTER_ADDRESS,
    QD_ROUTER_WAYPOINT,
    QD_ROUTER_EXCHANGE,
    QD_ROUTER_BINDING
} qd_router_entity_type_t;

typedef struct qdr_query_t qdr_query_t;

void qdr_manage_create(qdr_core_t *core, void *context, qd_router_entity_type_t type, qd_parsed_field_t *attributes);
void qdr_manage_delete(qdr_core_t *core, void *context, qd_router_entity_type_t type, qd_parsed_field_t *attributes);
void qdr_manage_read(qdr_core_t *core, void *context, qd_router_entity_type_t type, qd_parsed_field_t *attributes);

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
