#ifndef router_core_edge_link_routes_h
#define router_core_edge_link_routes_h 1
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
// Manages link route configuration proxies created on the edge's interior
// router
//

#include "router_core_private.h"

void qcm_edge_link_route_init_CT(qdr_core_t *core);
void qcm_edge_link_route_final_CT(qdr_core_t *core);
void qcm_edge_link_route_proxy_flow_CT(qdr_core_t *core, int available_credit, bool drain);
void qcm_edge_link_route_proxy_state_CT(qdr_core_t *core, bool active);
#endif
