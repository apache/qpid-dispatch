#ifndef __dispatch_agent_h__
#define __dispatch_agent_h__ 1
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
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/**
 * \defgroup Container Management Agent
 * @{
 */

typedef struct qd_agent_class_t qd_agent_class_t;


/**
 * \brief Get Schema Data Handler
 *
 * @param context The handler context supplied in qd_agent_register.
 */
typedef void (*qd_agent_schema_cb_t)(void* context, void *correlator);


/**
 * \brief Query Handler
 *
 * @param context The handler context supplied in qd_agent_register.
 * @param id The identifier of the instance being queried or NULL for all instances.
 * @param correlator The correlation handle to be used in calls to qd_agent_value_*
 */
typedef void (*qd_agent_query_cb_t)(void* context, const char *id, void *correlator);


/**
 * \brief Register a class/object-type with the agent.
 */
qd_agent_class_t *qd_agent_register_class(qd_dispatch_t        *qd,
                                          const char           *fqname,
                                          void                 *context,
                                          qd_agent_schema_cb_t  schema_handler,
                                          qd_agent_query_cb_t   query_handler);

/**
 * \brief Register an event-type with the agent.
 */
qd_agent_class_t *qd_agent_register_event(qd_dispatch_t        *qd,
                                          const char           *fqname,
                                          void                 *context,
                                          qd_agent_schema_cb_t  schema_handler);

/**
 *
 */
void qd_agent_value_string(void *correlator, const char *key, const char *value);
void qd_agent_value_uint(void *correlator, const char *key, uint64_t value);
void qd_agent_value_null(void *correlator, const char *key);
void qd_agent_value_boolean(void *correlator, const char *key, bool value);
void qd_agent_value_binary(void *correlator, const char *key, const uint8_t *value, size_t len);
void qd_agent_value_uuid(void *correlator, const char *key, const uint8_t *value);
void qd_agent_value_timestamp(void *correlator, const char *key, uint64_t value);

void qd_agent_value_start_list(void *correlator, const char *key);
void qd_agent_value_end_list(void *correlator);
void qd_agent_value_start_map(void *correlator, const char *key);
void qd_agent_value_end_map(void *correlator);


/**
 *
 */
void qd_agent_value_complete(void *correlator, bool more);


/**
 *
 */
void *qd_agent_raise_event(qd_dispatch_t *qd, qd_agent_class_t *event);


/**
 * @}
 */

#endif
