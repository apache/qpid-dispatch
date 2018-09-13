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
#include <inttypes.h>

struct qdr_edge_t {
    qdr_core_t        *core;
    qdr_connection_t  *conn;           // current active connection to interior

    qdr_link_t        *mgmt_link;      // outgoing link to interior $management
    qdr_link_t        *mgmt_reply_to;  // for management reply messages
};
ALLOC_DECLARE(qdr_edge_t);
ALLOC_DEFINE(qdr_edge_t);



qdr_edge_t *qdr_edge(qdr_core_t *core)
{
    qdr_edge_t *edge = new_qdr_edge_t();
    ZERO(edge);
    edge->core = core;
    // TODO initialize
    return edge;
}


void qdr_edge_free(qdr_edge_t *edge)
{
    if (edge) {
        // TODO cleanup
        free_qdr_edge_t(edge);
    }
}


//
// The router is in edge mode and the connection to the interior router
// has opened
//
void qdr_edge_connection_opened(qdr_edge_t *edge, qdr_connection_t *conn)
{
    qd_log(edge->core->log, QD_LOG_TRACE,
           "edge connection to interior opened (id=%"PRIu64")", conn->identity);
    edge->conn = conn;
}


//
// The router is in edge mode and the connection to the interior router
// has closed
//
void qdr_edge_connection_closed(qdr_edge_t *edge)
{
    qd_log(edge->core->log, QD_LOG_TRACE,
           "edge connection to interior closed");
    edge->conn = NULL;
}
