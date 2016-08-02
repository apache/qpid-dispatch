#ifndef __agent_adapter_h__
#define __agent_adapter_h__

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
#include "schema_enum.h"
#include "agent_private.h"

typedef struct qd_agent_t qd_agent_t;
typedef struct qd_agent_request_t qd_agent_request_t;

/**
 * Creates a new agent with the passed in address and whose configuration is located at config path.
 * @see qd_agent_start to start the agent
 */
qd_agent_t* qd_agent(char *address, const char *config_path);

/**
 * Free the agent and its components
 */
void qd_agent_free(qd_agent_t *agent);


typedef void (*qd_agent_handler_t) (void *context,
                                    qd_agent_request_t *request);

/**
 * Register CRUDQ handlers for a particular entity type
 */
void qd_agent_register_handlers(qd_agent_t *agent,
                                void *ctx,
                                qd_schema_entity_type_t entity_type,
                                qd_agent_handler_t create_handler,
                                qd_agent_handler_t read_handler,
                                qd_agent_handler_t update_handler,
                                qd_agent_handler_t delete_handler,
                                qd_agent_handler_t query_handler);

/**
 * Start the agent.
 * Loads the contents of the config file located in config_path
 * Agent starts listening on the provided address
 */
void qd_agent_start(qd_agent_t *agent);

#endif
