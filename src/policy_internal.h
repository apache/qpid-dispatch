#ifndef __policy_internal_h__
#define __policy_internal_h__
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

#include "policy.h"

/**
 * Private Function Prototypes
 */
/** Set the error condition and close the connection.
 * Over the wire this will send an open frame followed
 * immediately by a close frame with the error condition.
 * @param[in] conn proton connection being closed
 * @param[in] cond_name condition name
 * @param[in] cond_descr condition description
 **/ 
void qd_policy_private_deny_amqp_connection(pn_connection_t *conn, const char *cond_name, const char *cond_descr);


/** Internal function to deny an amqp session
 * The session is closed with a condition and the denial is logged and counted.
 * @param[in,out] ssn proton session being closed
 * @param[in,out] qd_conn dispatch connection
 */
void qd_policy_deny_amqp_session(pn_session_t *ssn, qd_connection_t *qd_conn);


/** Internal function to deny an amqp link
 * The link is closed and the denial is logged but not counted.
 * @param[in] link proton link being closed
 * @param[in] qd_conn the qd conection
 * @param[in] condition the AMQP error with which to close the link
 */ 
void _qd_policy_deny_amqp_link(pn_link_t *link, qd_connection_t *qd_conn, const char *condition);


/** Internal function to deny a sender amqp link
 * The link is closed and the denial is logged but not counted.
 * @param[in] link proton link to close
 * @param[in] qd_conn the qd conection
 * @param[in] condition the AMQP error with which to close the link
 */ 
void _qd_policy_deny_amqp_sender_link(pn_link_t *pn_link, qd_connection_t *qd_conn, const char *condition);


/** Internal function to deny a receiver amqp link
 * The link is closed and the denial is logged but not counted.
 * @param[in] link proton link to close
 * @param[in] qd_conn the qd conection
 * @param[in] condition the AMQP error with which to close the link
 */ 
void _qd_policy_deny_amqp_receiver_link(pn_link_t *pn_link, qd_connection_t *qd_conn, const char *condition);


/** Perform user name substitution into proposed link name.
 * The scheme is to substitute '${user}' into the incoming link name whereever the
 * the username is present. Then it can be matched against the original template with
 * a minimum of substitutions. For example:
 * uname    : joe
 * proposed : temp_joe_1
 * obuf     : temp_${user}_1
 * Note: substituted names are limited to osize characters
 * Note: only the first (leftmost) user name is substituted.
 *
 * @param[in] uname auth user name
 * @param[in] proposed the link name from the AMQP frame
 * @param[out] obuf where the constructed link name is returned
 * @param[in] osize size in bytes of obuf
 * @return NULL if uname is not present in proposed link name.
 */
char * _qd_policy_link_user_name_subst(const char *uname, const char *proposed, char *obuf, int osize);


/** Approve link by source/target name.
 * This match supports trailing wildcard match:
 *    proposed 'temp-305' matches allowed 'temp-*'
 * This match supports username substitution:
 *    user 'joe', proposed 'temp-joe' matches allowed 'temp-${user}'
 * Both username substitution and wildcards are allowed:
 *    user 'joe', proposed 'temp-joe-100' matches allowed 'temp-${user}*'
 * @param[in] username authenticated user name
 * @param[in] allowed policy settings source/target string in packed CSV form.
 * @param[in] proposed the link target name to be approved
 */
bool _qd_policy_approve_link_name(const char *username, const char *allowed, const char *proposed);
#endif
