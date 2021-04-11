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

#include "connection_manager.h"

#include "core_events.h"
#include "router_core_private.h"

#include <inttypes.h>

//
// This is the Connection Manager component of the Edge Router module.
//
// The Connection Manager is responsible for keeping track of all of the
// edge connections to Interior routers and choosing one to be the active
// edge connection.  An edge router may maintain multiple "edge-connection"
// connections to different Interior routers.  Only one of those connections
// will be designated as active and carry edge traffic.  This component
// identifies the active edge connection and generates outbound core events
// to notify other interested parties:
//
//     QDRC_EVENT_CONN_EDGE_ESTABLISHED
//     QDRC_EVENT_CONN_EDGE_LOST
//

struct qcm_edge_conn_mgr_t {
    qdr_core_t                *core;
    qdrc_event_subscription_t *event_sub;
    qdr_connection_t          *active_edge_connection;
};


static void on_conn_event(void *context, qdrc_event_t event, qdr_connection_t *conn)
{
    qcm_edge_conn_mgr_t *cm = (qcm_edge_conn_mgr_t*) context;

    switch (event) {
    case QDRC_EVENT_CONN_OPENED :
        if (cm->active_edge_connection == 0 && conn->role == QDR_ROLE_EDGE_CONNECTION) {
            qd_log(cm->core->log, QD_LOG_INFO, "Edge connection (id=%"PRIu64") to interior established", conn->identity);
            cm->active_edge_connection = conn;
            cm->core->active_edge_connection = conn;
            qdrc_event_conn_raise(cm->core, QDRC_EVENT_CONN_EDGE_ESTABLISHED, conn);
        }
        break;

    case QDRC_EVENT_CONN_CLOSED :
        if (cm->active_edge_connection == conn) {
            qdrc_event_conn_raise(cm->core, QDRC_EVENT_CONN_EDGE_LOST, conn);
            qdr_connection_t *alternate = DEQ_HEAD(cm->core->open_connections);
            while (alternate && (alternate == conn || alternate->role != QDR_ROLE_EDGE_CONNECTION))
                alternate = DEQ_NEXT(alternate);
            if (alternate) {
                qd_log(cm->core->log, QD_LOG_INFO,
                       "Edge connection (id=%"PRIu64") to interior lost, activating alternate id=%"PRIu64"",
                       conn->identity, alternate->identity);
                cm->active_edge_connection = alternate;
                cm->core->active_edge_connection = alternate;
                qdrc_event_conn_raise(cm->core, QDRC_EVENT_CONN_EDGE_ESTABLISHED, alternate);
            } else {
                qd_log(cm->core->log, QD_LOG_INFO,
                       "Edge connection (id=%"PRIu64") to interior lost, no alternate connection available",
                       conn->identity);
                cm->active_edge_connection = 0;
            }
        }
        break;

    default:
        assert(false);
        break;
    }
}


qcm_edge_conn_mgr_t *qcm_edge_conn_mgr(qdr_core_t *core)
{
    qcm_edge_conn_mgr_t *cm = NEW(qcm_edge_conn_mgr_t);

    cm->core = core;
    cm->event_sub = qdrc_event_subscribe_CT(core,
                                            QDRC_EVENT_CONN_OPENED | QDRC_EVENT_CONN_CLOSED,
                                            on_conn_event,
                                            0,
                                            0,
                                            0,
                                            cm);
    cm->active_edge_connection = 0;

    return cm;
}


void qcm_edge_conn_mgr_final(qcm_edge_conn_mgr_t *cm)
{
    qdrc_event_unsubscribe_CT(cm->core, cm->event_sub);
    free(cm);
}

