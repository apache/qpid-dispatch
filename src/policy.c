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

#include "python_private.h"
#include "qpid/dispatch/python_embedded.h"

#include "policy.h"

#include "dispatch_private.h"
#include "parse_tree.h"
#include "policy_internal.h"

#include "qpid/dispatch/container.h"
#include "qpid/dispatch/server.h"

#include <proton/condition.h>
#include <proton/connection.h>
#include <proton/event.h>
#include <proton/transport.h>

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

//
// The current statistics maintained globally through multiple
// reconfiguration of policy settings.
//
static sys_mutex_t *stats_lock = 0;

static uint64_t n_connections = 0;
static uint64_t n_denied = 0;
static uint64_t n_processed = 0;
static uint64_t n_links_denied = 0;
static uint64_t n_maxsize_messages_denied = 0;
static uint64_t n_total_denials = 0;

//
// error conditions signaled to effect denial
//

//
// error descriptions signaled to effect denial
//
static char* CONNECTION_DISALLOWED         = "connection disallowed by local policy";
static char* SESSION_DISALLOWED            = "session disallowed by local policy";
static char* LINK_DISALLOWED               = "link disallowed by local policy";

//
// username substitution key shared with configuration files and python code
// substitution triplet keys shared with python code 
//
static const char * const user_subst_key        = "${user}";
static const char * const user_subst_i_absent   = "a";
static const char * const user_subst_i_prefix   = "p";
static const char * const user_subst_i_embed    = "e";
static const char * const user_subst_i_suffix   = "s";
static const char * const user_subst_i_wildcard = "*";

//
// Fixed vhost policy usergroup used when storing connector policy.
// The connector attribute 'policyVhost' defines a vhost and within
// that vhost the connector policy values are in '$connector'.
static const char * const POLICY_VHOST_GROUP = "$connector";

static void hostname_tree_free(qd_parse_tree_t *hostname_tree);

// Imported qpid_dispatch_internal.policy.policy_manager python module
static PyObject * module = 0;

ALLOC_DEFINE(qd_policy_settings_t);

// Policy log module used outside of policy proper
qd_log_source_t* policy_log_source = 0;

//
// Policy configuration/statistics management interface
//
struct qd_policy_t {
    qd_dispatch_t        *qd;
    qd_log_source_t      *log_source;
    void                 *py_policy_manager;
    sys_mutex_t          *tree_lock;
    qd_parse_tree_t      *hostname_tree;
                          // configured settings
    int                   max_connection_limit;
    char                 *policyDir;
    bool                  enableVhostPolicy;
    bool                  enableVhostNamePatterns;
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
    ZERO(policy);
    policy->qd                   = qd;
    policy->log_source           = qd_log_source("POLICY");
    policy->max_connection_limit = 65535;
    policy->tree_lock            = sys_mutex();
    policy->hostname_tree        = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    stats_lock                   = sys_mutex();
    policy_log_source            = policy->log_source;

    qd_log(policy->log_source, QD_LOG_TRACE, "Policy Initialized");
    return policy;
}


/** Free the policy structure
 * @param[in] policy pointer to the policy
 **/
void qd_policy_free(qd_policy_t *policy)
{
    if (policy->policyDir)
        free(policy->policyDir);
    if (policy->tree_lock)
        sys_mutex_free(policy->tree_lock);
    hostname_tree_free(policy->hostname_tree);
    Py_XDECREF(module);
    free(policy);
    if (stats_lock)
        sys_mutex_free(stats_lock);
}

//
//
#define CHECK() if (qd_error_code()) goto error

qd_error_t qd_entity_configure_policy(qd_policy_t *policy, qd_entity_t *entity)
{
    module = PyImport_ImportModule("qpid_dispatch_internal.policy.policy_manager");
    if (!module) {
        qd_log(policy->log_source, QD_LOG_CRITICAL, "Required internal policy manager python module did not load. Shutting down.");
        exit(1);
    }
    policy->max_connection_limit = qd_entity_opt_long(entity, "maxConnections", 65535); CHECK();
    if (policy->max_connection_limit < 0)
        return qd_error(QD_ERROR_CONFIG, "maxConnections must be >= 0");
    policy->policyDir =
        qd_entity_opt_string(entity, "policyDir", 0); CHECK();
    policy->enableVhostPolicy = qd_entity_opt_bool(entity, "enableVhostPolicy", false); CHECK();
    policy->enableVhostNamePatterns = qd_entity_opt_bool(entity, "enableVhostNamePatterns", false); CHECK();
    qd_log(policy->log_source, QD_LOG_INFO,
           "Policy configured maxConnections: %d, "
           "policyDir: '%s',"
           "access rules enabled: '%s', "
           "use hostname patterns: '%s'",
           policy->max_connection_limit,
           policy->policyDir,
           (policy->enableVhostPolicy ? "true" : "false"),
           (policy->enableVhostNamePatterns ? "true" : "false"));
    return QD_ERROR_NONE;

error:
    if (policy->policyDir)
        free(policy->policyDir);
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


long qd_policy_c_counts_alloc()
{
    qd_policy_denial_counts_t * dc = NEW(qd_policy_denial_counts_t);
    assert(dc);
    ZERO(dc);
    return (long)dc;
}


void qd_policy_c_counts_free(long ccounts)
{
    void *dc = (void *)ccounts;
    assert(dc);
    free(dc);
}


qd_error_t qd_policy_c_counts_refresh(long ccounts, qd_entity_t *entity)
{
    qd_policy_denial_counts_t *dc = (qd_policy_denial_counts_t*)ccounts;
    if (!qd_entity_set_long(entity, "sessionDenied", dc->sessionDenied) &&
        !qd_entity_set_long(entity, "senderDenied", dc->senderDenied) &&
        !qd_entity_set_long(entity, "receiverDenied", dc->receiverDenied) &&
        !qd_entity_set_long(entity, "maxMessageSizeDenied", dc->maxSizeMessagesDenied)
    )
        return QD_ERROR_NONE;
    return qd_error_code();
}


/** Update the statistics in qdrouterd.conf["policy"]
 * @param[in] entity pointer to the policy management object
 **/
QD_EXPORT qd_error_t qd_entity_refresh_policy(qd_entity_t* entity, void *unused) {
    // Return global stats
    uint64_t np, nd, nc, nl, nm, nt;
    sys_mutex_lock(stats_lock);
    {
        np = n_processed;
        nd = n_denied;
        nc = n_connections;
        nl = n_links_denied;
        nm = n_maxsize_messages_denied;
        nt = n_total_denials;
    }
    sys_mutex_unlock(stats_lock);
    if (!qd_entity_set_long(entity, "connectionsProcessed", np) &&
        !qd_entity_set_long(entity, "connectionsDenied", nd) &&
        !qd_entity_set_long(entity, "connectionsCurrent", nc) &&
        !qd_entity_set_long(entity, "linksDenied", nl) &&
        !qd_entity_set_long(entity, "maxMessageSizeDenied", nm) &&
        !qd_entity_set_long(entity, "totalDenials", nt)
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

bool qd_policy_socket_accept(qd_policy_t *policy, const char *hostname)
{
    bool result = true;
    int nc;
    sys_mutex_lock(stats_lock);
    if (n_connections < policy->max_connection_limit) {
        // connection counted and allowed
        n_connections++;
        n_processed++;
        nc = n_connections;
        sys_mutex_unlock(stats_lock);
        qd_log(policy->log_source, QD_LOG_TRACE, "ALLOW Connection '%s' based on global connection count. nConnections= %d", hostname, nc);
    } else {
        // connection denied
        result = false;
        n_denied++;
        n_total_denials++;
        n_processed++;
        nc = n_connections;
        sys_mutex_unlock(stats_lock);
        qd_log(policy->log_source, QD_LOG_INFO, "DENY Connection '%s' based on global connection count. nConnections= %d", hostname, nc);
    }
    return result;
}


//
//
void qd_policy_socket_close(qd_policy_t *policy, const qd_connection_t *conn)
{
    sys_mutex_lock(stats_lock);
    n_connections--;
    uint64_t nc = n_connections;
    assert (n_connections >= 0);
    sys_mutex_unlock(stats_lock);
    if (policy->enableVhostPolicy) {
        // HACK ALERT: TODO: This should be deferred to a Python thread
        qd_python_lock_state_t lock_state = qd_python_lock();
        {
            PyObject *close_connection = PyObject_GetAttrString(module, "policy_close_connection");
            if (close_connection) {
                PyObject *result = PyObject_CallFunction(close_connection, "(OK)",
                                                         (PyObject *)policy->py_policy_manager,
                                                          conn->connection_id);
                if (result) {
                    Py_XDECREF(result);
                } else {
                    qd_log(policy->log_source, QD_LOG_DEBUG, "Internal: Connection close failed: result");
                }
                Py_XDECREF(close_connection);
            } else {
                qd_log(policy->log_source, QD_LOG_DEBUG, "Internal: Connection close failed: close_connection");
            }
        }
        qd_python_unlock(lock_state);
    }
    const char *hostname = qd_connection_name(conn);
    if (conn->policy_settings && conn->policy_settings->denialCounts) {
        qd_policy_denial_counts_t *qpdc = conn->policy_settings->denialCounts;
        qd_log(policy->log_source, QD_LOG_DEBUG,
           "[C%"PRIu64"] Connection '%s' closed with resources n_sessions=%d, n_senders=%d, n_receivers=%d, "
           "sessions_denied=%"PRIu64", senders_denied=%"PRIu64", receivers_denied=%"PRIu64", max_message_size_denied:%"PRIu64", nConnections= %"PRIu64".",
            conn->connection_id, hostname, conn->n_sessions, conn->n_senders, conn->n_receivers,
            qpdc->sessionDenied, qpdc->senderDenied, qpdc->receiverDenied, qpdc->maxSizeMessagesDenied, nc);
    }
}


// C in the CSV string
static const char* QPALN_COMMA_SEP =",";

//
// Given a CSV string defining parser tree specs for allowed sender or
// receiver links, return a parse_tree
//
//  @param config_spec CSV string with link name match patterns
//     The patterns consist of ('key', 'prefix', 'suffix') triplets describing
//     the match pattern.
//  @return pointer to parse tree
//
qd_parse_tree_t * qd_policy_parse_tree(const char *config_spec)
{
    if (!config_spec || strlen(config_spec) == 0)
        // empty config specs never match so don't even create parse tree
        return NULL;

    qd_parse_tree_t *tree = qd_parse_tree_new(QD_PARSE_TREE_ADDRESS);
    if (!tree)
        return NULL;

    // make a writable, disposable copy of the csv string
    char * dup = strdup(config_spec);
    if (!dup) {
        qd_parse_tree_free(tree);
        return NULL;
    }
    char * dupend = dup + strlen(dup);

    char * pch = dup;
    while (pch < dupend) {
        // the tuple strings
        char  *pChar, *pS1, *pS2;
        size_t sChar,  sS1,  sS2;

        // extract control field
        sChar = strcspn(pch, QPALN_COMMA_SEP);
        if (sChar != 1) { assert(false); break;}
        pChar = pch;
        pChar[sChar] = '\0';
        pch += sChar + 1;
        if (pch >= dupend) { assert(false); break; }

        // extract prefix field S1
        sS1 = strcspn(pch, QPALN_COMMA_SEP);
        pS1 = pch;
        pS1[sS1] = '\0';
        pch += sS1 + 1;
        if (pch > dupend) { assert(false); break; }

        // extract suffix field S2
        sS2 = strcspn(pch, QPALN_COMMA_SEP);
        pS2 = pch;
        pch += sS2 + 1;
        pS2[sS2] = '\0';

        size_t sName = sS1 + strlen(user_subst_key) + sS2 + 1; // large enough to handle any case
        char *pName = (char *)malloc(sName);

        if (!strcmp(pChar, user_subst_i_absent))
            snprintf(pName, sName, "%s", pS1);
        else if (!strcmp(pChar, user_subst_i_prefix))
            snprintf(pName, sName, "%s%s", user_subst_key, pS2);
        else if (!strcmp(pChar, user_subst_i_embed))
            snprintf(pName, sName, "%s%s%s", pS1, user_subst_key, pS2);
        else
            snprintf(pName, sName, "%s%s", pS1, user_subst_key);
        qd_parse_tree_add_pattern_str(tree, pName, (void *)1);

        free(pName);
    }
    free(dup);
    return tree;
}


//
// Functions related to authenticated connection denial.
// An AMQP Open has been received over some connection.
// * Evaluate the connection auth and the Open fields to allow or deny the Open. 
// * If allowed then return the settings from the python vhost database.
//

/** Look up vhost in python vhost aliases database
 *  * Return false if the mechanics of calling python fails or if returned name buf is blank.
 *  * Return true if a name was returned.
 * @param[in]  policy pointer to policy
 * @param[in]  vhost application name received in remote AMQP Open.hostname
 * @param[out] name_buf pointer to return name buffer
 * @param[in]  name_buf_size size of name_buf
 **/
bool qd_policy_lookup_vhost_alias(
    qd_policy_t *policy,
    const char *vhost,
    char       *name_buf,
    int         name_buf_size)
{
    bool res = false;
    name_buf[0] = 0;
    qd_python_lock_state_t lock_state = qd_python_lock();
    {
        PyObject *lookup_vhost_alias = PyObject_GetAttrString(module, "policy_lookup_vhost_alias");
        if (lookup_vhost_alias) {
            PyObject *result = PyObject_CallFunction(lookup_vhost_alias, "(Os)",
                                                     (PyObject *)policy->py_policy_manager,
                                                     vhost);
            if (result) {
                char *res_string = py_obj_2_c_string(result);
                const size_t res_len = res_string ? strlen(res_string) : 0;
                if (res_string && res_len < name_buf_size) {
                    strcpy(name_buf, res_string);
                } else {
                    qd_log(policy->log_source, QD_LOG_ERROR,
                           "Internal: lookup_vhost_alias: insufficient buffer for name");
                }
                Py_XDECREF(result);
                free(res_string);
                res = !!name_buf[0]; // settings name returned
            } else {
                qd_log(policy->log_source, QD_LOG_DEBUG, "Internal: lookup_vhost_alias: result");
            }
            Py_XDECREF(lookup_vhost_alias);
        } else {
            qd_log(policy->log_source, QD_LOG_DEBUG, "Internal: lookup_vhost_alias: lookup_vhost_alias");
        }
    }
    qd_python_unlock(lock_state);

    return res;
}


/** Look up user/host/vhost in python vhost database and give the AMQP Open
 *  a go-no_go decision. 
 *  * Return false if the mechanics of calling python fails or if name buf is blank. 
 *  * Deny the connection by returning a blank usergroup name in the name buffer.
 *  Connection and connection denial counting is done in the python code.
 * @param[in]  policy pointer to policy
 * @param[in]  username authenticated user name
 * @param[in]  hostip numeric host ip address
 * @param[in]  vhost application name received in remote AMQP Open.hostname
 * @param[in]  conn_name connection name for tracking
 * @param[out] name_buf pointer to settings name buffer
 * @param[in]  name_buf_size size of settings_buf
 * @param[in]  conn_id connection id for log tracking
 **/
bool qd_policy_open_lookup_user(
    qd_policy_t *policy,
    const char *username,
    const char *hostip,
    const char *vhost,
    const char *conn_name,
    char       *name_buf,
    int         name_buf_size,
    uint64_t    conn_id)
{
    bool res = false;
    name_buf[0] = 0;
    qd_python_lock_state_t lock_state = qd_python_lock();
    {
        PyObject *lookup_user = PyObject_GetAttrString(module, "policy_lookup_user");
        if (lookup_user) {
            PyObject *result = PyObject_CallFunction(lookup_user, "(OssssK)",
                                                     (PyObject *)policy->py_policy_manager,
                                                     username, hostip, vhost, conn_name, conn_id);
            if (result) {
                char *res_string = py_obj_2_c_string(result);
                const size_t res_len = res_string ? strlen(res_string) : 0;
                if (res_string && res_len < name_buf_size) {
                    strcpy(name_buf, res_string);
                } else {
                    qd_log(policy->log_source, QD_LOG_ERROR,
                           "Internal: lookup_user: insufficient buffer for name");
                }
                Py_XDECREF(result);
                free(res_string);
                res = !!name_buf[0]; // settings name returned
            } else {
                qd_log(policy->log_source, QD_LOG_DEBUG, "Internal: lookup_user: result");
            }
            Py_XDECREF(lookup_user);
        } else {
            qd_log(policy->log_source, QD_LOG_DEBUG, "Internal: lookup_user: lookup_user");
        }
    }
    qd_python_unlock(lock_state);

    if (name_buf[0]) {
        qd_log(policy->log_source,
           QD_LOG_TRACE,
           "[C%"PRIu64"] ALLOW AMQP Open lookup_user: %s, rhost: %s, vhost: %s, connection: %s. Usergroup: '%s'%s",
           conn_id, username, hostip, vhost, conn_name, name_buf, (res ? "" : " Internal error."));
    }
    return res;
}


/** Fetch policy settings for a vhost/group
 * A vhost database user group name has been returned by qd_policy_open_lookup_user
 * or by some configuration value. Access the vhost database for that group and
 * extract the run-time settings.
 * @param[in] policy pointer to policy
 * @param[in] vhost vhost name
 * @param[in] group_name usergroup that holds the settings
 * @param[out] settings pointer to settings object to be filled with policy values
 **/
bool qd_policy_open_fetch_settings(
    qd_policy_t *policy,
    const char *vhost,
    const char *group_name,
    qd_policy_settings_t *settings)
{
    bool res = false;
    qd_python_lock_state_t lock_state = qd_python_lock();
    {
        res = false;
        PyObject *upolicy = PyDict_New();
        if (upolicy) {
            PyObject *lookup_settings = PyObject_GetAttrString(module, "policy_lookup_settings");
            if (lookup_settings) {
                PyObject *result2 = PyObject_CallFunction(lookup_settings, "(OssO)",
                                                        (PyObject *)policy->py_policy_manager,
                                                        vhost, group_name, upolicy);
                if (result2) {
                    int truthy = PyObject_IsTrue(result2);
                    if (truthy) {
                        settings->spec.maxFrameSize         = qd_entity_opt_long((qd_entity_t*)upolicy, "maxFrameSize", 0);
                        settings->spec.maxSessionWindow     = qd_entity_opt_long((qd_entity_t*)upolicy, "maxSessionWindow", 0);
                        settings->spec.maxSessions          = qd_entity_opt_long((qd_entity_t*)upolicy, "maxSessions", 0);
                        settings->spec.maxSenders           = qd_entity_opt_long((qd_entity_t*)upolicy, "maxSenders", 0);
                        settings->spec.maxReceivers         = qd_entity_opt_long((qd_entity_t*)upolicy, "maxReceivers", 0);
                        settings->spec.maxMessageSize       = qd_entity_opt_long((qd_entity_t*)upolicy, "maxMessageSize", 0);
                        if (!settings->spec.allowAnonymousSender) { //don't override if enabled by authz plugin
                            settings->spec.allowAnonymousSender = qd_entity_opt_bool((qd_entity_t*)upolicy, "allowAnonymousSender", false);
                        }
                        if (!settings->spec.allowDynamicSource) { //don't override if enabled by authz plugin
                            settings->spec.allowDynamicSource   = qd_entity_opt_bool((qd_entity_t*)upolicy, "allowDynamicSource", false);
                        }
                        settings->spec.allowUserIdProxy       = qd_entity_opt_bool((qd_entity_t*)upolicy, "allowUserIdProxy", false);
                        settings->spec.allowWaypointLinks     = qd_entity_opt_bool((qd_entity_t*)upolicy, "allowWaypointLinks", true);
                        settings->spec.allowFallbackLinks     = qd_entity_opt_bool((qd_entity_t*)upolicy, "allowFallbackLinks", true);
                        settings->spec.allowDynamicLinkRoutes = qd_entity_opt_bool((qd_entity_t*)upolicy, "allowDynamicLinkRoutes", true);

                        //
                        // By default, deleting connections are enabled. To disable, set the allowAdminStatusUpdate to false in a policy.
                        //
                        settings->spec.allowAdminStatusUpdate = qd_entity_opt_bool((qd_entity_t*)upolicy, "allowAdminStatusUpdate", true);
                        if (settings->sources == 0) { //don't override if configured by authz plugin
                            settings->sources              = qd_entity_get_string((qd_entity_t*)upolicy, "sources");
                        }
                        if (settings->targets == 0) { //don't override if configured by authz plugin
                            settings->targets              = qd_entity_get_string((qd_entity_t*)upolicy, "targets");
                        }
                        settings->sourcePattern        = qd_entity_get_string((qd_entity_t*)upolicy, "sourcePattern");
                        settings->targetPattern        = qd_entity_get_string((qd_entity_t*)upolicy, "targetPattern");
                        settings->sourceParseTree      = qd_policy_parse_tree(settings->sourcePattern);
                        settings->targetParseTree      = qd_policy_parse_tree(settings->targetPattern);
                        settings->denialCounts         = (qd_policy_denial_counts_t*)
                                                        qd_entity_get_long((qd_entity_t*)upolicy, "denialCounts");
                        res = true; // named settings content returned
                    } else {
                        // lookup failed: object did not exist in python database
                    }
                    Py_XDECREF(result2);
                } else {
                    qd_log(policy->log_source, QD_LOG_DEBUG, "Internal: lookup_user: result2");
                }
                Py_XDECREF(lookup_settings);
            } else {
                qd_log(policy->log_source, QD_LOG_DEBUG, "Internal: lookup_user: lookup_settings");
            }
            Py_XDECREF(upolicy);
        } else {
            qd_log(policy->log_source, QD_LOG_DEBUG, "Internal: lookup_user: upolicy");
        }
    }
    qd_python_unlock(lock_state);

    return res;
}


//
//
void qd_policy_private_deny_amqp_connection(pn_connection_t *conn, const char *cond_name, const char *cond_descr)
{
    pn_condition_t * cond = pn_connection_condition(conn);
    (void) pn_condition_set_name(       cond, cond_name);
    (void) pn_condition_set_description(cond, cond_descr);
    pn_connection_close(conn);
    // Connection denial counts are counted and logged by python code.
}


//
//
void qd_policy_deny_amqp_session(pn_session_t *ssn, qd_connection_t *qd_conn)
{
    pn_condition_t * cond = pn_session_condition(ssn);
    (void) pn_condition_set_name(       cond, QD_AMQP_COND_RESOURCE_LIMIT_EXCEEDED);
    (void) pn_condition_set_description(cond, SESSION_DISALLOWED);
    pn_session_close(ssn);
    sys_mutex_lock(stats_lock);
    n_total_denials++;
    sys_mutex_unlock(stats_lock);
    if (qd_conn->policy_settings->denialCounts) {
        qd_conn->policy_settings->denialCounts->sessionDenied++;
    }
}


//
//
bool qd_policy_approve_amqp_session(pn_session_t *ssn, qd_connection_t *qd_conn)
{
    bool result = true;
    if (qd_conn->policy_settings) {
        if (qd_conn->policy_settings->spec.maxSessions) {
            if (qd_conn->n_sessions == qd_conn->policy_settings->spec.maxSessions) {
                qd_policy_deny_amqp_session(ssn, qd_conn);
                result = false;
            }
        }
    }
    pn_connection_t *conn = qd_connection_pn(qd_conn);
    qd_dispatch_t *qd = qd_server_dispatch(qd_conn->server);
    qd_policy_t *policy = qd->policy;
    const char *hostip = qd_connection_remote_ip(qd_conn);
    const char *vhost = pn_connection_remote_hostname(conn);
    if (result) {
        qd_log(policy->log_source,
           QD_LOG_TRACE,
           "[C%"PRIu64"] ALLOW AMQP Begin Session. user: %s, rhost: %s, vhost: %s",
           qd_conn->connection_id, qd_conn->user_id, hostip, vhost);
    } else {
        qd_log(policy->log_source,
           QD_LOG_INFO,
           "[C%"PRIu64"] DENY AMQP Begin Session due to session limit. user: %s, rhost: %s, vhost: %s",
           qd_conn->connection_id, qd_conn->user_id, hostip, vhost);
    }
    return result;
}


//
//
void qd_policy_apply_session_settings(pn_session_t *ssn, qd_connection_t *qd_conn)
{
    size_t capacity;
    if (qd_conn->policy_settings && qd_conn->policy_settings->spec.maxSessionWindow
        && !qd_conn->policy_settings->spec.outgoingConnection) {
        capacity = qd_conn->policy_settings->spec.maxSessionWindow;
    } else {
        const qd_server_config_t * cf = qd_connection_config(qd_conn);
        capacity = cf->incoming_capacity;
    }
    pn_session_set_incoming_capacity(ssn, capacity);
}

//
//
void _qd_policy_deny_amqp_link(pn_link_t *link, qd_connection_t *qd_conn, const char *condition)
{
    pn_condition_t * cond = pn_link_condition(link);
    (void) pn_condition_set_name(       cond, condition);
    (void) pn_condition_set_description(cond, LINK_DISALLOWED);
    pn_link_close(link);
    sys_mutex_lock(stats_lock);
    n_links_denied++;
    n_total_denials++;
    sys_mutex_unlock(stats_lock);
}


//
//
void _qd_policy_deny_amqp_sender_link(pn_link_t *pn_link, qd_connection_t *qd_conn, const char *condition)
{
    _qd_policy_deny_amqp_link(pn_link, qd_conn, condition);
    if (qd_conn->policy_settings->denialCounts) {
        qd_conn->policy_settings->denialCounts->senderDenied++;
    }
}


//
//
void _qd_policy_deny_amqp_receiver_link(pn_link_t *pn_link, qd_connection_t *qd_conn, const char *condition)
{
    _qd_policy_deny_amqp_link(pn_link, qd_conn, condition);
    if (qd_conn->policy_settings->denialCounts) {
        qd_conn->policy_settings->denialCounts->receiverDenied++;
    }
}


//
//
void qd_policy_count_max_size_event(pn_link_t *link, qd_connection_t *qd_conn)
{
    sys_mutex_lock(stats_lock);
    n_maxsize_messages_denied++;
    n_total_denials++;
    sys_mutex_unlock(stats_lock);
    // TODO: denialCounts is shared among connections and should be protected also
    if (qd_conn->policy_settings && qd_conn->policy_settings->denialCounts) {
        qd_conn->policy_settings->denialCounts->maxSizeMessagesDenied++;
    }
}

/**
 * Given a char return true if it is a parse_tree token separater
 */
bool is_token_sep(char testc)
{
    for (const char *ptr = qd_parse_address_token_sep(); *ptr != '\0'; ptr++) {
        if (*ptr == testc)
            return true;
    }
    return false;
}


//
//
// Size of 'easy' temporary copy of allowed input string
#define QPALN_SIZE 1024
// Wildcard character at end of source/target name strings
#define QPALN_WILDCARD '*'

#define MIN(a,b) (((a)<(b))?(a):(b))

/**
 * Given a username and a list of allowed link names 
 * decide if the proposed link name is approved.
 * @param[in] username the user name
 * @param[in] allowed csv of (key, prefix, suffix) tuples
 * @param[in] proposed the link source/target name to be approved
 * @return true if the user is allowed to open this link source/target name
 * 
 * Concrete example
 * user: 'bob', allowed (from spec): 'A,B,tmp-${user},C', proposed: 'tmp-bob'
 * note that allowed above is now a tuple and not simple string fron the spec.
 */
bool _qd_policy_approve_link_name(const char *username, const char *allowed, const char *proposed)
{
    // Verify string sizes are usable
    size_t p_len = strlen(proposed);
    if (p_len == 0) {
        // degenerate case of blank proposed name being opened. will never match anything.
        return false;
    }

    size_t a_len = strlen(allowed);
    if (a_len == 0) {
        // no names in 'allowed'.
        return false;
    }

    if (!username)
        username = "";

    size_t username_len = strlen(username);

    // make a writable, disposable copy of the csv string
    char * dup = strdup(allowed);
    if (!dup) {
        return false;
    }
    char * dupend = dup + strlen(dup);
    char * pch = dup;

    // get a scratch buffer for writing temporary match strings
    char * pName = (char *)malloc(QPALN_SIZE);
    if (!pName) {
        free(dup);
        return false;
    }

    size_t pName_sz = QPALN_SIZE;

    bool result = false;

    while (pch < dupend) {
        // the tuple strings
        char  *pChar, *pS1, *pS2;
        size_t sChar,  sS1,  sS2;

        // extract control field
        sChar = strcspn(pch, QPALN_COMMA_SEP);
        if (sChar != 1) { assert(false); break;}
        pChar = pch;
        pChar[sChar] = '\0';
        pch += sChar + 1;
        if (pch >= dupend) { assert(false); break; }

        // extract prefix field S1
        sS1 = strcspn(pch, QPALN_COMMA_SEP);
        pS1 = pch;
        pS1[sS1] = '\0';
        pch += sS1 + 1;
        if (pch > dupend) { assert(false); break; }

        // extract suffix field S2
        sS2 = strcspn(pch, QPALN_COMMA_SEP);
        pS2 = pch;
        pch += sS2 + 1;
        pS2[sS2] = '\0';

        // compute size of generated string and make sure
        // temporary buffer is big enough to hold it.
        size_t sName = sS1 + username_len + sS2 + 1;
        if (sName > pName_sz) {
            size_t newSize = sName + QPALN_SIZE;
            char * newPtr = (char *)realloc(pName, newSize);
            if (!newPtr) {
                break;
            }
            pName = newPtr;
            pName_sz = newSize;
        }

        // if wildcard then check no more
        if (*pChar == *user_subst_i_wildcard) {
            result = true;
            break;
        }
        // From the rule clause construct what the rule is allowing
        // given the user name associated with this request.
        int snpN;
        if (*pChar == *user_subst_i_absent)
            snpN = snprintf(pName, sName, "%s", pS1);
        else if (*pChar == *user_subst_i_prefix)
            snpN = snprintf(pName, sName, "%s%s", username, pS2);
        else if (*pChar == *user_subst_i_embed)
            snpN = snprintf(pName, sName, "%s%s%s", pS1, username, pS2);
        else if (*pChar == *user_subst_i_suffix)
            snpN = snprintf(pName, sName, "%s%s", pS1, username);
        else {
            assert(false);
            break;
        }

        size_t rule_len = MIN(snpN, sName); 
        if (pName[rule_len-1] != QPALN_WILDCARD) {
            // Rule clauses that do not end with wildcard 
            // must match entire proposed name string.
            // pName=tmp-bob-5, proposed can be only 'tmp-bob-5'
            result = strcmp(proposed, pName) == 0;
        } else {
            // Rule clauses that end with wildcard
            // must match only as many characters as the cluase without the '*'.
            // pName=tmp*, will match proposed 'tmp', 'tmp-xxx', 'tmp-bob', ...
            result = strncmp(proposed, pName, rule_len - 1) == 0;
        }
        if (result)
            break;
    }
    free(pName);
    free(dup);

    return result;
}


bool _qd_policy_approve_link_name_tree(const char *username, const char *allowed, const char *proposed,
                                       qd_parse_tree_t *tree)
{
    // Verify string sizes are usable
    size_t proposed_len = strlen(proposed);
    if (proposed_len == 0) {
        // degenerate case of blank proposed name being opened. will never match anything.
        return false;
    }
    size_t a_len = strlen(allowed);
    if (a_len == 0) {
        // no names in 'allowed'.
        return false;
    }

    if (!username)
        username = "";

    size_t username_len = strlen(username);
    size_t usersubst_len = strlen(user_subst_key);

    // make a writable, disposable copy of the csv string
    char * dup = strdup(allowed);
    if (!dup) {
        return false;
    }
    char * dupend = dup + strlen(dup);
    char * pch = dup;

    // get a scratch buffer for writing temporary match strings
    char * pName = (char *)malloc(QPALN_SIZE);
    if (!pName) {
        free(dup);
        return false;
    }
    size_t pName_sz = QPALN_SIZE;

    bool result = false;

    while (pch < dupend) {
        // the tuple strings
        char  *pChar, *pS1, *pS2;
        size_t sChar,  sS1,  sS2;

        // extract control field
        sChar = strcspn(pch, QPALN_COMMA_SEP);
        if (sChar != 1) { assert(false); break;}
        pChar = pch;
        pChar[sChar] = '\0';
        pch += sChar + 1;
        if (pch >= dupend) { assert(false); break; }

        // extract prefix field S1
        sS1 = strcspn(pch, QPALN_COMMA_SEP);
        pS1 = pch;
        pS1[sS1] = '\0';
        pch += sS1 + 1;
        if (pch > dupend) { assert(false); break; }

        // extract suffix field S2
        sS2 = strcspn(pch, QPALN_COMMA_SEP);
        pS2 = pch;
        pch += sS2 + 1;
        pS2[sS2] = '\0';

        // compute size of generated string and make sure
        // temporary buffer is big enough to hold it.
        size_t sName = proposed_len + usersubst_len + 1;
        if (sName > pName_sz) {
            size_t newSize = sName + QPALN_SIZE;
            char * newPtr = (char *)realloc(pName, newSize);
            if (!newPtr) {
                break;
            }
            pName = newPtr;
            pName_sz = newSize;
        }

        // From the rule clause construct what the rule is allowing
        // given the user name associated with this request.
        if (*pChar == *user_subst_i_absent) {
            // Substitution spec is absent. The search string is the literal
            // S1 in the rule.
            snprintf(pName, sName, "%s", proposed);
        }
        else if (*pChar == *user_subst_i_prefix) {
            // Substitution spec is prefix.
            if (strncmp(proposed, username, username_len) != 0)
                continue; // Denied. Proposed does not have username prefix.
            // Check that username is not part of a larger token.
            if (username_len == proposed_len) {
                // If username is the whole link name then allow if lookup is ok
            } else {
                // Proposed is longer than username. Make sure that proposed
                // is delimited after user name.
                if (!is_token_sep(proposed[username_len])) {
                    continue; // denied. proposed has username prefix it it not a delimited user name
                }
            }
            snprintf(pName, sName, "%s%s", user_subst_key, proposed + username_len);
        }
        else if (*pChar == *user_subst_i_embed) {
            assert(false); // not supported
        }
        else if (*pChar == *user_subst_i_suffix) {
            // Check that link name has username suffix
            if (username_len > proposed_len) {
                continue; // denied. proposed name is too short to hold username
            } else {
                //---
                // if (username_len == proposed_len) { ... }
                // unreachable code. substitution-only rule clause is handled by prefix
                //---
                if (!is_token_sep(proposed[proposed_len - username_len - 1])) {
                    continue; // denied. proposed suffix it it not a delimited user name
                }
                if (strncmp(&proposed[proposed_len - username_len], username, username_len) != 0) {
                    continue; // denied. username is not the suffix
                }
            }
            pName[0] = '\0';
            strncat(pName, proposed, proposed_len - username_len);
            strcat(pName, user_subst_key);
        }
        else {
            assert(false);
            break;
        }

        void * unused_payload = 0;
        result = qd_parse_tree_retrieve_match_str(tree, pName, &unused_payload);
        if (result)
            break;
    }
    free(pName);
    free(dup);

    return result;
}


static bool qd_policy_terminus_is_waypoint(pn_terminus_t *term)
{
    pn_data_t *cap = pn_terminus_capabilities(term);
    if (cap) {
        pn_data_rewind(cap);
        pn_data_next(cap);
        if (cap && pn_data_type(cap) == PN_SYMBOL) {
            pn_bytes_t sym = pn_data_get_symbol(cap);
            size_t     len = strlen(QD_CAPABILITY_WAYPOINT_DEFAULT);
            if (sym.size >= len && strncmp(sym.start, QD_CAPABILITY_WAYPOINT_DEFAULT, len) == 0)
                return true;
        }
    }

    return false;
}


static bool qd_policy_terminus_is_fallback(pn_terminus_t *term)
{
    pn_data_t *cap = pn_terminus_capabilities(term);
    if (cap) {
        pn_data_rewind(cap);
        pn_data_next(cap);
        if (cap && pn_data_type(cap) == PN_SYMBOL) {
            pn_bytes_t sym = pn_data_get_symbol(cap);
            if (strcmp(sym.start, QD_CAPABILITY_FALLBACK) == 0)
                return true;
        }
    }

    return false;
}


bool qd_policy_approve_message_target(qd_iterator_t *address, qd_connection_t *qd_conn)
{
#define ON_STACK_SIZE 2048
    char  on_stack[ON_STACK_SIZE + 1];
    char *buffer    = on_stack;
    bool  on_heap = false;
    int   length  = qd_iterator_length(address);

    if (length > ON_STACK_SIZE) {
        buffer    = (char*) malloc(length + 1);
        on_heap = true;
    }

    const char* target = qd_iterator_strncpy(address, buffer, length + 1);

    bool lookup = false;
    if (qd_conn->policy_settings->targetParseTree) {
        lookup = _qd_policy_approve_link_name_tree(qd_conn->user_id, qd_conn->policy_settings->targetPattern, target, qd_conn->policy_settings->targetParseTree);
    } else if (qd_conn->policy_settings->targets) {
        lookup = _qd_policy_approve_link_name(qd_conn->user_id, qd_conn->policy_settings->targets, target);
    }

    const char *hostip = qd_connection_remote_ip(qd_conn);
    const char *vhost = pn_connection_remote_hostname(qd_connection_pn(qd_conn));
    qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, (lookup ? QD_LOG_TRACE : QD_LOG_INFO),
           "[C%"PRIu64"] %s AMQP message to '%s' for user '%s', rhost '%s', vhost '%s' based on target address",
           qd_conn->connection_id, (lookup ? "ALLOW" : "DENY"), target, qd_conn->user_id, hostip, vhost);

    if (on_heap)
        free(buffer);

    if (!lookup) {
        return false;
    } else {
        return true;
    }
}

bool qd_policy_approve_amqp_sender_link(pn_link_t *pn_link, qd_connection_t *qd_conn)
{
    const char *hostip = qd_connection_remote_ip(qd_conn);
    const char *vhost = pn_connection_remote_hostname(qd_connection_pn(qd_conn));

    if (qd_conn->policy_settings->spec.maxSenders) {
        if (qd_conn->n_senders == qd_conn->policy_settings->spec.maxSenders) {
            // Max sender limit specified and violated.
            qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, QD_LOG_INFO,
                "[C%"PRIu64"] DENY AMQP Attach sender for user '%s', rhost '%s', vhost '%s' based on maxSenders limit",
                qd_conn->connection_id, qd_conn->user_id, hostip, vhost);
            _qd_policy_deny_amqp_sender_link(pn_link, qd_conn, QD_AMQP_COND_RESOURCE_LIMIT_EXCEEDED);
            return false;
        } else {
            // max sender limit not violated
        }
    } else {
        // max sender limit not specified
    }
    // Approve sender link based on target
    const char * target = pn_terminus_get_address(pn_link_remote_target(pn_link));
    bool lookup;
    if (target && *target) {
        // a target is specified
        if (!qd_conn->policy_settings->spec.allowWaypointLinks) {
            bool waypoint = qd_policy_terminus_is_waypoint(pn_link_remote_target(pn_link));
            if (waypoint) {
                qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, QD_LOG_INFO,
                       "[C%"PRIu64"] DENY AMQP Attach sender link '%s' for user '%s', rhost '%s', vhost '%s'.  Waypoint capability not permitted",
                       qd_conn->connection_id, target, qd_conn->user_id, hostip, vhost);
                _qd_policy_deny_amqp_sender_link(pn_link, qd_conn, QD_AMQP_COND_UNAUTHORIZED_ACCESS);
                return false;
            }
        }

        if (!qd_conn->policy_settings->spec.allowFallbackLinks) {
            bool fallback = qd_policy_terminus_is_fallback(pn_link_remote_target(pn_link));
            if (fallback) {
                qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, QD_LOG_INFO,
                       "[C%"PRIu64"] DENY AMQP Attach sender link '%s' for user '%s', rhost '%s', vhost '%s'.  Fallback capability not permitted",
                       qd_conn->connection_id, target, qd_conn->user_id, hostip, vhost);
                _qd_policy_deny_amqp_sender_link(pn_link, qd_conn, QD_AMQP_COND_UNAUTHORIZED_ACCESS);
                return false;
            }
        }

        lookup = qd_policy_approve_link_name(qd_conn->user_id, qd_conn->policy_settings, target, false);

        qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, (lookup ? QD_LOG_TRACE : QD_LOG_INFO),
            "[C%"PRIu64"] %s AMQP Attach sender link '%s' for user '%s', rhost '%s', vhost '%s' based on link target name",
            qd_conn->connection_id, (lookup ? "ALLOW" : "DENY"), target, qd_conn->user_id, hostip, vhost);

        if (!lookup) {
            _qd_policy_deny_amqp_sender_link(pn_link, qd_conn, QD_AMQP_COND_UNAUTHORIZED_ACCESS);
            return false;
        }
    } else {
        // A sender with no remote target.
        // This happens all the time with anonymous relay
        lookup = qd_conn->policy_settings->spec.allowAnonymousSender;
        qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, (lookup ? QD_LOG_TRACE : QD_LOG_INFO),
            "[C%"PRIu64"] %s AMQP Attach anonymous sender for user '%s', rhost '%s', vhost '%s'",
            qd_conn->connection_id, (lookup ? "ALLOW" : "DENY"), qd_conn->user_id, hostip, vhost);
        if (!lookup) {
            _qd_policy_deny_amqp_sender_link(pn_link, qd_conn, QD_AMQP_COND_UNAUTHORIZED_ACCESS);
            return false;
        }
    }
    // Approved
    return true;
}


bool qd_policy_approve_amqp_receiver_link(pn_link_t *pn_link, qd_connection_t *qd_conn)
{
    const char *hostip = qd_connection_remote_ip(qd_conn);
    const char *vhost = pn_connection_remote_hostname(qd_connection_pn(qd_conn));

    if (qd_conn->policy_settings->spec.maxReceivers) {
        if (qd_conn->n_receivers == qd_conn->policy_settings->spec.maxReceivers) {
            // Max sender limit specified and violated.
            qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, QD_LOG_INFO,
                "[C%"PRIu64"] DENY AMQP Attach receiver for user '%s', rhost '%s', vhost '%s' based on maxReceivers limit",
                qd_conn->connection_id, qd_conn->user_id, hostip, vhost);
            _qd_policy_deny_amqp_receiver_link(pn_link, qd_conn, QD_AMQP_COND_RESOURCE_LIMIT_EXCEEDED);
            return false;
        } else {
            // max receiver limit not violated
        }
    } else {
        // max receiver limit not specified
    }
    // Approve receiver link based on source
    bool dynamic_src = pn_terminus_is_dynamic(pn_link_remote_source(pn_link));
    if (dynamic_src) {
        bool lookup = qd_conn->policy_settings->spec.allowDynamicSource;
        qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, (lookup ? QD_LOG_TRACE : QD_LOG_INFO),
            "[C%"PRIu64"] %s AMQP Attach receiver dynamic source for user '%s', rhost '%s', vhost '%s',",
            qd_conn->connection_id, (lookup ? "ALLOW" : "DENY"), qd_conn->user_id, hostip, vhost);
        // Dynamic source policy rendered the decision
        if (!lookup) {
            _qd_policy_deny_amqp_receiver_link(pn_link, qd_conn, QD_AMQP_COND_UNAUTHORIZED_ACCESS);
        }
        return lookup;
    }
    const char * source = pn_terminus_get_address(pn_link_remote_source(pn_link));
    if (source && *source) {
        // a source is specified
        if (!qd_conn->policy_settings->spec.allowWaypointLinks) {
            bool waypoint = qd_policy_terminus_is_waypoint(pn_link_remote_source(pn_link));
            if (waypoint) {
                qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, QD_LOG_INFO,
                       "[C%"PRIu64"] DENY AMQP Attach receiver link '%s' for user '%s', rhost '%s', vhost '%s'.  Waypoint capability not permitted",
                       qd_conn->connection_id, source, qd_conn->user_id, hostip, vhost);
                _qd_policy_deny_amqp_sender_link(pn_link, qd_conn, QD_AMQP_COND_UNAUTHORIZED_ACCESS);
                return false;
            }
        }

        if (!qd_conn->policy_settings->spec.allowFallbackLinks) {
            bool fallback = qd_policy_terminus_is_fallback(pn_link_remote_source(pn_link));
            if (fallback) {
                qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, QD_LOG_INFO,
                       "[C%"PRIu64"] DENY AMQP Attach receiver link '%s' for user '%s', rhost '%s', vhost '%s'.  Fallback capability not permitted",
                       qd_conn->connection_id, source, qd_conn->user_id, hostip, vhost);
                _qd_policy_deny_amqp_sender_link(pn_link, qd_conn, QD_AMQP_COND_UNAUTHORIZED_ACCESS);
                return false;
            }
        }

        bool lookup = qd_policy_approve_link_name(qd_conn->user_id, qd_conn->policy_settings, source, true);

        qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, (lookup ? QD_LOG_TRACE : QD_LOG_INFO),
            "[C%"PRIu64"] %s AMQP Attach receiver link '%s' for user '%s', rhost '%s', vhost '%s' based on link source name",
            qd_conn->connection_id, (lookup ? "ALLOW" : "DENY"), source, qd_conn->user_id, hostip, vhost);

        if (!lookup) {
            _qd_policy_deny_amqp_receiver_link(pn_link, qd_conn, QD_AMQP_COND_UNAUTHORIZED_ACCESS);
            return false;
        }
    } else {
        // A receiver with no remote source.
        qd_log(qd_server_dispatch(qd_conn->server)->policy->log_source, QD_LOG_INFO,
               "[C%"PRIu64"] DENY AMQP Attach receiver link '' for user '%s', rhost '%s', vhost '%s'",
               qd_conn->connection_id, qd_conn->user_id, hostip, vhost);
        _qd_policy_deny_amqp_receiver_link(pn_link, qd_conn, QD_AMQP_COND_UNAUTHORIZED_ACCESS);
        return false;
    }
    // Approved
    return true;
}


void qd_policy_amqp_open(qd_connection_t *qd_conn) {
    pn_connection_t *conn = qd_connection_pn(qd_conn);
    qd_dispatch_t *qd = qd_server_dispatch(qd_conn->server);
    qd_policy_t *policy = qd->policy;
    bool connection_allowed = true;

    const char *policy_vhost = 0;
    if (!!qd_conn->listener)
        policy_vhost = qd_conn->listener->config.policy_vhost;

    if (policy->enableVhostPolicy && (!qd_conn->role || strcmp(qd_conn->role, "inter-router"))) {
        // Open connection or not based on policy.
        pn_transport_t *pn_trans = pn_connection_transport(conn);
        const char *hostip = qd_connection_remote_ip(qd_conn);
        const char *pcrh = pn_connection_remote_hostname(conn);
        const char *vhost = (policy_vhost ? policy_vhost : (pcrh ? pcrh : ""));
        const char *conn_name = qd_connection_name(qd_conn);
#define SETTINGS_NAME_SIZE 256
        char settings_name[SETTINGS_NAME_SIZE];
        uint32_t conn_id = qd_conn->connection_id;
        if (!qd_conn->policy_settings) {
            qd_conn->policy_settings = new_qd_policy_settings_t();
            ZERO(qd_conn->policy_settings);
        }

        if (qd_policy_open_lookup_user(policy, qd_conn->user_id, hostip, vhost, conn_name,
                                       settings_name, SETTINGS_NAME_SIZE, conn_id) &&
            settings_name[0]) {
            // This connection is allowed by policy.
            // Apply transport policy settings
            if (qd_policy_open_fetch_settings(policy, vhost, settings_name, qd_conn->policy_settings)) {
                if (qd_conn->policy_settings->spec.maxFrameSize > 0)
                    pn_transport_set_max_frame(pn_trans, qd_conn->policy_settings->spec.maxFrameSize);
                if (qd_conn->policy_settings->spec.maxSessions > 0)
                    pn_transport_set_channel_max(pn_trans, qd_conn->policy_settings->spec.maxSessions - 1);
                const qd_server_config_t *cf = qd_connection_config(qd_conn);
                if (cf && cf->multi_tenant) {
                    char vhost_name_buf[SETTINGS_NAME_SIZE];
                    if (qd_policy_lookup_vhost_alias(policy, vhost, vhost_name_buf, SETTINGS_NAME_SIZE)) {
                        if (pcrh && !strcmp(pcrh, vhost_name_buf)) {
                            // Default condition: use proton connection value; no action here
                        } else {
                            // Policy used a name different from what came in the AMQP Open hostname.
                            // Memorize it for multitenant namespace
                            qd_conn->policy_settings->vhost_name = (char*)malloc(strlen(vhost_name_buf) + 1);
                            strcpy(qd_conn->policy_settings->vhost_name, vhost_name_buf);
                        }
                    }
                } else {
                    // not multi-tenant: don't look for vhost
                }
            } else {
                // failed to fetch settings
                connection_allowed = false;
            }
        } else {
            // This connection is denied by policy.
            connection_allowed = false;
        }
    } else {
        // No policy implies automatic policy allow
        // Note that connections not governed by policy have no policy_settings.
    }
    if (connection_allowed) {
        if (pn_connection_state(conn) & PN_LOCAL_UNINIT)
            pn_connection_open(conn);
        policy_notify_opened(qd_conn->open_container, qd_conn, qd_conn->context);
    } else {
        qd_policy_private_deny_amqp_connection(conn, QD_AMQP_COND_RESOURCE_LIMIT_EXCEEDED, CONNECTION_DISALLOWED);
    }
}


void qd_policy_amqp_open_connector(qd_connection_t *qd_conn) {
    pn_connection_t *conn = qd_connection_pn(qd_conn);
    qd_dispatch_t *qd = qd_server_dispatch(qd_conn->server);
    qd_policy_t *policy = qd->policy;
    bool connection_allowed = true;

    if (policy->enableVhostPolicy &&
        (!qd_conn->role || !strcmp(qd_conn->role, "normal") || !strcmp(qd_conn->role, "route-container"))) {
        // Open connection or not based on policy.
        uint32_t conn_id = qd_conn->connection_id;

        qd_connector_t *connector = qd_connection_connector(qd_conn);
        const char *policy_vhost = qd_connector_policy_vhost(connector);

        if (policy_vhost && strlen(policy_vhost) > 0) {
            qd_conn->policy_settings = new_qd_policy_settings_t();
            if (qd_conn->policy_settings) {
                ZERO(qd_conn->policy_settings);

                if (qd_policy_open_fetch_settings(policy, policy_vhost, POLICY_VHOST_GROUP, qd_conn->policy_settings)) {
                    qd_conn->policy_settings->spec.outgoingConnection = true;
                    qd_conn->policy_counted = true; // Count senders and receivers for this connection
                } else {
                    qd_log(policy->log_source,
                        QD_LOG_ERROR,
                        "[C%"PRIu64"] Failed to find policyVhost settings for connection '%d', policyVhost: '%s'",
                        qd_conn->connection_id, conn_id, policy_vhost);
                    connection_allowed = false;
                }
            } else {
                connection_allowed = false; // failed to allocate settings
            }
        } else {
            // This connection is allowed since no policy is specified for the connector
        }
    } else {
        // No policy implies automatic policy allow
        // Note that connections not governed by policy have no policy_settings.
    }
    if (connection_allowed) {
        policy_notify_opened(qd_conn->open_container, qd_conn, qd_conn->context);
    } else {
        qd_policy_private_deny_amqp_connection(conn, QD_AMQP_COND_RESOURCE_LIMIT_EXCEEDED, CONNECTION_DISALLOWED);
    }
}


void qd_policy_settings_free(qd_policy_settings_t *settings)
{
    if (!settings) return;
    if (settings->sources)         free(settings->sources);
    if (settings->targets)         free(settings->targets);
    if (settings->sourcePattern)   free(settings->sourcePattern);
    if (settings->targetPattern)   free(settings->targetPattern);
    if (settings->sourceParseTree) qd_parse_tree_free(settings->sourceParseTree);
    if (settings->targetParseTree) qd_parse_tree_free(settings->targetParseTree);
    if (settings->vhost_name)      free(settings->vhost_name);
    free_qd_policy_settings_t(settings);
}


bool qd_policy_approve_link_name(const char *username,
                                 const qd_policy_settings_t *settings,
                                 const char *proposed,
                                 bool isReceiver)
{
    if (isReceiver) {
        if (settings->sourceParseTree) {
            return _qd_policy_approve_link_name_tree(username, settings->sourcePattern, proposed, settings->sourceParseTree);
        } else if (settings->sources) {
            return _qd_policy_approve_link_name(username, settings->sources, proposed);
        }
    } else {
        if (settings->targetParseTree) {
            return _qd_policy_approve_link_name_tree(username, settings->targetPattern, proposed, settings->targetParseTree);
        } else if (settings->targets) {
            return _qd_policy_approve_link_name(username, settings->targets, proposed);
        }
    }
    return false;
}


// Add a hostname to the lookup parse_tree
bool qd_policy_host_pattern_add(qd_policy_t *policy, const char *hostPattern)
{
    void *payload = strdup(hostPattern);
    sys_mutex_lock(policy->tree_lock);
    qd_error_t rc = qd_parse_tree_add_pattern_str(policy->hostname_tree, hostPattern, payload);
    sys_mutex_unlock(policy->tree_lock);

    if (rc != QD_ERROR_NONE) {
        const char *err = qd_error_name(rc);
        free(payload);
        qd_log(policy->log_source,
               QD_LOG_WARNING,
               "vhost hostname pattern '%s' add failed: %s",
               hostPattern, err ? err : "unknown error");
        qd_error_clear();  // allow policy agent to raise PolicyError
    }
    return rc == QD_ERROR_NONE;
}


// Remove a hostname from the lookup parse_tree
void qd_policy_host_pattern_remove(qd_policy_t *policy, const char *hostPattern)
{
    sys_mutex_lock(policy->tree_lock);
    void *oldp = qd_parse_tree_remove_pattern_str(policy->hostname_tree, hostPattern);
    sys_mutex_unlock(policy->tree_lock);
    if (oldp) {
        free(oldp);
    } else {
        qd_log(policy->log_source, QD_LOG_WARNING, "vhost hostname pattern '%s' for removal not found", hostPattern);
    }
}


// Look up a hostname in the lookup parse_tree
char * qd_policy_host_pattern_lookup(qd_policy_t *policy, const char *hostPattern)
{
    void *payload = 0;
    sys_mutex_lock(policy->tree_lock);
    bool matched = qd_parse_tree_retrieve_match_str(policy->hostname_tree, hostPattern, &payload);
    sys_mutex_unlock(policy->tree_lock);
    if (!matched) {
        payload = 0;
    }
    qd_log(policy->log_source, QD_LOG_TRACE, "vhost hostname pattern '%s' lookup returned '%s'", 
           hostPattern, (payload ? (char *)payload : "null"));
    return payload;
}


// free the hostname parse tree and associated resources
//
static bool _hostname_tree_free_payload(void *handle,
                                        const char *pattern,
                                        void *payload)
{
    free(payload);
    return true;
}

static void hostname_tree_free(qd_parse_tree_t *hostname_tree)
{
    qd_parse_tree_walk(hostname_tree, _hostname_tree_free_payload, NULL);
    qd_parse_tree_free(hostname_tree);
}

// Convert naked CSV allow list into parsed settings 3-tuple
// Note that this logic is also present in python compile_app_settings.
char * qd_policy_compile_allowed_csv(char * csv)
{
    size_t csv_len = strlen(csv);
    size_t usersubst_len = strlen(user_subst_key);

    size_t n_commas = 0;
    char * pch = strchr(csv, *QPALN_COMMA_SEP);
    while (pch != NULL) {
        n_commas++;
        pch = strchr(pch + 1, *QPALN_COMMA_SEP);
    }

    size_t result_size = csv_len + 3 * (n_commas + 1) + 1; // each token gets ctrl char and 2 commas
    char * result = (char *)malloc(result_size);
    if (!result)
        return NULL;
    result[0] = '\0';

    char * dup = strdup(csv);
    if (!dup) {
        free(result);
        return NULL;
    }
    char * dupend = dup + csv_len;

    size_t tok_size = 0;
    char * sep = "";
    for (pch = dup; pch < dupend; pch += tok_size + 1) {
        // isolate token
        char * pcomma = strchr(pch, *QPALN_COMMA_SEP);
        if (!pcomma) pcomma = dupend;
        *pcomma = '\0';
        tok_size = pcomma - pch;

        strcat(result, sep);
        sep = ",";
        char * psubst = strstr(pch, user_subst_key);
        if (psubst) {
            // substitute token is present
            if (psubst == pch) {
                // token is a prefix
                strcat(result, user_subst_i_prefix);
                strcat(result, ",,");
                strcat(result, pch + usersubst_len);
            } else if (psubst == pch + tok_size - usersubst_len) {
                // token is a suffix
                strcat(result, user_subst_i_suffix);
                strcat(result, ",");
                strncat(result, pch, tok_size - usersubst_len);
                strcat(result, ",");
            } else {
                // token is embedded
                strcat(result, user_subst_i_embed);
                strcat(result, ",");
                strncat(result, pch, psubst - pch);
                strcat(result, ",");
                strncat(result, psubst + usersubst_len, tok_size - (psubst - pch) - usersubst_len);
            }
        } else {
            // substitute token is absent
            if (strcmp(pch, user_subst_i_wildcard) == 0) {
                // token is wildcard
                strcat(result, user_subst_i_wildcard);
                strcat(result, ",,");
            } else {
                // token is ordinary string
                strcat(result, user_subst_i_absent);
                strcat(result, ",");
                strcat(result, pch);
                strcat(result, ",");
            }
        }
    }
    free(dup);
    return result;
}


qd_log_source_t* qd_policy_log_source() {
    return policy_log_source;
}
