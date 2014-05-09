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
 * @defgroup agent
 *
 * Container Management Agent
 * 
 * @{
 */

typedef struct qd_agent_class_t qd_agent_class_t;


/**
 * \brief Query Handler
 *
 * This function is called by the agent when a query is received.  This function is
 * responsible for generating object data for the query response.  It also must participate
 * in the pagination capability of the agent.
 *
 * There will be only one call into this function per query.  The function must increment
 * *index for every instance of the object (row in the table).
 *
 * Row data may only be generated for rows where (*index >= offset).
 *
 * @param context The handler context supplied in qd_agent_register.
 * @param correlator The correlation handle to be used in calls to qd_agent_value_*
 * @param attr_list An array of integers (terminated by a -1) identifying the attributes
 *                  requested in order.
 * @param index Pointer to an integer representing the ordinal of the table row.  This function
 *              must increment the index for each row it visits.  If the index is less than
 *              offset, no data should be generated.
 * @param offset The first index that is eligible for data generation.
 */
typedef void (*qd_agent_query_cb_t)(void *context, void *correlator);

typedef void (*qd_agent_query_attr_t)(void *object_handle, void *correlator, void *context);

bool qd_agent_object(void *correlator, void *object_handle);

typedef struct qd_agent_attribute_t {
    const char            *name;
    qd_agent_query_attr_t  handler;
    void                  *context;
} qd_agent_attribute_t;


/**
 * \brief Register a class/object-type with the agent.
 */
qd_agent_class_t *qd_agent_register_class(qd_dispatch_t              *qd,
                                          const char                 *fqname,
                                          void                       *context,
                                          const qd_agent_attribute_t *attributes,
                                          qd_agent_query_cb_t         query_handler);

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
void *qd_agent_raise_event(qd_dispatch_t *qd, qd_agent_class_t *event);


/**
 * @}
 */

#endif
