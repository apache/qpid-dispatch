#ifndef __agent_handler_h__
#define __agent_handler_h__

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

#include "agent.h"

// The qdr_* functions handle CRUDQ calls related the the CORE data structures
void qd_core_agent_create_handler(qd_agent_request_t *request);
void qd_core_agent_update_handler(qd_agent_request_t *request);
void qd_core_agent_delete_handler(qd_agent_request_t *request);
void qd_core_agent_read_handler(qd_agent_request_t *request);
void qd_core_agent_query_handler(qd_agent_request_t *request);

// Handle sslProfile requests
void qd_create_ssl_profile(qd_agent_request_t *request);
void qd_delete_ssl_profile(qd_agent_request_t *request);
void qd_read_ssl_profile(qd_agent_request_t *request);
void qd_query_ssl_profile(qd_agent_request_t *request);

// Handle listener requests
void qd_create_listener(qd_agent_request_t *request);
void qd_delete_listener(qd_agent_request_t *request);
void qd_read_listener(qd_agent_request_t *request);
void qd_query_listener(qd_agent_request_t *request);

//Handler connector requests
void qd_create_connector(qd_agent_request_t *request);
void qd_delete_connector(qd_agent_request_t *request);
void qd_read_connector(qd_agent_request_t *request);
void qd_query_connector(qd_agent_request_t *request);

//Handler router requests
void qd_create_router(qd_agent_request_t *request);
void qd_delete_router(qd_agent_request_t *request);
void qd_read_router(qd_agent_request_t *request);
void qd_query_router(qd_agent_request_t *request);

//Handler connection requests
void qd_create_connection(qd_agent_request_t *request);
void qd_delete_connection(qd_agent_request_t *request);
void qd_read_connection(qd_agent_request_t *request);
void qd_query_connection(qd_agent_request_t *request);

void send_response(void *ctx,
                   qd_field_iterator_t *reply_to,
                   qd_field_iterator_t *correlation_id,
                   const qd_amqp_error_t *status,
                   qd_composed_field_t *out_body);


//TODO - will add more here


#endif
