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

typedef struct qdr_core_t qdr_core_t;
typedef struct qdr_link_t qdr_link_t;
typedef struct qdr_delivery_t qdr_delivery_t;

/**
 * Allocate and start an instance of the router core module.
 */
qdr_core_t *qdr_core(void);

/**
 * Stop and deallocate an instance of the router core.
 */
void qdr_core_free(qdr_core_t *rt);

/**
 * Route table maintenance functions
 */
void qdr_core_add_router(qdr_core_t *rt, const char *address, int router_maskbit);
void qdr_core_del_router(qdr_core_t *rt, int router_maskbit);
void qdr_core_set_link(qdr_core_t *rt, int router_maskbit, int link_maskbit);
void qdr_core_remove_link(qdr_core_t *rt, int router_maskbit);
void qdr_core_set_next_hop(qdr_core_t *rt, int router_maskbit, int nh_router_maskbit);
void qdr_core_remove_next_hop(qdr_core_t *rt, int router_maskbit);
void qdr_core_set_valid_origins(qdr_core_t *rt, const qd_bitmask_t *routers);
void qdr_core_map_destination(qdr_core_t *rt, int router_maskbit, const char *address, char phase);
void qdr_core_unmap_destination(qdr_core_t *rt, int router_maskbit, const char *address, char phase);

typedef void (*qdr_mobile_added_t)   (void *context, qd_field_iterator_t *address);
typedef void (*qdr_mobile_removed_t) (void *context, qd_field_iterator_t *address);
typedef void (*qdr_link_lost_t)      (void *context, int link_maskbit);

void qdr_core_route_table_handlers(void                 *context,
                                   qdr_mobile_added_t    mobile_added,
                                   qdr_mobile_removed_t  mobile_removed,
                                   qdr_link_lost_t       link_lost);

/**
 * Link functions
 */
qdr_link_t *qdr_link(qdr_core_t *rt, qd_direction_t dir, bool dynamic, const char *address);
void qdr_link_detach(qdr_link_t *link, qd_link_t *link);

/**
 * Delivery functions
 */
void qdr_core_delivery(qdr_core_t *rt, qd_link_t *in_link, pn_delivery_t *delivery, qd_message_t *msg,
                       qd_field_iterator_t *effective_address);


/**
 * Management instrumentation functions
 */
typedef enum {
    QD_ROUTER_ADDRESS,
    QD_ROUTER_LINK
} qd_router_entity_type_t;

void qdr_core_query(qdr_core_t *rt, qd_router_entity_type_t type, const char *filter, void *context);

#endif
