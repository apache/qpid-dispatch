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

#include "server_private.h"
#include "config_private.h"
#include <qpid/dispatch/ctools.h>

typedef struct qd_container_t qd_container_t;
typedef struct qd_router_t    qd_router_t;
typedef struct qd_agent_t     qd_agent_t;

typedef struct qd_config_listener_t {
    DEQ_LINKS(struct qd_config_listener_t);
    qd_listener_t      *listener;
    qd_server_config_t  configuration;
} qd_config_listener_t;

DEQ_DECLARE(qd_config_listener_t, qd_config_listener_list_t);
ALLOC_DECLARE(qd_config_listener_t);


typedef struct qd_config_connector_t {
    DEQ_LINKS(struct qd_config_connector_t);
    qd_connector_t     *connector;
    qd_server_config_t  configuration;
} qd_config_connector_t;

DEQ_DECLARE(qd_config_connector_t, qd_config_connector_list_t);
ALLOC_DECLARE(qd_config_connector_t);

struct qd_dispatch_t {
    qd_server_t        *server;
    qd_container_t     *container;
    qd_router_t        *router;
    qd_agent_t         *agent;
    qd_config_t        *config;

    qd_config_listener_list_t   config_listeners;
    qd_config_connector_list_t  config_connectors;
};

#endif

