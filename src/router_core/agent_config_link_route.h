#ifndef qdr_agent_config_link_route
#define qdr_agent_config_link_route 1
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

void qdra_config_link_route_get_first_CT(qdr_core_t *core, qdr_query_t *query, int offset);
void qdra_config_link_route_get_next_CT(qdr_core_t *core, qdr_query_t *query);
void qdra_config_link_route_create_CT(qdr_core_t *core, qd_iterator_t *name, qdr_query_t *query, qd_parsed_field_t *in_body);
void qdra_config_link_route_update_CT(qdr_core_t *core, qdr_query_t *query, qd_parsed_field_t *in_body);
void qdra_config_link_route_delete_CT(qdr_core_t *core, qdr_query_t *query, qd_iterator_t *name,
                                qd_iterator_t *identity);
void qdra_config_link_route_get_CT(qdr_core_t    *core,
                                   qd_iterator_t *name,
                                   qd_iterator_t *identity,
                                   qdr_query_t   *query,
                                   const char    *qdr_config_link_route_columns[]);
const char *qdra_link_route_direction_CT(qd_parsed_field_t *field, qd_direction_t *dir);

#define QDR_CONFIG_LINK_ROUTE_COLUMN_COUNT 13

extern const char *qdr_config_link_route_columns[QDR_CONFIG_LINK_ROUTE_COLUMN_COUNT + 1];

#endif
