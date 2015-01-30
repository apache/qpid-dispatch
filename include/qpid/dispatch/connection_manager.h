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

#include <qpid/dispatch/dispatch.h>
#include <qpid/dispatch/server.h>

typedef struct qd_connection_manager_t qd_connection_manager_t;
typedef struct qd_config_connector_t qd_config_connector_t;
typedef struct qd_config_listener_t qd_config_listener_t;

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
void qd_connection_manager_start(qd_dispatch_t *qd);


/**
 * Given a connector-name, find and return a pointer to the on-demand connector.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 * @param name The name that uniquely identifies the on-demand connector.
 * @return The matching on-demand connector or NULL if the name is not found.
 */
qd_config_connector_t *qd_connection_manager_find_on_demand(qd_dispatch_t *qd, const char *name);


/**
 * Set open and close handlers for a connector.
 *
 * @param cc A configured connector.
 * @param open_handler A handler callback
 * @param close_handler A handler callback
 * @param context Context to be passed back to the handler
 */
void qd_connection_manager_set_handlers(qd_config_connector_t *cc,
                                        qd_connection_manager_handler_t open_handler,
                                        qd_connection_manager_handler_t close_handler,
                                        void *context);


/**
 * Start an on-demand connector.
 *
 * @param qd Pointer to the dispatch instance.
 * @param cc The pointer to an on-demand connector returned by qd_connections_find_on_demand.
 */
void qd_connection_manager_start_on_demand(qd_dispatch_t *qd, qd_config_connector_t *cc);


/**
 * Stop an on-demand connector.
 *
 * @param qd Pointer to the dispatch instance.
 * @param cc The pointer to an on-demand connector returned by qd_connections_find_on_demand.
 */
void qd_connection_manager_stop_on_demand(qd_dispatch_t *qd, qd_config_connector_t *cc);


/**
 * Get the user context for a configured connector.
 *
 * @param cc Connector handle returned by qd_connection_manager_find_on_demand
 * @return User context for the configured connector.
 */
void *qd_config_connector_context(qd_config_connector_t *cc);


/**
 * Set the user context for a configured connector.
 *
 * @param cc Connector handle returned by qd_connection_manager_find_on_demand
 * @param context User context to be stored with the configured connector
 */
void qd_config_connector_set_context(qd_config_connector_t *cc, void *context);


/**
 * Get the connector's name.
 *
 * @param cc Connector handle
 * @return The name of the connector
 */
const char *qd_config_connector_name(qd_config_connector_t *cc);

#endif
