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

#include "core_link_endpoint.h"
#include "module.h"

#include "qpid/dispatch/ctools.h"

#include <inttypes.h>

#define CREDIT_WINDOW       2
#define HEARTBEAT_THRESHOLD 8

typedef struct endpoint_ref {
    DEQ_LINKS(struct endpoint_ref);
    qdrc_endpoint_t  *endpoint;
    const char       *container_id;
    qdr_connection_t *conn;
    uint64_t          conn_id;
    int               last_heartbeat;
} endpoint_ref_t;
DEQ_DECLARE(endpoint_ref_t, endpoint_ref_list_t);
ALLOC_DECLARE(endpoint_ref_t);
ALLOC_DEFINE(endpoint_ref_t);


static struct {
    qdr_core_t          *core;
    qdr_core_timer_t    *timer;
    endpoint_ref_list_t  endpoints;
} _server_state;


static void _on_transfer(void           *link_context,
                         qdr_delivery_t *delivery,
                         qd_message_t   *message)
{
    if (!qd_message_receive_complete(message))
        return;

    endpoint_ref_t *epr = (endpoint_ref_t*) link_context;

    epr->last_heartbeat = qdr_core_uptime_ticks(_server_state.core);

    qdrc_endpoint_settle_CT(_server_state.core, delivery, PN_ACCEPTED);
    qdrc_endpoint_flow_CT(_server_state.core, epr->endpoint, 1, false);
}


static void _on_first_attach(void            *bind_context,
                             qdrc_endpoint_t *endpoint,
                             void            **link_context,
                             qdr_terminus_t  *remote_source,
                             qdr_terminus_t  *remote_target)
{
    //
    // Only accept incoming links. Detach all other links
    //
    qdr_connection_t *conn = qdrc_endpoint_get_connection_CT(endpoint);
    if (qdrc_endpoint_get_direction_CT(endpoint) != QD_INCOMING) {
        *link_context = 0;
        qdrc_endpoint_detach_CT(_server_state.core, endpoint, 0);
        qd_log(_server_state.core->log, QD_LOG_ERROR,
               "Attempt to attach to heartbeat server rejected (container=%s)",
               (conn->connection_info) ? conn->connection_info->container : "<unknown>");
        qdr_terminus_free(remote_source);
        qdr_terminus_free(remote_target);
        return;
    }

    endpoint_ref_t *epr = new_endpoint_ref_t();
    ZERO(epr);
    epr->endpoint       = endpoint;
    epr->last_heartbeat = qdr_core_uptime_ticks(_server_state.core);
    epr->container_id   = (conn->connection_info) ? conn->connection_info->container : "<unknown>";
    epr->conn           = conn;
    epr->conn_id        = conn->identity;
    DEQ_INSERT_TAIL(_server_state.endpoints, epr);
    *link_context = epr;
    qdrc_endpoint_second_attach_CT(_server_state.core, endpoint, remote_source, remote_target);
    qdrc_endpoint_flow_CT(_server_state.core, endpoint, CREDIT_WINDOW, false);

    qd_log(_server_state.core->log, QD_LOG_INFO,
           "[C%"PRIu64"] Client attached to heartbeat server (container=%s, endpoint=%p)",
           epr->conn_id, epr->container_id, (void*) endpoint);
}


/* handle incoming detach from client
 */
static void _on_cleanup(void *link_context)
{
    endpoint_ref_t *epr = (endpoint_ref_t*) link_context;
    if (epr != 0) {
        qd_log(_server_state.core->log, QD_LOG_INFO,
            "[C%"PRIu64"] Client detached from heartbeat server (container=%s, endpoint=%p)",
            epr->conn_id, epr->container_id, (void*) epr->endpoint);
        DEQ_REMOVE(_server_state.endpoints, epr);
        free_endpoint_ref_t(epr);
    }
}


static qdrc_endpoint_desc_t _endpoint_handlers =
{
    .label           = "heartbeat",
    .on_first_attach = _on_first_attach,
    .on_transfer     = _on_transfer,
    .on_cleanup      = _on_cleanup,
};


static void on_timer(qdr_core_t *core, void *context)
{
    qdr_core_timer_schedule_CT(core, _server_state.timer, 2);
    endpoint_ref_t *epr = DEQ_HEAD(_server_state.endpoints);
    while (epr) {
        if (qdr_core_uptime_ticks(core) - epr->last_heartbeat > HEARTBEAT_THRESHOLD) {
            qd_log(core->log, QD_LOG_INFO, "[C%"PRIu64"] Lost heartbeat from container %s, closing connection",
            epr->conn_id, epr->container_id);
            qdr_close_connection_CT(core, epr->conn);
        }
        epr = DEQ_NEXT(epr);
    }
}


static bool _heartbeat_server_enable_CT(qdr_core_t *core)
{
    return true;
}


static void _heartbeat_server_init_CT(qdr_core_t *core, void **module_context)
{
    _server_state.core = core;

    //
    // Handle any incoming links to the QD_TERMINUS_ADDRESS_LOOKUP address
    //
    qdrc_endpoint_bind_mobile_address_CT(core,
                                         QD_TERMINUS_HEARTBEAT,
                                         '0', // phase
                                         &_endpoint_handlers,
                                         &_server_state);
    _server_state.timer = qdr_core_timer_CT(core, on_timer, 0);
    qdr_core_timer_schedule_CT(core, _server_state.timer, 2);
    *module_context = &_server_state;
}


static void _heartbeat_server_final_CT(void *module_context)
{
    qdr_core_timer_free_CT(_server_state.core, _server_state.timer);
    endpoint_ref_t *epr = DEQ_HEAD(_server_state.endpoints);
    while (epr) {
        DEQ_REMOVE_HEAD(_server_state.endpoints);
        free_endpoint_ref_t(epr);
        epr = DEQ_HEAD(_server_state.endpoints);
    }
}


QDR_CORE_MODULE_DECLARE("heartbeat_server", _heartbeat_server_enable_CT, _heartbeat_server_init_CT, _heartbeat_server_final_CT)
