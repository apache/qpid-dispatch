#ifndef __policy_h__
#define __policy_h__
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

#include "qpid/dispatch.h"
#include "qpid/dispatch/server.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/static_assert.h"

#include "config.h"
#include "alloc.h"
#include "entity.h"
#include "entity_cache.h"
#include <dlfcn.h>

typedef struct qd_policy_denial_counts_s qd_policy_denial_counts_t;

// TODO: Provide locking
struct qd_policy_denial_counts_s {
    int sessionDenied;
    int senderDenied;
    int receiverDenied;
};

typedef struct qd_policy_t qd_policy_t;

struct qd_policy__settings_s {
    int  maxFrameSize;
    int  maxMessageSize;
    int  maxSessionWindow;
    int  maxSessions;
    int  maxSenders;
    int  maxReceivers;
    bool allowDynamicSrc;
    bool allowAnonymousSender;
    char *sources;
    char *targets;
    qd_policy_denial_counts_t *denialCounts;
};

typedef struct qd_policy__settings_s qd_policy_settings_t;

/** Configure the C policy entity from the settings in qdrouterd.conf["policy"]
 * Called python-to-C during config processing.
 * @param[in] policy pointer to the policy
 * @param[in] entity pointer to the managed entity
 * @return error or not. If error then the policy is freed.
 **/
qd_error_t qd_entity_configure_policy(qd_policy_t *policy, qd_entity_t *entity);

/** Memorize the address of python policy_manager object.
 * This python object gets called by C to execute user lookups 
 * @param[in] policy pointer to the policy
 * @param[in] policy_manager the address of the policy_manager object
 **/
qd_error_t qd_register_policy_manager(qd_policy_t *policy, void *policy_manager);


/** Allocate counts statistics block.
 * Called from Python
 */
long qd_policy_c_counts_alloc();

/** Free counts statistics block.
 * Called from Python
 */
void qd_policy_c_counts_free(long ccounts);

/** Refresh a counts statistics block
 * Called from Python
 */
qd_error_t qd_policy_c_counts_refresh(long ccounts, qd_entity_t*entity);


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
 * @param[in] conn qd_connection
 **/
void qd_policy_socket_close(void *context, const qd_connection_t *conn);


/** Approve a new session based on connection's policy.
 * Sessions denied are closed and counted.
 *
 * @param[in] ssn proton session being approved
 * @param[in] qd_conn dispatch connection with policy settings and counts
 **/
bool qd_policy_approve_amqp_session(pn_session_t *ssn, qd_connection_t *qd_conn);


/** Apply policy or default settings for a new session.
 *
 * @param[in] ssn proton session being set
 * @param[in] qd_conn dispatch connection with policy settings and counts
 **/
void qd_policy_apply_session_settings(pn_session_t *ssn, qd_connection_t *qd_conn);


/** Approve a new sender link based on connection's policy.
 * Links denied are closed and counted.
 *
 * @param[in] pn_link proton link being approved
 * @param[in] qd_conn dispatch connection with policy settings and counts
 **/
bool qd_policy_approve_amqp_sender_link(pn_link_t *pn_link, qd_connection_t *qd_conn);


/** Approve a new receiver link based on connection's policy.
 * Links denied are closed and counted.
 *
 * @param[in] pn_link proton link being approved
 * @param[in] qd_conn dispatch connection with policy settings and counts
 **/
bool qd_policy_approve_amqp_receiver_link(pn_link_t *pn_link, qd_connection_t *qd_conn);


/** Allow or deny an incoming connection.
 * An Open performative was received over a new connection.
 * Consult local policy to determine if this host/user is
 *  allowed to make this connection.
 * Denied pn_connections are closed with a condition.
 * Allowed connections are signaled through qd_connection_manager.
 * This function is called from the deferred queue.
 * @param[in] context a qd_connection_t object
 * @param[in] discard callback switch
 **/
void qd_policy_amqp_open(void *context, bool discard);

#endif
