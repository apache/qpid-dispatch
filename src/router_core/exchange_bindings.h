#ifndef EXCHANGE_BINDINGS_H
#define EXCHANGE_BINDINGS_H 1
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

#define QDR_CONFIG_EXCHANGE_COLUMN_COUNT 8
#define QDR_CONFIG_BINDING_COLUMN_COUNT  7

extern const char *qdr_config_exchange_columns[];
extern const char *qdr_config_binding_columns[];

int qdr_forward_exchange_CT(qdr_core_t     *core,
                            qdr_address_t  *addr,
                            qd_message_t   *msg,
                            qdr_delivery_t *in_delivery,
                            bool            exclude_inprocess,
                            bool            control);
void qdra_config_exchange_create_CT(qdr_core_t         *core,
                                    qd_iterator_t      *name,
                                    qdr_query_t        *query,
                                    qd_parsed_field_t  *in_body);
void qdra_config_exchange_delete_CT(qdr_core_t    *core,
                                    qdr_query_t   *query,
                                    qd_iterator_t *name,
                                    qd_iterator_t *identity);
void qdra_config_exchange_get_CT(qdr_core_t    *core,
                                 qd_iterator_t *name,
                                 qd_iterator_t *identity,
                                 qdr_query_t   *query,
                                 const char    *columns[]);
void qdra_config_exchange_get_first_CT(qdr_core_t  *core,
                                       qdr_query_t *query,
                                       int          offset);
void qdra_config_exchange_get_next_CT(qdr_core_t  *core,
                                      qdr_query_t *query);
void qdra_config_binding_get_first_CT(qdr_core_t  *core,
                                      qdr_query_t *query,
                                      int          offset);
void qdra_config_binding_get_next_CT(qdr_core_t  *core,
                                     qdr_query_t *query);
void qdra_config_binding_create_CT(qdr_core_t         *core,
                                   qd_iterator_t      *name,
                                   qdr_query_t        *query,
                                   qd_parsed_field_t  *in_body);
void qdra_config_binding_delete_CT(qdr_core_t    *core,
                                   qdr_query_t   *query,
                                   qd_iterator_t *name,
                                   qd_iterator_t *identity);
void qdra_config_binding_get_CT(qdr_core_t    *core,
                                qd_iterator_t *name,
                                qd_iterator_t *identity,
                                qdr_query_t   *query,
                                const char    *columns[]);
#endif /* exchange_bindings.h */
