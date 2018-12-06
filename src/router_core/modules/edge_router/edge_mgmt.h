#ifndef router_core_edge_mgmt_h
#define router_core_edge_mgmt_h 1
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

#include "router_core_private.h"

//
// Edge Router Management API
//
// API to interior router management agent.  Note that all callbacks are run
// under the Core thread.
//


/**
 * invoked when the reply arrives from the interior router
 *
 * @param core
 * @param request_context - user supplied context passed to the qcm_edge_mgmt_request_CT call
 * @param statusCode - as defined by the AMQP management spec
 * @param statusDescription - as defined by the AMQP management spec, may be NULL
 * @param body - body from reply message. May be NULL. Ownership is transferred
 *               to this callback - it must be freed by the application.
 * @return the final disposition to return to the interior router
 */
typedef uint64_t (*qcm_edge_mgmt_reply_CT_t)(qdr_core_t     *core,
                                             void           *request_context,
                                             int32_t         statusCode,
                                             const char     *statusDescription,
                                             qd_iterator_t  *body);


/**
 * Should the request message fail to be sent this callback will be invoked
 * instead the reply callback.  It can be used to clean up application state on
 * failure.
 *
 * @param core
 * @param request_context - user supplied context passed to the qcm_edge_mgmt_request_CT call
 * @param error - a description of the error that occurred.
 *
 */
typedef void (*qcm_edge_mgmt_error_CT_t)(qdr_core_t *core,
                                         void       *request_context,
                                         const char *error);

/**
 * Send management request to the interior router.
 *
 * Creates a management request message and sends it to the interior router's
 * $management target.  When the reply is received it is dispatched via the
 * reply_cb callback.  If a messaging failure occurs the error_cb callback is
 * invoked (and the reply_cb is not invoked)
 *
 * @param core
 * @param request_context - user supplied context that is passed to the callbacks
 * @param operation - one of "CREATE", "DELETE"
 * @param entity_type - identifies the type of mgmt entity to operate on,
 *                      (e.g. "org.apache.qpid.dispatch.router.config.address")
 * @param identity - for DELETE only: identifier for entity to be deleted
 * @param name - (optional) the entity's name
 * @param body - message body content.  Ownership is transferred - the caller
 *               must not reference this on return.
 * @param timeout - time in seconds to wait for request to complete.  If this
 *                  timer expires the error_cb will be invoked with the
 *                  error "Timed out"
 * @param reply_cb - Callback for reply message.
 * @param error_cb - Callback if error occurs
 * @return zero on success, else error. On success a callback is
 *         guaranteed to be invoked.
 */
int qcm_edge_mgmt_request_CT(qdr_core_t *core,
                             void       *request_context,
                             const char *operation,
                             const char *entity_type,
                             const char *identity,
                             const char *name,
                             qd_composed_field_t *body,
                             uint32_t                 timeout,
                             qcm_edge_mgmt_reply_CT_t reply_cb,
                             qcm_edge_mgmt_error_CT_t error_cb);


// module setup/teardown
void qcm_edge_mgmt_init_CT(qdr_core_t *core);
void qcm_edge_mgmt_final_CT(qdr_core_t *core);

#endif // router_core_edge_mgmt_h
