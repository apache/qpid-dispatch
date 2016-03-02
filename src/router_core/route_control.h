#ifndef qd_router_core_route_control
#define qd_router_core_route_control 1
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

#include "router_core_private.h"

const char *qdr_route_create_CT(qdr_core_t             *core,
                                qd_field_iterator_t    *name,
                                qdr_route_path_t        path,
                                qd_address_treatment_t  treatment,
                                qd_parsed_field_t      *addr_field,
                                qd_parsed_field_t      *route_addr_field,
                                qdr_route_config_t    **route);

void qdr_route_delete_CT(qdr_route_config_t *route);

void qdr_route_connection_add_CT(qdr_route_config_t *route,
                                 qd_parsed_field_t  *conn_id,
                                 bool                is_container);

void qdr_route_connection_delete_CT(qdr_route_config_t *route,
                                    qd_parsed_field_t  *conn_id,
                                    bool                is_container);

void qdr_route_connection_kill_CT(qdr_route_config_t *route,
                                  qd_parsed_field_t  *conn_id,
                                  bool                is_container);

void qdr_route_connection_opened_CT(qdr_core_t *core, qdr_connection_t *conn);

void qdr_route_connection_closed_CT(qdr_core_t *core, qdr_connection_t *conn);

#endif
