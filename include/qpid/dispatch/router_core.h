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

#include <qpid/dispatch.h>
#include <qpid/dispatch/bitmask.h>

//
// All callbacks in this module shall be invoked on a connection thread from the server thread pool.
//

typedef struct qdr_core_t qdr_core_t;
typedef struct qdr_connection_t qdr_connection_t;
typedef struct qdr_link_t qdr_link_t;
typedef struct qdr_delivery_t qdr_delivery_t;

/**
 * Allocate and start an instance of the router core module.
 */
qdr_core_t *qdr_core(void);

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
void qdr_core_set_valid_origins(qdr_core_t *core, const qd_bitmask_t *routers);
void qdr_core_map_destination(qdr_core_t *core, int router_maskbit, const char *address, char phase);
void qdr_core_unmap_destination(qdr_core_t *core, int router_maskbit, const char *address, char phase);

typedef void (*qdr_mobile_added_t)   (void *context, const char *address);
typedef void (*qdr_mobile_removed_t) (void *context, const char *address);
typedef void (*qdr_link_lost_t)      (void *context, int link_maskbit);

void qdr_core_route_table_handlers(void                 *context,
                                   qdr_mobile_added_t    mobile_added,
                                   qdr_mobile_removed_t  mobile_removed,
                                   qdr_link_lost_t       link_lost);

/**
 ******************************************************************************
 * In-process message-receiver functions
 ******************************************************************************
 */
typedef void (*qdr_receive_t) (void *context, qd_message_t *msg, int link_maskbit);

void qdr_core_subscribe(qdr_core_t *core, const char *address, bool local, bool mobile, qdr_receive_t on_message, void *context);


/**
 ******************************************************************************
 * Connection functions
 ******************************************************************************
 */
typedef enum {
    QDR_WORK_FIRST_ATTACH,  // Core is initiating a first-attach
    QDR_WORK_SECOND_ATTACH, // Core is sending a second-attach
    QDR_WORK_DETACH,        // Core is sending a detach
    QDR_WORK_DELIVERY       // Core is updating a delivery for in-thread processing
} qdr_work_type_t;

typedef struct {
    qdr_work_type_t  work_type;
    pn_terminus_t   *source;   // For FIRST_ATTACH
    pn_terminus_t   *target;   // For FIRST_ATTACH
    qdr_link_t      *link;     // For SECOND_ATTACH, DETACH
    qdr_delivery_t  *delivery; // For DELIVERY
} qdr_work_t;

qdr_connection_t *qdr_connection_opened(qdr_core_t *core, qd_field_iterator_t *label);
void qdr_connection_closed(qdr_connection_t *conn);
void qdr_connection_set_context(qdr_connection_t *conn, void *context);
void *qdr_connection_get_context(qdr_connection_t *conn);
qdr_work_t *qdr_connection_work(qdr_connection_t *conn);

typedef void (*qdr_connection_activate_t) (void *context, const qdr_connection_t *connection);
void qdr_connection_activate_handler(qdr_core_t *core, qdr_connection_activate_t handler, void *context);

/**
 ******************************************************************************
 * Link functions
 ******************************************************************************
 */
qdr_link_t *qdr_link_first_attach(qdr_connection_t *conn, qd_direction_t dir, pn_terminus_t *source, pn_terminus_t *target);
void qdr_link_second_attach(qdr_link_t *link, pn_terminus_t *source, pn_terminus_t *target);
void qdr_link_detach(qdr_link_t *link, pn_condition_t *condition);

qdr_delivery_t *qdr_link_deliver(qdr_link_t *link, pn_delivery_t *delivery, qd_message_t *msg);
qdr_delivery_t *qdr_link_deliver_to(qdr_link_t *link, pn_delivery_t *delivery, qd_message_t *msg, qd_field_iterator_t *addr);


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

qdr_query_t *qdr_manage_get_first(qdr_core_t *core, void *context, qd_router_entity_type_t type, int offset);
void qdr_manage_get_next(qdr_query_t *query);
void qdr_query_cancel(qdr_query_t *query);

typedef void (*qdr_manage_response_t) (void *context, int status_code, qd_composed_field_t *body);
void qdr_manage_handler(qdr_core_t *core, qdr_manage_response_t response_handler);

#endif
