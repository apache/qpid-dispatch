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

#include "policy_private.h"
#include <stdio.h>
#include <string.h>
#include "dispatch_private.h"
#include "connection_manager_private.h"
#include <qpid/dispatch/container.h>
#include <qpid/dispatch/server.h>
#include <qpid/dispatch/message.h>
#include <proton/engine.h>
#include <proton/message.h>
#include <proton/condition.h>
#include <proton/connection.h>
#include <proton/error.h>
#include <proton/event.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/hash.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/log.h>

//
// TODO: get a real policy engine
//       This engine accepts every other connection
//
static bool allow_this = true;

bool policy_engine()
{
    return allow_this;
}

void policy_engine_step()
{
    // allow_this = !allow_this;
}


//
// TODO: when policy dev is more complete lower the log level
//
#define POLICY_LOG_LEVEL QD_LOG_CRITICAL

//
// The current statistics maintained globally through multiple
// reconfiguration of policy settings.
//
static int n_connections = 0;

//
// error conditions signaled to effect denial
//
static char* RESOURCE_LIMIT_EXCEEDED     = "amqp:resource-limit-exceeded";
//static char* UNAUTHORIZED_ACCESS         = "amqp:unauthorized-access";
//static char* CONNECTION_FORCED           = "amqp:connection:forced";

//
// error descriptions signaled to effect denial
//
static char* CONNECTION_DISALLOWED         = "connection disallowed by local policy";


//
// Policy configuration/statistics management interface
//
struct qd_policy_t {
    qd_dispatch_t        *qd;
    qd_log_source_t      *log_source;

    int                   max_connections;

    int                   current_connections;
};


qd_policy_t *qd_policy(qd_dispatch_t *qd)
{
    qd_policy_t *policy = NEW(qd_policy_t);

    policy->qd                  = qd;
    policy->log_source          = qd_log_source("POLICY");
    policy->max_connections     = 0;
    policy->current_connections = 0;

    qd_log(policy->log_source, QD_LOG_TRACE, "Policy Initialized");
    return policy;
}


void qd_policy_free(qd_policy_t *policy)
{
    free(policy);
}

//
//
qd_error_t qd_router_configure_policy(qd_policy_t *policy, qd_entity_t *entity)
{
    policy->max_connections = qd_entity_opt_long(entity, "maximumConnections", 0); QD_ERROR_RET();
    if (policy->max_connections < 0)
        return qd_error(QD_ERROR_CONFIG, "maximumConnections must be >= 0");
    qd_log(policy->log_source, QD_LOG_INFO, "Configured maximumConnections: %d", policy->max_connections);
    return QD_ERROR_NONE;
}


//
// Functions related to absolute connection counts.
// These handle connections at the socket level with
// no regard to user identity. Simple yes/no decisions
// are made and there is no AMQP channel for returning
// error conditions.
//
bool qd_policy_socket_accept(void *context, const char *hostname)
{
    qd_policy_t *policy = (qd_policy_t *)context;
    bool result = true;

    if (policy->max_connections == 0) {
        // Policy not in force; connection counted and allowed
        n_connections += 1;
    } else {
        // Policy in force
        if (n_connections < policy->max_connections) {
            // connection counted and allowed
            n_connections += 1;
            qd_log(policy->log_source, POLICY_LOG_LEVEL, "Connection '%s' allowed. N= %d", hostname, n_connections);
        } else {
            // connection denied
            result = false;
            qd_log(policy->log_source, POLICY_LOG_LEVEL, "Connection '%s' denied, N=%d", hostname, n_connections);
        }
    }
    return result;
}


void qd_policy_socket_close(void *context, const char *hostname)
{
    qd_policy_t *policy = (qd_policy_t *)context;

    n_connections -= 1;
    assert (n_connections >= 0);
    if (policy->max_connections > 0) {
        qd_log(policy->log_source, POLICY_LOG_LEVEL, "Connection '%s' closed, N=%d", hostname, n_connections);
    }
    qd_log(policy->log_source, POLICY_LOG_LEVEL, "Connection '%s' closed, N=%d", hostname, n_connections);  // HACK EXTRA
}


//
// Functions related to authenticated connection denial.
// An AMQP Open has been received over some connection.
// Evaluate the connection auth and the Open fields to
// allow or deny the Open. Denied Open attempts are
// effected with a returned Open-Close_with_condition.
//
void qd_policy_private_deny_amqp_connection(pn_connection_t *conn, const char *cond_name, const char *cond_descr)
{
    // Set the error condition and close the connection.
    // Over the wire this will send an open frame followed
    // immediately by a close frame with the error condition.
    pn_condition_t * cond = pn_connection_condition(conn);
    (void) pn_condition_set_name(       cond, cond_name);
    (void) pn_condition_set_description(cond, cond_descr);
    pn_connection_close(conn);
}

void qd_policy_amqp_open(void *context, bool discard)
{
    qd_connection_t *qd_conn = (qd_connection_t *)context;

    if (!discard) {
        pn_connection_t *conn = qd_connection_pn(qd_conn);

        // Consult policy engine for this connection attempt
        if ( policy_engine() ) { // TODO: get rid of this phony policy engine
            // This connection is allowed.
            if (pn_connection_state(conn) & PN_LOCAL_UNINIT)
                pn_connection_open(conn);
            qd_connection_manager_connection_opened(qd_conn);
        } else {
            // This connection is denied.
            qd_policy_private_deny_amqp_connection(conn, RESOURCE_LIMIT_EXCEEDED, CONNECTION_DISALLOWED);
        }
        // update the phony policy engine
        policy_engine_step();
    }
    qd_connection_set_event_stall(qd_conn, false);
}
