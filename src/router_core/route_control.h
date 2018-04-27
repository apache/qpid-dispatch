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

qdr_link_route_t *qdr_route_add_link_route_CT(qdr_core_t             *core,
                                              qd_iterator_t          *name,
                                              qd_parsed_field_t      *prefix_field,
                                              qd_parsed_field_t      *pattern_field,
                                              qd_parsed_field_t      *add_prefix_field,
                                              qd_parsed_field_t      *del_prefix_field,
                                              qd_parsed_field_t      *container_field,
                                              qd_parsed_field_t      *connection_field,
                                              qd_address_treatment_t  treatment,
                                              qd_direction_t          dir);

void qdr_route_del_link_route_CT(qdr_core_t *core, qdr_link_route_t *lr);

qdr_auto_link_t *qdr_route_add_auto_link_CT(qdr_core_t          *core,
                                            qd_iterator_t       *name,
                                            qd_parsed_field_t   *addr_field,
                                            qd_direction_t       dir,
                                            int                  phase,
                                            qd_parsed_field_t      *container_field,
                                            qd_parsed_field_t      *connection_field,
                                            qd_parsed_field_t   *external_addr);

void qdr_route_del_auto_link_CT(qdr_core_t *core, qdr_auto_link_t *auto_link);

void qdr_route_connection_opened_CT(qdr_core_t       *core,
                                    qdr_connection_t *conn,
                                    qdr_field_t      *container_field,
                                    qdr_field_t      *connection_field);

void qdr_route_connection_closed_CT(qdr_core_t *core, qdr_connection_t *conn);

void qdr_link_route_map_pattern_CT(qdr_core_t *core, qd_iterator_t *address, qdr_address_t *addr);
void qdr_link_route_unmap_pattern_CT(qdr_core_t *core, qd_iterator_t *address);

#endif
