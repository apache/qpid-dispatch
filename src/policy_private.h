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

/** Allow or deny an incoming connection.
 * An Open performative was received over a new connection.
 * Consult local policy to determine if this host/user is
 * allow to make this connection. The underlying proton 
 * connection is either opened or closed.
 * @param[in] context a qd_connection_t object
 * @param[in] discard callback switch
 **/
void qd_policy_handle_open(void *context, bool discard);
