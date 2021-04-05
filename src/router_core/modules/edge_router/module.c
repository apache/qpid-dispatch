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

#include "module.h"

#include "addr_proxy.h"
#include "connection_manager.h"
#include "edge_mgmt.h"
#include "link_route_proxy.h"


typedef struct {
    qdr_core_t            *core;
    qcm_edge_conn_mgr_t   *conn_mgr;
    qcm_edge_addr_proxy_t *addr_proxy;
    // TODO - Add pointers to other edge-router state here
} qcm_edge_t;


static bool qcm_edge_router_enable_CT(qdr_core_t *core)
{
    return core->router_mode == QD_ROUTER_MODE_EDGE;
}


static void qcm_edge_router_init_CT(qdr_core_t *core, void **module_context)
{
    qcm_edge_t *edge = NEW(qcm_edge_t);
    edge->core = core;
    edge->conn_mgr   = qcm_edge_conn_mgr(core);
    edge->addr_proxy = qcm_edge_addr_proxy(core);
    qcm_edge_mgmt_init_CT(core);
    qcm_edge_link_route_init_CT(core);
    // TODO - Add initialization of other edge-router functions here
    *module_context = edge;
}


static void qcm_edge_router_final_CT(void *module_context)
{
    qcm_edge_t *edge = (qcm_edge_t*) module_context;

    qcm_edge_conn_mgr_final(edge->conn_mgr);
    qcm_edge_addr_proxy_final(edge->addr_proxy);
    qcm_edge_mgmt_final_CT(edge->core);
    qcm_edge_link_route_final_CT(edge->core);
    // TODO - Add finalization of other edge-router functions here
    free(edge);
}


QDR_CORE_MODULE_DECLARE("edge_router", qcm_edge_router_enable_CT, qcm_edge_router_init_CT, qcm_edge_router_final_CT)
