#ifndef __remote_sasl_h__
#define __remote_sasl_h__ 1

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

#include <proton/event.h>
#include <proton/ssl.h>
#include <proton/types.h>

void qdr_use_remote_authentication_service(pn_transport_t* transport, const char* address, const char* hostname, const char* sasl_init_hostname, pn_ssl_domain_t* ssl_domain, pn_proactor_t* proactor);
bool qdr_is_authentication_service_connection(pn_connection_t* conn);
void qdr_handle_authentication_service_connection_event(pn_event_t *e);

#endif /* remote_sasl.h */
