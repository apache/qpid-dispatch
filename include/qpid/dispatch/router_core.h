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

typedef struct qd_router_core_t qd_router_core_t;

/**
 * Allocate and start an instance of the router core module.
 */
qd_router_core_t *qd_router_core(void);

/**
 * Stop and deallocate an instance of the router core.
 */
void qd_router_core_free(qd_router_core_t *rt);

/**
 * Route table maintenance functions
 */
void qd_router_core_add_router(qd_router_core_t *rt, const char *address, int router_maskbit);
void qd_router_core_del_router(qd_router_core_t *rt, int router_maskbit);
void qd_router_core_set_link(qd_router_core_t *rt, int router_maskbit, int link_maskbit);
void qd_router_core_remove_link(qd_router_core_t *rt, int router_maskbit);
void qd_router_core_set_next_hop(qd_router_core_t *rt, int router_maskbit, int nh_router_maskbit);
void qd_router_core_remove_next_hop(qd_router_core_t *rt, int router_maskbit);
//void qd_router_core_set_valid_origins(qd_router_core_t *rt, ???);
void qd_router_core_map_destination(qd_router_core_t *rt, int router_maskbit, const char *address, char phase);
void qd_router_core_unmap_destination(qd_router_core_t *rt, int router_maskbit, const char *address, char phase);

/**
 * Link attach and detach functions
 */
void qd_router_core_attach(qd_router_core_t *rt, qd_direction_t dir, qd_link_t *link);
void qd_router_core_detach(qd_router_core_t *rt, qd_link_t *link);

/**
 * Delivery functions
 */
void qd_router_core_delivery(qd_router_core_t *rt, qd_link_t *in_link, pn_delivery_t *delivery, qd_message_t *msg,
                               qd_field_iterator_t *effective_address);


/**
 * Management instrumentation functions
 */
typedef enum {
    QD_ROUTER_ADDRESS,
    QD_ROUTER_LINK
} qd_router_entity_type_t;

void qd_router_core_query(qd_router_core_t *rt, qd_router_entity_type_t type, const char *filter, void *context);

#endif
