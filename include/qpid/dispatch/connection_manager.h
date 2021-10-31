#ifndef __dispatch_connection_manager_h__
#define __dispatch_connection_manager_h__ 1
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

/**@file
 * Manage listeners and connectors.
 */

#include "qpid/dispatch/dispatch.h"
#include "qpid/dispatch/server.h"

typedef struct qd_connection_manager_t qd_connection_manager_t;
typedef struct qd_config_ssl_profile_t qd_config_ssl_profile_t;
typedef struct qd_config_sasl_plugin_t qd_config_sasl_plugin_t;

typedef void (*qd_connection_manager_handler_t) (void *context, qd_connection_t *conn);

/**
 * Allocate a connection manager
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
qd_connection_manager_t *qd_connection_manager(qd_dispatch_t *qd);


/**
 * Free all the resources associated with the connection manager
 *
 * @param cm The connection manager handle returned by qd_connection_manager.
 */
void qd_connection_manager_free(qd_connection_manager_t *cm);

/**
 * Start the configured Listeners and Connectors
 *
 * Note that on-demand connectors are not started by this function.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
QD_EXPORT void qd_connection_manager_start(qd_dispatch_t *qd);

#endif
