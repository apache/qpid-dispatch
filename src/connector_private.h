#ifndef __connector_private_h__
#define __connector_private_h__ 1

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

// Connector objects represent the desire to create and maintain an
// outgoing transport connection
struct qd_connector_t {
    /* May be referenced by connection_manager, timer and pn_connection_t */
    sys_atomic_t              ref_count;
    qd_server_t              *server;
    qd_server_config_t        config;
    qd_timer_t               *timer;
    long                      delay;

    /* Connector state and ctx can be modified in proactor or management threads. */
    sys_mutex_t              *lock;
    cxtr_state_t              state;
    char                     *conn_msg;
    qd_connection_t          *ctx;

    /* This conn_list contains all the connection information needed to make a connection. It also includes failover connection information */
    qd_failover_item_list_t   conn_info_list;
    int                       conn_index; // Which connection in the connection list to connect to next.

    /* Optional policy vhost name */
    char                     *policy_vhost;

    DEQ_LINKS(qd_connector_t);
};

DEQ_DECLARE(qd_connector_t, qd_connector_list_t);
ALLOC_DECLARE(qd_connector_t);

const char *qd_connector_policy_vhost(qd_connector_t* connector);
const qd_server_config_t *qd_connector_config(const qd_connector_t *connector);
bool qd_connector_decref(qd_connector_t* connector);
qd_failover_item_t *qd_connector_get_conn_info(qd_connector_t *connector);
bool qd_connector_has_failover_info(qd_connector_t* connector);

#endif
