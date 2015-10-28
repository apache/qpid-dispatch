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

//
// error conditions
//
static char* RESOURCE_LIMIT_EXCEEDED     = "amqp:resource-limit-exceeded";
//static char* UNAUTHORIZED_ACCESS         = "amqp:unauthorized-access";
//static char* CONNECTION_FORCED           = "amqp:connection:forced";

//
// error descriptions
//
static char* CONNECTION_DISALLOWED         = "connection disallowed by local policy";


void qd_policy_handle_open(void *context, bool discard)
{
    qd_connection_t *qd_conn = (qd_connection_t *)context;
    
    if (!discard) {
        pn_connection_t *conn = qd_connection_pn(qd_conn);

        if (allow_this) { // TODO: Consult actual policy engine
            // This connection is allowed.
            if (pn_connection_state(conn) & PN_LOCAL_UNINIT)
                pn_connection_open(conn);
            qd_connection_manager_connection_opened(qd_conn);
        } else {
            // This connection is denied.
            // Set the error condition and close the connection.
            // Over the wire this will send an open frame followed
            // immediately by a close frame with the error condition.
            pn_condition_t * cond = pn_connection_condition(conn);
            (void) pn_condition_set_name(       cond, RESOURCE_LIMIT_EXCEEDED);
            (void) pn_condition_set_description(cond, CONNECTION_DISALLOWED);
            pn_connection_close(conn);
        }
    
        // update the policy
        //allow_this = !allow_this;
    }
    qd_connection_set_event_stall(qd_conn, false);
}
