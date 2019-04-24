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
                                              const char             *addr_pattern,
                                              bool                    is_prefix,
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
                                            qd_parsed_field_t   *container_field,
                                            qd_parsed_field_t   *connection_field,
                                            qd_parsed_field_t   *external_addr,
                                            bool                 fallback);

void qdr_route_del_auto_link_CT(qdr_core_t *core, qdr_auto_link_t *auto_link);

void qdr_route_connection_opened_CT(qdr_core_t       *core,
                                    qdr_connection_t *conn,
                                    qdr_field_t      *container_field,
                                    qdr_field_t      *connection_field);

void qdr_route_connection_closed_CT(qdr_core_t *core, qdr_connection_t *conn);

void qdr_link_route_map_pattern_CT(qdr_core_t *core, qd_iterator_t *address, qdr_address_t *addr);
void qdr_link_route_unmap_pattern_CT(qdr_core_t *core, qd_iterator_t *address);
void qdr_route_check_id_for_deletion_CT(qdr_core_t *core, qdr_conn_identifier_t *cid);

/**
 * Actions to be performed when an auto link detaches.
 * Retries to establishe an auto link that is associated with the passed in link.
 * Uses the core thread timer API to schedule an auto link retry.
 *
 * @param core Pointer to the core object returned by qd_core()
 * @param link qdr_link_t reference. The attach on this link for an auto link was rejected.
 */
void qdr_route_auto_link_detached_CT(qdr_core_t *core, qdr_link_t *link);

/**
 * Performs actions that need to be taken when an auto link is closed.
 * For example, if a timer was setup to reconnect the autolink, it needs to be canceled.
 * @param link qdr_link_t reference.
 */
void qdr_route_auto_link_closed_CT(qdr_core_t *core, qdr_link_t *link);

// Connection scoped link routes:
qdr_link_route_t *qdr_route_add_conn_route_CT(qdr_core_t       *core,
                                              qdr_connection_t *conn,
                                              qd_iterator_t    *name,
                                              const char       *addr_pattern,
                                              qd_direction_t    dir);

void qdr_route_del_conn_route_CT(qdr_core_t       *core,
                                 qdr_link_route_t *lr);
#endif
