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

#include <Python.h>
#include <qpid/dispatch/python_embedded.h>
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
#include <proton/transport.h>
#include <proton/error.h>
#include <proton/event.h>
#include <qpid/dispatch/ctools.h>
#include <qpid/dispatch/hash.h>
#include <qpid/dispatch/threading.h>
#include <qpid/dispatch/iterator.h>
#include <qpid/dispatch/log.h>


//
// TODO: when policy dev is more complete lower the log level
//
#define POLICY_LOG_LEVEL QD_LOG_CRITICAL

//
// The current statistics maintained globally through multiple
// reconfiguration of policy settings.
//
static int n_connections = 0;
static int n_denied = 0;
static int n_processed = 0;

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
    void                 *py_policy_manager;
                          // configured settings
    int                   max_connection_limit;
    char                 *policyFolder;
    bool                  enableAccessRules;
                          // live statistics
    int                   connections_processed;
    int                   connections_denied;
    int                   connections_current;
};

/** Create the policy structure
 * @param[in] qd pointer the the qd
 **/
qd_policy_t *qd_policy(qd_dispatch_t *qd)
{
    qd_policy_t *policy = NEW(qd_policy_t);

    policy->qd                   = qd;
    policy->log_source           = qd_log_source("POLICY");
    policy->max_connection_limit = 0;
    policy->policyFolder         = 0;
    policy->enableAccessRules    = false;
    policy->connections_processed= 0;
    policy->connections_denied   = 0;
    policy->connections_current  = 0;

    qd_log(policy->log_source, QD_LOG_TRACE, "Policy Initialized");
    return policy;
}


/** Free the policy structure
 * @param[in] policy pointer to the policy
 **/
void qd_policy_free(qd_policy_t *policy)
{
    if (policy->policyFolder)
        free(policy->policyFolder);
    free(policy);
}

//
//
#define CHECK() if (qd_error_code()) goto error

qd_error_t qd_entity_configure_policy(qd_policy_t *policy, qd_entity_t *entity)
{
    policy->max_connection_limit = qd_entity_opt_long(entity, "maximumConnections", 0); CHECK();
    if (policy->max_connection_limit < 0)
        return qd_error(QD_ERROR_CONFIG, "maximumConnections must be >= 0");
    policy->policyFolder =
        qd_entity_opt_string(entity, "policyFolder", 0); CHECK();
    policy->enableAccessRules = qd_entity_opt_bool(entity, "enableAccessRules", false); CHECK();
    qd_log(policy->log_source, QD_LOG_INFO, "Configured maximumConnections: %d", policy->max_connection_limit);
    return QD_ERROR_NONE;

error:
    qd_policy_free(policy);
    return qd_error_code();
}


//
//
qd_error_t qd_register_policy_manager(qd_policy_t *policy, void *policy_manager)
{
    policy->py_policy_manager = policy_manager;
    return QD_ERROR_NONE;
}

/** Update the statistics in qdrouterd.conf["policy"]
 * @param[in] entity pointer to the policy management object
 **/
qd_error_t qd_entity_refresh_policy(qd_entity_t* entity, void *unused) {
    // Return global stats
    if (!qd_entity_set_long(entity, "connectionsProcessed", n_processed) &&
        !qd_entity_set_long(entity, "connectionsDenied", n_denied) &&
        !qd_entity_set_long(entity, "connectionsCurrent", n_connections)
    )
        return QD_ERROR_NONE;
    return qd_error_code();
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

    if (policy->max_connection_limit == 0) {
        // Policy not in force; connection counted and allowed
        n_connections += 1;
    } else {
        // Policy in force
        if (n_connections < policy->max_connection_limit) {
            // connection counted and allowed
            n_connections += 1;
            qd_log(policy->log_source, POLICY_LOG_LEVEL, "Connection '%s' allowed. N= %d", hostname, n_connections);
        } else {
            // connection denied
            result = false;
            n_denied += 1;
            qd_log(policy->log_source, POLICY_LOG_LEVEL, "Connection '%s' denied, N=%d", hostname, n_connections);
        }
    }
    n_processed += 1;
    return result;
}


//
//
void qd_policy_socket_close(void *context, const qd_connection_t *conn)
{
    qd_policy_t *policy = (qd_policy_t *)context;

    n_connections -= 1;
    assert (n_connections >= 0);
    if (policy->enableAccessRules) {
        // HACK ALERT: TODO: This should be deferred to a Python thread
        qd_python_lock_state_t lock_state = qd_python_lock();
        PyObject *module = PyImport_ImportModule("qpid_dispatch_internal.policy.policy_manager");
        PyObject *close_connection = module ? PyObject_GetAttrString(module, "policy_close_connection") : NULL;
        Py_XDECREF(module);
        PyObject *result = close_connection ? PyObject_CallFunction(close_connection, "(OK)", 
                                                               (PyObject *)policy->py_policy_manager, 
                                                               conn->connection_id) : NULL;
        Py_XDECREF(close_connection);
        if (!result) {
            qd_python_unlock(lock_state);
            return;
        }
        Py_XDECREF(result);

        qd_python_unlock(lock_state);

    }
    const char *hostname = qdpn_connector_name(conn->pn_cxtr);
    if (policy->max_connection_limit > 0) {
        qd_log(policy->log_source, POLICY_LOG_LEVEL, "Connection '%s' closed. N connections=%d", hostname, n_connections);
    }
}


//
// Functions related to authenticated connection denial.
// An AMQP Open has been received over some connection.
// Evaluate the connection auth and the Open fields to
// allow or deny the Open. Denied Open attempts are
// effected by returning Open and then Close_with_condition.
//
/** Look up user/host/app in python policyRuleset and give the AMQP Open
 *  a go-no_go decision. Return false if the mechanics of calling python
 *  fails. A policy lookup will deny the connection by returning a blank
 *  usergroup name in the name buffer.
 * @param[in] policy pointer to policy
 * @param[in] username authenticated user name
 * @param[in] hostip numeric host ip address
 * @param[in] app application name received in remote AMQP Open.hostname
 * @param[in] conn_name connection name for tracking
 * @param[out] name_buf pointer to settings name buffer
 * @param[in] name_buf_size size of settings_buf
 **/
bool qd_policy_open_lookup_user(
    qd_policy_t *policy,
    const char *username,
    const char *hostip,
    const char *app,
    const char *conn_name,
    char       *name_buf,
    int         name_buf_size,
    uint64_t    conn_id,
    qd_policy_settings_t *settings)
{
    // Lookup the user/host/app for allow/deny and to get settings name
    qd_python_lock_state_t lock_state = qd_python_lock();
    PyObject *module = PyImport_ImportModule("qpid_dispatch_internal.policy.policy_manager");
    PyObject *lookup_user = module ? PyObject_GetAttrString(module, "policy_lookup_user") : NULL;
    PyObject *result = lookup_user ? PyObject_CallFunction(lookup_user, "(OssssK)", 
                                                           (PyObject *)policy->py_policy_manager, 
                                                           username, hostip, app, conn_name, conn_id) : NULL;
    Py_XDECREF(lookup_user);
    if (!result) {
        Py_XDECREF(module);
        qd_python_unlock(lock_state);
        qd_log(policy->log_source,
               POLICY_LOG_LEVEL,
               "PyObject lookup_user is Null");
        return false;
    }
    const char *res_string = PyString_AsString(result);
    strncpy(name_buf, res_string, name_buf_size);
    Py_XDECREF(result);

    if (name_buf[0]) {
        // Go get the settings
        PyObject *upolicy = PyDict_New();
        PyObject *lookup_settings = module ? PyObject_GetAttrString(module, "policy_lookup_settings") : NULL;
        PyObject *result2 = lookup_settings ? PyObject_CallFunction(lookup_settings, "(OssO)", 
                                                            (PyObject *)policy->py_policy_manager, 
                                                            app, name_buf, upolicy) : NULL;
        Py_XDECREF(lookup_settings);
        if (!result2) {
            Py_XDECREF(upolicy);
            qd_python_unlock(lock_state);
            qd_log(policy->log_source,
                   POLICY_LOG_LEVEL,
                   "PyObject lookup_settings is Null");
            return false;
        }
        Py_XDECREF(result2);
        settings->maxFrameSize         = qd_entity_opt_long((qd_entity_t*)upolicy, "maxFrameSize", 0);
        settings->maxMessageSize       = qd_entity_opt_long((qd_entity_t*)upolicy, "maxMessageSize", 0);
        settings->maxSessionWindow     = qd_entity_opt_long((qd_entity_t*)upolicy, "maxSessionWindow", 0);
        settings->maxSessions          = qd_entity_opt_long((qd_entity_t*)upolicy, "maxSessions", 0);
        settings->maxSenders           = qd_entity_opt_long((qd_entity_t*)upolicy, "maxSenders", 0);
        settings->maxReceivers         = qd_entity_opt_long((qd_entity_t*)upolicy, "maxReceivers", 0);
        settings->allowAnonymousSender = qd_entity_opt_bool((qd_entity_t*)upolicy, "allowAnonymousSender", false);
        settings->allowDynamicSrc      = qd_entity_opt_bool((qd_entity_t*)upolicy, "allowDynamicSrc", false);
        settings->sources              = qd_entity_get_string((qd_entity_t*)upolicy, "sources");
        settings->targets              = qd_entity_get_string((qd_entity_t*)upolicy, "targets");
        Py_XDECREF(upolicy);
    }
    Py_XDECREF(module);
    qd_python_unlock(lock_state);

    qd_log(policy->log_source, 
           POLICY_LOG_LEVEL, 
           "Policy AMQP Open lookup_user: %s, hostip: %s, app: %s, connection: %s. Usergroup: '%s'", 
           username, hostip, app, conn_name, name_buf);

    return true;
}

/** Set the error condition and close the connection.
 * Over the wire this will send an open frame followed
 * immediately by a close frame with the error condition.
 * @param[in] conn proton connection being closed
 * @param[in] cond_name condition name
 * @param[in] cond_descr condition description
 **/ 
void qd_policy_private_deny_amqp_connection(pn_connection_t *conn, const char *cond_name, const char *cond_descr)
{
    pn_condition_t * cond = pn_connection_condition(conn);
    (void) pn_condition_set_name(       cond, cond_name);
    (void) pn_condition_set_description(cond, cond_descr);
    pn_connection_close(conn);
}


//
//
void qd_policy_amqp_open(void *context, bool discard)
{
    qd_connection_t *qd_conn = (qd_connection_t *)context;
    if (!discard) {
        pn_connection_t *conn = qd_connection_pn(qd_conn);
        qd_dispatch_t *qd = qd_conn->server->qd;
        qd_policy_t *policy = qd->policy;

        // username = pn_connection_get_user(conn) returns blank when
        // the transport returns 'anonymous'.
        pn_transport_t *pn_trans = pn_connection_transport(conn);
        const char *username = pn_transport_get_user(pn_trans);

        const char *hostip = qdpn_connector_hostip(qd_conn->pn_cxtr);
        const char *app = pn_connection_remote_hostname(conn);
        const char *conn_name = qdpn_connector_name(qd_conn->pn_cxtr);
#define SETTINGS_NAME_SIZE 256
        char settings_name[SETTINGS_NAME_SIZE];
        uint32_t conn_id = qd_conn->connection_id;
        // TODO: settings need to be cached and kept beyond the open
        qd_policy_settings_t settings;
        memset(&settings, 0, sizeof(settings));

        if (!policy->enableAccessRules ||
            (qd_policy_open_lookup_user(policy, username, hostip, app, conn_name, 
                                        settings_name, SETTINGS_NAME_SIZE, conn_id,
                                        &settings) &&
             settings_name[0])) {
            // This connection is allowed.
            // Apply received settings
            if (settings.maxFrameSize > 0)
                pn_transport_set_max_frame(pn_trans, settings.maxFrameSize);
            if (settings.maxSessions > 0)
                pn_transport_set_channel_max(pn_trans, settings.maxSessions);

            // HACK ALERT: The settings were fetched, used for the Open,
            // and now they discarded.
            if (settings.sources)
                free(settings.sources);
            if (settings.targets)
                free(settings.targets);

            if (pn_connection_state(conn) & PN_LOCAL_UNINIT)
                pn_connection_open(conn);
            qd_connection_manager_connection_opened(qd_conn);
        } else {
            // This connection is denied.
            qd_policy_private_deny_amqp_connection(conn, RESOURCE_LIMIT_EXCEEDED, CONNECTION_DISALLOWED);
        }
    }
    qd_connection_set_event_stall(qd_conn, false);
}
