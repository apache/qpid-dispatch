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

#include <proton/connection.h>

#include "connection.h"
#include "connector.h"
#include "server_private.h"

ALLOC_DEFINE(qd_connector_t);

const char* qd_connector_policy_vhost(qd_connector_t* connector) {
    return connector->policy_vhost;
}

bool qd_connector_connect(qd_connector_t* connector) {
    sys_mutex_lock(connector->lock);

    connector->ctx = NULL;
    connector->delay = 0;

    // Referenced by timer
    sys_atomic_inc(&connector->ref_count);
    qd_timer_schedule(connector->timer, connector->delay);

    sys_mutex_unlock(connector->lock);

    return true;
}

bool qd_connector_decref(qd_connector_t* connector) {
    if (connector && sys_atomic_dec(&connector->ref_count) == 1) {
        sys_mutex_lock(connector->lock);

        if (connector->ctx) {
            connector->ctx->connector = NULL;
        }

        sys_mutex_unlock(connector->lock);

        qd_server_config_free(&connector->config);
        qd_timer_free(connector->timer);

        qd_failover_item_t* item = DEQ_HEAD(connector->conn_info_list);

        while (item) {
            DEQ_REMOVE_HEAD(connector->conn_info_list);
            free(item->scheme);
            free(item->host);
            free(item->port);
            free(item->hostname);
            free(item->host_port);
            free(item);
            item = DEQ_HEAD(connector->conn_info_list);
        }

        sys_mutex_free(connector->lock);

        if (connector->policy_vhost) {
            free(connector->policy_vhost);
        }

        free(connector->conn_msg);
        free_qd_connector_t(connector);

        return true;
    }

    return false;
}

qd_failover_item_t* qd_connector_get_conn_info(qd_connector_t* connector) {
    qd_failover_item_t* item = DEQ_HEAD(connector->conn_info_list);

    if (DEQ_SIZE(connector->conn_info_list) > 1) {
        for (int i = 1; i < connector->conn_index; i++) {
            item = DEQ_NEXT(item);
        }
    }

    return item;
}

bool qd_connector_has_failover_info(qd_connector_t* connector) {
    if (connector && DEQ_SIZE(connector->conn_info_list) > 1) {
        return true;
    }

    return false;
}

const qd_server_config_t* qd_connector_config(const qd_connector_t* connector) {
    return &connector->config;
}
