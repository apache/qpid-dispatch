#ifndef __policy_private_h__
#define __policy_private_h__
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

#include <qpid/dispatch.h>
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/static_assert.h>

#include "config.h"
#include "alloc.h"
#include "entity.h"
#include "entity_cache.h"
#include <dlfcn.h>

typedef struct qd_policy_t qd_policy_t;

qd_error_t qd_entity_configure_policy(qd_policy_t *policy, qd_entity_t *entity);

qd_error_t qd_register_policy_manager(qd_policy_t *policy, void *policy_manager);


/** Allow or deny an incoming connection based on connection count(s).
 * A server listener has just accepted a socket.
 * Allow or deny this connection based on the absolute number
 *  of allowed connections.
 * The identity of the connecting user has not been negotiated yet.
 * @param[in] context the current policy
 * @param[in] name the connector name
 * @return the connection is allowed or not
 **/
bool qd_policy_socket_accept(void *context, const char *hostname);


/** Record a closing connection.
 * A server listener is closing a socket.
 * Release the counted connection against provisioned limits
 * 
 * @param[in] context the current policy
 * @param[in] name the connector name
 **/
void qd_policy_socket_close(void *context, const char *hostname);


/** Allow or deny an incoming connection.
 * An Open performative was received over a new connection.
 * Consult local policy to determine if this host/user is
 * allow to make this connection. The underlying proton 
 * connection is either opened or closed.
 * This function is called from the deferred queue.
 * @param[in] context a qd_connection_t object
 * @param[in] discard callback switch
 **/
void qd_policy_amqp_open(void *context, bool discard);

#endif
