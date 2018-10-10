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
#include "connection_manager.h"

typedef struct {
    qdrcm_edge_conn_mgr_t *conn_mgr;
    // TODO - Add pointers to other edge-router state here
} qdrcm_edge_t;


static void qdrcm_edge_router_init_CT(qdr_core_t *core, void **module_context)
{
    if (core->router_mode == QD_ROUTER_MODE_EDGE) {
        qdrcm_edge_t *edge = NEW(qdrcm_edge_t);
        edge->conn_mgr = qdrcm_edge_conn_mgr(core);
        // TODO - Add initialization of other edge-router functions here
        *module_context = edge;
    } else
        *module_context = 0;
}


static void qdrcm_edge_router_final_CT(void *module_context)
{
    qdrcm_edge_t *edge = (qdrcm_edge_t*) module_context;

    if (edge) {
        qdrcm_edge_conn_mgr_final(edge->conn_mgr);
        // TODO - Add finalization of other edge-router functions here
        free(edge);
    }
}


QDR_CORE_MODULE_DECLARE("edge_router", qdrcm_edge_router_init_CT, qdrcm_edge_router_final_CT)
