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

#include <qpid/dispatch/dispatch.h>

typedef struct qd_connection_manager_t qd_connection_manager_t;
typedef struct qd_config_connector_t qd_config_connector_t;
typedef struct qd_config_listener_t qd_config_listener_t;

/**
 * \brief Allocate a connection manager
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
qd_connection_manager_t *qd_connection_manager(qd_dispatch_t *qd);


/**
 * \brief Free all the resources associated with the connection manager
 *
 * @param cm The connection manager handle returned by qd_connection_manager.
 */
void qd_connection_manager_free(qd_connection_manager_t *cm);


/**
 * \brief Load the Listeners and Connections from the configuration file.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
void qd_connection_manager_configure(qd_dispatch_t *qd);


/**
 * \brief Start the configured Listeners and Connectors
 *
 * Note that on-demand connectors are not started by this function.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 */
void qd_connection_manager_start(qd_dispatch_t *qd);


/**
 * \brief Given a connector-tag, find and return a pointer to the on-demand connector.
 *
 * @param qd The dispatch handle returned by qd_dispatch.
 * @param tag The tag that uniquely identifies the on-demand connector.
 * @return The matching on-demand connector or NULL if the tag is not found.
 */
qd_config_connector_t *qd_connection_manager_find_on_demand(qd_dispatch_t *qd, const char *tag);


/**
 * \brief Start an on-demand connector.
 *
 * @param od The pointer to an on-demand connector returned by qd_connections_find_on_demand.
 */
void qd_connection_manager_start_on_demand(qd_config_connector_t *od);


/**
 * \brief Stop an on-demand connector.
 *
 * @param od The pointer to an on-demand connector returned by qd_connections_find_on_demand.
 */
void qd_connection_manager_stop_on_demand(qd_config_connector_t *od);

#endif
