#ifndef __router_private_h__
#define __router_private_h__ 1
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

/**@file
 * Router Private type definitions
 *@internal
 */

#include "dispatch_private.h"
#include "entity.h"
#include "parse_tree.h"

#include "qpid/dispatch/bitmask.h"
#include "qpid/dispatch/enum.h"
#include "qpid/dispatch/hash.h"
#include "qpid/dispatch/log.h"
#include "qpid/dispatch/message.h"
#include "qpid/dispatch/router.h"
#include "qpid/dispatch/router_core.h"
#include "qpid/dispatch/trace_mask.h"

qd_error_t qd_router_python_setup(qd_router_t *router);
void qd_router_python_free(qd_router_t *router);
qd_error_t qd_pyrouter_tick(qd_router_t *router);
qd_error_t qd_router_configure_address(qd_router_t *router, qd_entity_t *entity);
qd_error_t qd_router_configure_link_route(qd_router_t *router, qd_entity_t *entity);
qd_error_t qd_router_configure_auto_link(qd_router_t *router, qd_entity_t *entity);
qd_error_t qd_router_configure_exchange(qd_router_t *router, qd_entity_t *entity);
qd_error_t qd_router_configure_binding(qd_router_t *router, qd_entity_t *entity);

void qd_router_configure_free(qd_router_t *router);

extern const char *QD_ROUTER_NODE_TYPE;
extern const char *QD_ROUTER_ADDRESS_TYPE;
extern const char *QD_ROUTER_LINK_TYPE;


struct qd_router_t {
    qd_dispatch_t            *qd;
    qdr_core_t               *router_core;
    qd_tracemask_t           *tracemask;
    qd_log_source_t          *log_source;
    qd_router_mode_t          router_mode;
    const char               *router_area;
    const char               *router_id;
    qd_node_t                *node;

    sys_mutex_t              *lock;
    qd_timer_t               *timer;

    //
    // Store the "radius" of the current network topology.  This is defined as the
    // distance in hops (not cost) from the local router to the most distant known
    // router in the topology.
    //
    int topology_radius;
};

#endif
