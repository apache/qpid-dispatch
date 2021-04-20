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

#include "config.h"
#include "entity.h"
#include "entity_cache.h"
#include "parse_tree.h"

#include "qpid/dispatch.h"
#include "qpid/dispatch/alloc.h"
#include "qpid/dispatch/alloc_pool.h"
#include "qpid/dispatch/ctools.h"
#include "qpid/dispatch/policy_spec.h"
#include "qpid/dispatch/server.h"
#include "qpid/dispatch/static_assert.h"

#include <dlfcn.h>

typedef struct qd_policy_denial_counts_s qd_policy_denial_counts_t;

// TODO: Provide locking
struct qd_policy_denial_counts_s {
    uint64_t sessionDenied;
    uint64_t senderDenied;
    uint64_t receiverDenied;
    uint64_t maxSizeMessagesDenied;
};

typedef struct qd_policy_t qd_policy_t;

//
// Policy settings are defined in include/qpid/dispatch/policy_settings.h
//

struct qd_policy__settings_s {
    qd_policy_spec_t spec;
    char *sources;
    char *targets;
    char *sourcePattern;
    char *targetPattern;
    qd_parse_tree_t *sourceParseTree;
    qd_parse_tree_t *targetParseTree;
    qd_policy_denial_counts_t *denialCounts;
    char *vhost_name;
};

typedef struct qd_policy__settings_s qd_policy_settings_t;

ALLOC_DECLARE(qd_policy_settings_t);

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
bool qd_policy_socket_accept(qd_policy_t *context, const char *hostname);


/** Record a closing connection.
 * A server listener is closing a socket.
 * Release the counted connection against provisioned limits
 * 
 * @param[in] context the current policy
 * @param[in] conn qd_connection
 **/
void qd_policy_socket_close(qd_policy_t *context, const qd_connection_t *conn);


/** Look up vhost in python vhost aliases database
 *  * Return false if the mechanics of calling python fails or if returned name buf is blank.
 *  * Return true if a name was returned.
 * @param[in]  policy pointer to policy
 * @param[in]  vhost vhost name received in remote AMQP Open.hostname
 * @param[out] name_buf pointer to result name buffer
 * @param[in]  name_buf_size size of name_buf
 **/
bool qd_policy_lookup_vhost_alias(
    qd_policy_t *policy,
    const char *vhost,
    char       *name_buf,
    int         name_buf_size);

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
 **/
void qd_policy_amqp_open(qd_connection_t *conn);


/** Allow or deny an outgoing connector connection.
 * An Open performative was received over a new connection.
 * Consult local policy to determine if this host/user is
 *  allowed to make this connection.
 * Denied pn_connections are closed with a condition.
 **/
void qd_policy_amqp_open_connector(qd_connection_t *conn);


/** Dispose of policy settings
 * 
 * @param settings the settings to be destroyed
 */
void qd_policy_settings_free(qd_policy_settings_t *settings);

/** Approve link by source/target name.
 * @param[in] username authenticated user name
 * @param[in] settings policy settings
 * @param[in] proposed the link target name to be approved
 * @param[in] isReceiver indication to check using receiver settings
 */
bool qd_policy_approve_link_name(const char *username,
                                 const qd_policy_settings_t *settings,
                                 const char *proposed,
                                 bool isReceiver
                                 );

/** Add a hostname to the lookup parse_tree
 * Note that the parse_tree may store an 'optimised' pattern for a given
 * pattern. Thus the patterns a user puts in may collide with existing
 * patterns even though the text of the host patterns is different.
 * This function does not allow new patterns with thier optimizations 
 * to overwrite existing patterns that may have been optimised.
 * @param[in] policy qd_policy_t
 * @param[in] hostPattern the hostname pattern with possible parse_tree wildcards
 * @return True if the possibly optimised pattern was added to the lookup parse tree
 */
bool qd_policy_host_pattern_add(qd_policy_t *policy, const char *hostPattern);

/** Remove a hostname from the lookup parse_tree
 * @param[in] policy qd_policy_t
 * @param[in] hostPattern the hostname pattern with possible parse_tree wildcards
 */
void qd_policy_host_pattern_remove(qd_policy_t *policy, const char *hostPattern);

/** Look up a hostname in the lookup parse_tree
 * @param[in] policy qd_policy_t
 * @param[in] hostname a concrete vhost name
 * @return the name of the ruleset whose hostname pattern matched this actual hostname
 */
char * qd_policy_host_pattern_lookup(qd_policy_t *policy, const char *hostPattern);

/**
 * Compile raw CSV spec of allowed sources/targets and return
 * the string of tuples used by policy runtime.
 * The returned string is allocated here and freed by the caller.
 * This function does no error checking or logging.
 *
 * @param[in] csv the CSV allowed list
 * @return the ruleset string to be used in policy settings.
 */
char * qd_policy_compile_allowed_csv(char * csv);

/**
 * Approve sending of message on anonymous link based on connection's policy.
 *
 * @param[in] address the address from the message 'to' field
 * @param[in] qd_conn dispatch connection with policy settings
 */
bool qd_policy_approve_message_target(qd_iterator_t *address, qd_connection_t *qd_conn);

/**
 * Increment counters for a link when policy maxMessageSize limit is exceeded.
 *
 * @param[in] pn_link proton link being with delivery/transfer being rejected
 * @param[in] qd_conn dispatch connection with policy settings and counts
 **/
void qd_policy_count_max_size_event(pn_link_t *link, qd_connection_t *qd_conn);

/**
 * Return POLICY log_source to log policy 
 */
qd_log_source_t* qd_policy_log_source();
#endif
