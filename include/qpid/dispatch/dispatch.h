#ifndef __dispatch_dispatch_h__
#define __dispatch_dispatch_h__ 1
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

/**
 * \defgroup General Dispatch Definitions
 * @{
 */

typedef struct qd_dispatch_t qd_dispatch_t;

/**
 * \brief Initialize the Dispatch library and prepare it for operation.
 *
 * @param python_pkgdir The path to the Python files.
 * @return A handle to be used in API calls for this instance.
 */
qd_dispatch_t *qd_dispatch(const char *python_pkgdir);


/**
 * \brief Finalize the Dispatch library after it has stopped running.
 *
 * @param dispatch The dispatch handle returned by qd_dispatch
 */
void qd_dispatch_free(qd_dispatch_t *dispatch);

/**
 * \brief Extend the schema for the configuration file prior to loading and
 *        parsing the file.
 *
 * @param dispatch The dispatch handle returned by qd_dispatch
 * @param text Python text used to extend the schema structure in the config reader
 */
void qd_dispatch_extend_config_schema(qd_dispatch_t *dispatch, const char* text);

/**
 * \brief Load the configuration file.
 *
 * @param dispatch The dispatch handle returned by qd_dispatch
 * @param config_path The path to the configuration file.
 */
void qd_dispatch_load_config(qd_dispatch_t *dispatch, const char *config_path);

/**
 * \brief Configure the AMQP container from the parsed configuration file.
 *        If this is not called, the container will take on default settings.
 *
 * @param dispatch The dispatch handle returned by qd_dispatch
 */
void qd_dispatch_configure_container(qd_dispatch_t *dispatch);

/**
 * \brief Configure the router node from the parsed configuration file.
 *        If this is not called, the router will run in ENDPOINT mode.
 *
 * @param dispatch The dispatch handle returned by qd_dispatch
 */
void qd_dispatch_configure_router(qd_dispatch_t *dispatch);

/**
 * \brief Prepare Dispatch for operation.  This must be called prior to
 *        calling qd_server_run or qd_server_start.
 *
 * @param dispatch The dispatch handle returned by qd_dispatch
 */
void qd_dispatch_prepare(qd_dispatch_t *dispatch);

/**
 * \brief Configure the server connectors and listeners from the
 *        parsed configuration file.  This must be called after the
 *        call to qd_dispatch_prepare completes.
 *
 * @param dispatch The dispatch handle returned by qd_dispatch
 */
void qd_dispatch_post_configure_connections(qd_dispatch_t *dispatch);


/**
 * @}
 */

#endif
