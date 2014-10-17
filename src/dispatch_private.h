#ifndef __dispatch_private_h__
#define __dispatch_private_h__
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

//
// The following declarations are for types that are shared between modules yet are
// not in the public API.
//
typedef struct qd_server_t          qd_server_t;
typedef struct qd_container_t       qd_container_t;
typedef struct qd_router_t          qd_router_t;
typedef struct qd_agent_t           qd_agent_t;
typedef struct qd_waypoint_t        qd_waypoint_t;
typedef struct qd_router_link_t     qd_router_link_t;
typedef struct qd_router_node_t     qd_router_node_t;
typedef struct qd_router_ref_t      qd_router_ref_t;
typedef struct qd_router_link_ref_t qd_router_link_ref_t;
typedef struct qd_router_conn_t     qd_router_conn_t;
typedef struct qd_config_phase_t    qd_config_phase_t;
typedef struct qd_config_address_t  qd_config_address_t;

#include <qpid/dispatch/container.h>
#include <qpid/dispatch/router.h>
#include <qpid/dispatch/connection_manager.h>
#include "server_private.h"
#include "router_private.h"

struct qd_dispatch_t {
    qd_server_t             *server;
    qd_container_t          *container;
    qd_router_t             *router;
    qd_agent_t              *agent;
    void                    *py_agent;
    qd_connection_manager_t *connection_manager;

    int    thread_count;
    char  *container_name;
    char  *router_area;
    char  *router_id;
    qd_router_mode_t  router_mode;

    qd_log_source_t *log_source;
};

void qd_dispatch_set_agent(qd_dispatch_t *qd, void *agent);

#endif
